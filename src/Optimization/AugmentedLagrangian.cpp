#include <Eigen/CholmodSupport>
#include <fstream>
#include <iomanip>
#include "../../include/Optimization/LineSearch.h"
#include "../../include/Optimization/AugmentedLagrangian.h"
#include "../../include/timer.h"
#include <iostream>

void OptSolver::augmentedLagrangianSolver(
	std::function<double(const Eigen::VectorXd&, Eigen::VectorXd*, Eigen::SparseMatrix<double>*, bool)> objFunc,
	std::function<double(const Eigen::VectorXd&, const Eigen::VectorXd&)> findMaxStep,
	std::function<double(const Eigen::VectorXd&, const Eigen::VectorXd&, Eigen::VectorXd*, Eigen::SparseMatrix<double>*, bool, Eigen::VectorXd*)> constraintsFunc,
	std::function<double(const Eigen::VectorXd&, const double&, Eigen::VectorXd*, Eigen::SparseMatrix<double>*, bool)> penaltyFunc,
	Eigen::VectorXd& x0, Eigen::VectorXd& lambda, double& mu,
	int numIter, double gradTol, double xTol, double cTol, bool displayInfo, std::function<void(const Eigen::VectorXd&, double&, double&)> getNormFunc, std::string* savingFolder, std::function<void(Eigen::VectorXd&)> postProcess)
{
	const int DIM = x0.rows();
	Eigen::VectorXd randomVec = x0;
	randomVec.setRandom();
	x0 += 1e-6 * randomVec;
	Eigen::VectorXd grad = Eigen::VectorXd::Zero(DIM);
	Eigen::SparseMatrix<double> hessian;

	Eigen::VectorXd neggrad, delta_x;
	double maxStepSize = 1.0;
	double reg = 1e-8;

	bool isProj = true;
	Timer<std::chrono::high_resolution_clock> totalTimer;
	double totalAssemblingTime = 0;
	double totalSolvingTime = 0;
	double totalLineSearchTime = 0;

	totalTimer.start();
	std::ofstream optInfo;

	Eigen::VectorXd constVec;
	double init = constraintsFunc(x0, lambda, NULL, NULL, false, &constVec);
	std::cout << "constraint violation: " << constVec.minCoeff() << ", " << constVec.maxCoeff() << std::endl;

	int i = 0;
	for (; i < numIter; i++)
	{
		if (displayInfo)
			std::cout << "\niter: " << i << std::endl;

		auto lagrangian = [&](const Eigen::VectorXd& x, Eigen::VectorXd* grad, Eigen::SparseMatrix<double>* hess, bool isProj) {
			Eigen::VectorXd deriv, derivc, derivp;
			Eigen::SparseMatrix<double> H, Hc, Hp;
			double E = objFunc(x, grad ? &deriv : NULL, hess ? &H : NULL, isProj);
			double Ec = constraintsFunc(x, lambda, grad ? &derivc : NULL, hess ? &Hc : NULL, isProj, NULL);
			double Ep = penaltyFunc(x, mu, grad ? &derivp : NULL, hess ? &Hp : NULL, isProj);

			if (grad)
			{
				(*grad) = deriv + derivc + derivp;
			}

			if (hess)
			{
				(*hess) = H + Hc + Hp;
			}

			return E + Ec + Ep;
		};

		Timer<std::chrono::high_resolution_clock> localTimer;
		localTimer.start();
		double f = lagrangian(x0, &grad, &hessian, isProj);
		localTimer.stop();
		double localAssTime = localTimer.elapsed<std::chrono::milliseconds>() * 1e-3;
		totalAssemblingTime += localAssTime;

		localTimer.start();
		Eigen::SparseMatrix<double> H = hessian;
		Eigen::SparseMatrix<double> I(DIM, DIM);
		I.setIdentity();
		std::cout << "num of nonzeros: " << H.nonZeros() << ", rows: " << H.rows() << ", cols: " << H.cols() << std::endl;
		Eigen::CholmodSupernodalLLT<Eigen::SparseMatrix<double>> solver(H);

		//		Eigen::SimplicialLLT<Eigen::SparseMatrix<double> > solver(hessian);


		while (solver.info() != Eigen::Success)
		{
			if (displayInfo)
			{
				if (isProj)
					std::cout << "some small perturb is needed to remove round-off error, current reg = " << reg << std::endl;
				else
					std::cout << "Matrix is not positive definite, current reg = " << reg << std::endl;
			}

			H = hessian + reg * I;
			solver.compute(H);
			reg = std::max(2 * reg, 1e-16);
		}

		neggrad = -grad;
		delta_x = solver.solve(neggrad);

		localTimer.stop();
		double localSolvingTime = localTimer.elapsed<std::chrono::milliseconds>() * 1e-3;
		totalSolvingTime += localSolvingTime;


		maxStepSize = findMaxStep(x0, delta_x);

		localTimer.start();
		double rate = LineSearch::backtrackingArmijo(x0, grad, delta_x, lagrangian, maxStepSize);
		localTimer.stop();
		double localLinesearchTime = localTimer.elapsed<std::chrono::milliseconds>() * 1e-3;
		totalLineSearchTime += localLinesearchTime;


		if (!isProj)
		{
			reg *= 0.5;
			reg = std::max(reg, 1e-16);
		}

		x0 = x0 + rate * delta_x;

		double fnew = lagrangian(x0, &grad, NULL, isProj);

		//Eigen::VectorXd constVec;
		double tmpE = constraintsFunc(x0, lambda, NULL, NULL, false, &constVec);
		bool iskeepsame = true;

		if (rate * delta_x.lpNorm<Eigen::Infinity>() < std::max(1.0, xTol))
		{
			if (std::max(std::abs(constVec.minCoeff()), std::abs(constVec.maxCoeff())) > 10 * cTol && mu < 1e4)
			{
				iskeepsame = false;
				mu *= 2;
			}
			if (iskeepsame)
			{
				for (int j = 0; j < constVec.rows(); j++)
				{
					lambda(j) += mu * constVec(j);
				}
			}

		}
		double fnewAfterUpdate = lagrangian(x0, NULL, NULL, isProj);	// after update lambda and mu
		double Ec = constraintsFunc(x0, lambda, NULL, NULL, isProj, NULL);
		double Ep = penaltyFunc(x0, mu, NULL, NULL, isProj);
		if (displayInfo)
		{
			std::cout << "line search rate : " << rate << ", actual hessian : " << !isProj << ", reg = " << reg << std::endl;
			std::cout << "f_old: " << f << ", after update x, f_new: " << fnew << ", after update lambda and mu, f_new: " << fnewAfterUpdate << ", grad norm: " << grad.norm() << ", delta x: " << rate * delta_x.norm() << ", delta_f: " << f - fnew << std::endl;
			std::cout << "constraint violation: " << constVec.minCoeff() << ", " << constVec.maxCoeff() << std::endl;
			std::cout << "lambda: " << lambda.minCoeff() << ", " << lambda.maxCoeff() << ", mu: " << mu << std::endl;
			std::cout << "E const: " << Ec << ", E penalty: " << Ep << std::endl;
			if (getNormFunc)
			{
				double gradz, gradw;
				getNormFunc(grad, gradz, gradw);

				double updatez, updatew;
				getNormFunc(rate * delta_x, updatez, updatew);
				std::cout << "z grad: " << gradz << ", w grad: " << gradw << ", z change: " << updatez << ", w change: " << updatew << std::endl;
			}
			std::cout << "timing info (in total seconds): " << std::endl;
			std::cout << "assembling took: " << totalAssemblingTime << ", LLT solver took: " << totalSolvingTime << ", line search took: " << totalLineSearchTime << std::endl;
		}

		if (savingFolder)
		{
			optInfo << "line search rate : " << rate << ", actual hessian : " << !isProj << ", reg = " << reg << std::endl;
			optInfo << "f_old: " << f << ", f_new: " << fnew << ", grad norm: " << grad.norm() << ", delta x: " << rate * delta_x.norm() << ", delta_f: " << f - fnew << std::endl;
			if (getNormFunc)
			{
				double gradz, gradw;
				getNormFunc(grad, gradz, gradw);

				double updatez, updatew;
				getNormFunc(rate * delta_x, updatez, updatew);
				optInfo << "z grad: " << gradz << ", w grad: " << gradw << ", z change: " << updatez << ", w change: " << updatew << std::endl;
			}
			optInfo << "timing info (in total seconds): " << std::endl;
			optInfo << "assembling took: " << totalAssemblingTime << ", LLT solver took: " << totalSolvingTime << ", line search took: " << totalLineSearchTime << std::endl;

			if (i % 100 == 0)
			{
				std::string fileName = (*savingFolder) + "intermediate.txt";
				std::ofstream ofs(fileName);
				if (ofs)
				{
					ofs << std::setprecision(std::numeric_limits<long double>::digits10 + 1) << x0 << std::endl;
				}
			}
		}

		if (iskeepsame)
		{
			if (std::abs(f - fnew) / f < 1e-5 || rate * delta_x.norm() < 1e-5 || grad.norm() < 1e-4)
			{
				isProj = false;
			}
			if (reg > 1e3)
				isProj = true;
		}


		if (rate < 1e-8)
		{
			std::cout << "terminate with small line search rate (<1e-8): L2-norm = " << grad.norm() << std::endl;
			break;
		}

		if (grad.norm() < gradTol && std::max(std::abs(constVec.minCoeff()), std::abs(constVec.maxCoeff())) < cTol)
		{
			std::cout << "terminate with gradient L2-norm = " << grad.norm() << std::endl;
			break;
		}

		if (rate * delta_x.norm() < xTol)
		{
			std::cout << "terminate with small variable change, gradient L2-norm = " << grad.norm() << std::endl;
			break;
		}
	}
	if (i >= numIter)
		std::cout << "terminate with reaching the maximum iteration, with gradient L2-norm = " << grad.norm() << std::endl;

	totalTimer.stop();
	if (displayInfo)
	{
		std::cout << "total time costed (s): " << totalTimer.elapsed<std::chrono::milliseconds>() * 1e-3 << ", within that, assembling took: " << totalAssemblingTime << ", LLT solver took: " << totalSolvingTime << ", line search took: " << totalLineSearchTime << std::endl;
	}

	if (savingFolder)
	{
		optInfo << "total time costed (s): " << totalTimer.elapsed<std::chrono::milliseconds>() * 1e-3 << ", within that, assembling took: " << totalAssemblingTime << ", LLT solver took: " << totalSolvingTime << ", line search took: " << totalLineSearchTime << std::endl;

		std::string fileName = (*savingFolder) + "final_res.txt";
		std::ofstream ofs(fileName);
		if (ofs)
		{
			ofs << std::setprecision(std::numeric_limits<long double>::digits10 + 1) << x0 << std::endl;
		}


	}

}


