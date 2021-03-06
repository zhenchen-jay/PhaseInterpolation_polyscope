#include "polyscope/polyscope.h"

#include <igl/PI.h>
#include <igl/avg_edge_length.h>
#include <igl/barycenter.h>
#include <igl/boundary_loop.h>
#include <igl/exact_geodesic.h>
#include <igl/gaussian_curvature.h>
#include <igl/invert_diag.h>
#include <igl/lscm.h>
#include <igl/massmatrix.h>
#include <igl/per_vertex_normals.h>
#include <igl/readOBJ.h>
#include <igl/writeOBJ.h>
#include <igl/colormap.h>
#include <igl/doublearea.h>
#include <igl/file_dialog_open.h>
#include <igl/file_dialog_save.h>
#include <igl/boundary_loop.h>
#include <igl/triangle/triangulate.h>
#include <filesystem>
#include "polyscope/messages.h"
#include "polyscope/point_cloud.h"
#include "polyscope/surface_mesh.h"

#include <iostream>
#include <fstream>
#include <unordered_set>
#include <utility>


#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#ifndef GRAIN_SIZE
#define GRAIN_SIZE 10
#endif


#include "../../include/InterpolationScheme/PhaseInterpolation.h"
#include "../../include/InterpolationScheme/PlaneWaveExtraction.h"
#include "../../include/MeshLib/MeshConnectivity.h"
#include "../../include/MeshLib/MeshUpsampling.h"
#include "../../include/Visualization/PaintGeometry.h"
#include "../../include/InterpolationScheme/VecFieldSplit.h"
#include "../../include/Optimization/NewtonDescent.h"
#include "../../include/Optimization/LinearConstrainedSolver.h"
#include "../../include/DynamicInterpolation/GetInterpolatedValues.h"
#include "../../include/DynamicInterpolation/InterpolateKeyFrames.h"
#include "../../include/DynamicInterpolation/TimeIntegratedFrames.h"
#include "../../include/DynamicInterpolation/StrainDrivenModel.h"
#include "../../include/DynamicInterpolation/ComputeZandZdot.h"
#include "../../include/DynamicInterpolation/ZdotIntegration.h"
#include "../../include/IntrinsicFormula/InterpolateZvalsFromEdgeOmega.h"
#include "../../include/IntrinsicFormula/ComputeZdotFromEdgeOmega.h"
#include "../../include/IntrinsicFormula/KnoppelStripePattern.h"
#include "../../include/IntrinsicFormula/IntrinsicKnoppelDrivenFormula.h"
#include "../../include/DynamicInterpolation/TimeIntegratedStrainDrivenModel.h"
#include "../../include/CommonTools.h"

Eigen::MatrixXd triV, upsampledTriV;
Eigen::MatrixXi triF, upsampledTriF;
MeshConnectivity triMesh, upsampledTriMesh;
std::vector<std::pair<int, Eigen::Vector3d>> bary;

Eigen::MatrixXd sourceOmegaFields, tarOmegaFields;
Eigen::MatrixXd theoOmega, tarTheoOmega;


std::vector<std::complex<double>> sourceZvals, sourceTheoZVals, upsampledTheoZVals;
std::vector<std::complex<double>> tarZvals, tarTheoZVals, upsampledTarTheoZVals;
std::vector<Eigen::Vector2cd> sourceTheoGradZvals, tarTheoGradZvals;

std::vector<Eigen::MatrixXd> omegaList;
std::vector<Eigen::MatrixXd> theoOmegaList;
std::vector<std::vector<std::complex<double>>> zList;
std::vector<std::vector<std::complex<double>>> theoZList;

Eigen::MatrixXd planeFields;
Eigen::MatrixXd whirlFields;

Eigen::VectorXd phaseField(0), tarPhaseField(0);
Eigen::VectorXd ampField(0), tarAmpField(0);

std::vector<Eigen::VectorXd> phaseFieldsList;
std::vector<Eigen::VectorXd> ampFieldsList;

std::vector<Eigen::VectorXd> theoPhaseFieldsList;
std::vector<Eigen::VectorXd> theoAmpFieldsList;


Eigen::MatrixXd dataV;
Eigen::MatrixXi dataF;
Eigen::MatrixXd dataVec;
Eigen::MatrixXd curColor;

int loopLevel = 4;
bool isShowWrinkledMesh = true;
bool isShowTheoretical = false;

bool isFixedSource = true;
bool isFixedTar = true;

bool isForceOptimize = false;
bool isTwoTriangles = false;

PhaseInterpolation model;
PaintGeometry mPaint;

int numFrames = 20;
int curFrame = 0;

int singIndSource = 1, singIndSource1 = 1;
int singIndTar = 1, singIndTar1 = 1;
int numWavesSource = 2, numWaveTar = 4;

double globalAmpMax = 1;

double dragSpeed = 0.5;
float ampEnlargeRatio = 1;

double triarea = 0.04;

float vecratio = 0.1;

double sourceCenter1x = 0, sourceCenter1y = 0, sourceCenter2x = 0.8, sourceCenter2y = -0.2, targetCenter1x = 0, targetCenter1y = 0, targetCenter2x = 0.3, targetCenter2y = -0.7;
double sourceDirx = 1.0, sourceDiry = 0, targetDirx = 1, targetDiry = 0;

double sourceShearAngle = 10, targetShearAngle = 45.0;
double sourceShearAmp = 0.01, targetShearAmp = 0.05;

double gradTol = 1e-6;
double xTol = 0;
double fTol = 0;
int numIter = 1000;
int quadOrder = 4;

bool useInertial = false;

double smoothCoef = 1;
double unitNormPenalty = 0;
double fakeThickness = 0.01;


enum FunctionType {
	Whirlpool = 0,
	PlaneWave = 1,
	Summation = 2,
	YShape = 3,
	TwoWhirlPool = 4,
    Shearing = 5,
    ShearingAmpOmega = 6
};

enum IntermediateFrameType {
	BVP = 0,
	IVP = 1
};

enum InitializationType {
	Random = 0,
	Linear = 1,
	Theoretical = 2
};


bool isUseUpMesh = false;

FunctionType functionType = FunctionType::ShearingAmpOmega;
FunctionType tarFunctionType = FunctionType::ShearingAmpOmega;
InitializationType initializationType = InitializationType::Linear;
IntermediateFrameType frameType = IntermediateFrameType::BVP;
KnoppelModelType knoppelType = Z_WTar;
double velMag = 1.0;


void generateSquare(double length, double width, double triarea, Eigen::MatrixXd& irregularV, Eigen::MatrixXi& irregularF)
{
	double area = length * width;
	int N = (0.25 * std::sqrt(area / triarea));
	N = N > 1 ? N : 1;
	double deltaX = length / (4.0 * N);
	double deltaY = width / (4.0 * N);

	Eigen::MatrixXd planeV;
	Eigen::MatrixXi planeE;

	//	planeV.resize(10, 2);
	//	planeE.resize(10, 2);

	//	for (int i = -2; i <= 2; i++)
	//	{
	//		planeV.row(i + 2) << length / 4.0 * i, -width / 2.0;
	//	}
	//
	//	for (int i = 2; i >= -2; i--)
	//	{
	//		planeV.row(5 + 2 - i) << length / 4.0 * i, width / 2.0;
	//	}
	//
	//	for (int i = 0; i < 10; i++)
	//	{
	//		planeE.row(i) << i, (i + 1) % 10;
	//	}

	int M = 2 * N + 1;
	planeV.resize(4 * M - 4, 2);
	planeE.resize(4 * M - 4, 2);

	for (int i = 0; i < M; i++)
	{
		planeV.row(i) << -length / 2, i* width / (M - 1) - width / 2;
	}
	for (int i = 1; i < M; i++)
	{
		planeV.row(M - 1 + i) << i * length / (M - 1) - length / 2, width / 2;
	}
	for (int i = 1; i < M; i++)
	{
		planeV.row(2 * (M - 1) + i) << length / 2, width / 2 - i * width / (M - 1);
	}
	for (int i = 1; i < M - 1; i++)
	{
		planeV.row(3 * (M - 1) + i) << length / 2 - i * length / (M - 1), -width / 2;
	}

	for (int i = 0; i < 4 * (M - 1); i++)
	{
		planeE.row(i) << i, (i + 1) % (4 * (M - 1));
	}

	Eigen::MatrixXd V2d;
	Eigen::MatrixXi F;
	Eigen::MatrixXi H(0, 2);
	std::cout << triarea << std::endl;
	// Create an output string stream
	std::ostringstream streamObj;
	//Add double to stream
	streamObj << triarea;
	const std::string flags = "q20a" + std::to_string(triarea);

	igl::triangle::triangulate(planeV, planeE, H, flags, V2d, F);

	if (isTwoTriangles)
	{
		/*V2d.resize(4, 3);
		V2d << -1, -1, 0,
			1, -1, 0,
			1, 1, 0,
			-1, 1, 0;

		F.resize(2, 3);
		F << 0, 1, 2,
			2, 3, 0;*/

		V2d.resize(3, 3);
		V2d << 0, 0, 0,
			1, 0, 0,
			0, 1, 0;

		F.resize(1, 3);
		F << 0, 1, 2;
	}

	irregularV.resize(V2d.rows(), 3);
	irregularV.setZero();
	irregularV.block(0, 0, irregularV.rows(), 2) = V2d.block(0, 0, irregularV.rows(), 2);
	irregularF = F;
	igl::writeOBJ("irregularPlane.obj", irregularV, irregularF);
}

void generateParallelogramCompression(const Eigen::MatrixXd& pos, double theta, Eigen::MatrixXd& compDir)
{
    theta = theta / 180.0 * M_PI;   // convert degree to radian
    double comp = std::tan(theta) / (4 * std::cos(theta)) * (-std::sqrt(10 + 6 * std::cos(2 * theta)) + 2 * std::sin(theta));
    Eigen::Vector2d dir;
    dir << -1.0 / (4 * std::cos(theta)) * (std::sqrt(10 + 6 * std::cos(2 * theta)) + 2 * std::sin(theta)), 1;

    std::cout << "shearing angle in radian: " << theta << ", compression dir: " << (dir / dir.norm()).transpose() << std::endl;
    compDir.resize(pos.rows(), 2);
    for(int i = 0; i < pos.rows(); i++)
    {
        compDir.row(i) = dir / dir.norm() * comp;
    }
}

void generateParallelogramWZ(const Eigen::MatrixXd &pos, const MeshConnectivity& mesh, double theta, double amp, Eigen::MatrixXd& omega, std::vector<std::complex<double>>& zvals)
{
    Eigen::MatrixXd comp;
    generateParallelogramCompression(pos, theta, comp);
    omega = comp / amp;
    Eigen::MatrixXd fullOmega, edgeOmega;
    fullOmega.setZero(omega.rows(), 3);
    fullOmega.block(0, 0, omega.rows(), 2) = omega;
    edgeOmega = vertexVec2IntrinsicHalfEdgeVec(fullOmega, pos, mesh);

    Eigen::MatrixXd cotEntries;
    Eigen::VectorXd faceArea;
    igl::cotmatrix_entries(pos, mesh.faces(), cotEntries);

    igl::doublearea(pos, mesh.faces(), faceArea);
    IntrinsicFormula::roundVertexZvalsFromHalfEdgeOmega(mesh, edgeOmega, faceArea, cotEntries, pos.rows(), zvals);

    for(auto &z : zvals)
        if(std::abs(z))
            z *= amp / std::abs(z);
}

void shearingSquare(const Eigen::MatrixXd &pos, double theta, Eigen::MatrixXd &shearedPos)
{
    theta = theta / 180.0 * M_PI;   // convert degree to radian
    int nverts = pos.rows();
    shearedPos = pos;
    for(int i = 0; i < nverts; i++)
    {
        double x = pos(i, 0);
        double y = pos(i, 1);
        shearedPos << x + y * std::tan(theta), y, 0;
    }
}


void generateWhirlPool(double centerx, double centery, Eigen::MatrixXd& w, std::vector<std::complex<double>>& z, int pow = 1, std::vector<Eigen::Vector2cd>* gradZ = NULL, std::vector<std::complex<double>>* upsampledZ = NULL)
{
	z.resize(triV.rows());
	w.resize(triV.rows(), 2);
	std::cout << "whirl pool center: " << centerx << ", " << centery << std::endl;
	bool isnegative = false;
	if (pow < 0)
	{
		isnegative = true;
		pow *= -1;
	}

	for (int i = 0; i < z.size(); i++)
	{
		double x = triV(i, 0) - centerx;
		double y = triV(i, 1) - centery;
		double rsquare = x * x + y * y;

		if (isnegative)
		{
			z[i] = std::pow(std::complex<double>(x, -y), pow);

			if (std::abs(std::sqrt(rsquare)) < 1e-10)
				w.row(i) << 0, 0;
			else
				w.row(i) << pow * y / rsquare, -pow * x / rsquare;
		}
		else
		{
			z[i] = std::pow(std::complex<double>(x, y), pow);

			if (std::abs(std::sqrt(rsquare)) < 1e-10)
				w.row(i) << 0, 0;
			else
				//			w.row(i) << -y / rsquare, x / rsquare;
				w.row(i) << -pow * y / rsquare, pow* x / rsquare;
		}
	}

	if (upsampledZ)
	{
		upsampledZ->resize(upsampledTriV.rows());
		for (int i = 0; i < upsampledZ->size(); i++)
		{
			double x = upsampledTriV(i, 0) - centerx;
			double y = upsampledTriV(i, 1) - centery;
			double rsquare = x * x + y * y;

			upsampledZ->at(i) = std::pow(std::complex<double>(x, y), pow);
			if (isnegative)
				upsampledZ->at(i) = std::pow(std::complex<double>(x, -y), pow);
		}
	}
	if (gradZ)
	{
		gradZ->resize(triV.rows());
		for (int i = 0; i < gradZ->size(); i++)
		{
			double x = upsampledTriV(i, 0) - centerx;
			double y = upsampledTriV(i, 1) - centery;

			Eigen::Vector2cd tmpGrad;
			tmpGrad << 1, std::complex<double>(0, 1);
			if (isnegative)
				tmpGrad(1) *= -1;

			(*gradZ)[i] = std::pow(std::complex<double>(x, y), pow - 1) * tmpGrad;
			(*gradZ)[i] *= pow;

		}

	}
}

void generatePlaneWave(Eigen::Vector2d v, Eigen::MatrixXd& w, std::vector<std::complex<double>>& z, std::vector<Eigen::Vector2cd>* gradZ = NULL, std::vector<std::complex<double>>* upsampledZ = NULL)
{
	z.resize(triV.rows());
	w.resize(triV.rows(), 2);
	std::cout << "plane wave direction: " << v.transpose() << std::endl;

	for (int i = 0; i < z.size(); i++)
	{
		double theta = v.dot(triV.row(i).segment<2>(0));
		double x = std::cos(theta);
		double y = std::sin(theta);
		z[i] = std::complex<double>(x, y);
		w.row(i) = v;
	}

	if (upsampledZ)
	{
		upsampledZ->resize(upsampledTriV.rows());
		for (int i = 0; i < upsampledZ->size(); i++)
		{
			double theta = v.dot(upsampledTriV.row(i).segment<2>(0));
			double x = std::cos(theta);
			double y = std::sin(theta);
			upsampledZ->at(i) = std::complex<double>(x, y);
		}
	}
	if (gradZ)
	{
		gradZ->resize(triV.rows());
		for (int i = 0; i < gradZ->size(); i++)
		{
			double theta = v.dot(triV.row(i).segment<2>(0));
			double x = std::cos(theta);
			double y = std::sin(theta);
			std::complex<double> tmpZ = std::complex<double>(x, y);
			std::complex<double> I = std::complex<double>(0, 1);

			(*gradZ)[i] << I * tmpZ * v(0), I* tmpZ* v(1);
		}

	}
}

void generatePeriodicWave(int waveNum, Eigen::MatrixXd& w, std::vector<std::complex<double>>& z, std::vector<Eigen::Vector2cd>* gradZ = NULL, std::vector<std::complex<double>>* upsampledZ = NULL)
{
	Eigen::Vector2d v(2 * M_PI * waveNum, 0);
	generatePlaneWave(v, w, z, gradZ, upsampledZ);
}

void generateTwoWhirlPool(double centerx0, double centery0, double centerx1, double centery1, Eigen::MatrixXd& w, std::vector<std::complex<double>>& z, int n0 = 1, int n1 = 1, std::vector<Eigen::Vector2cd>* gradZ = NULL, std::vector<std::complex<double>>* upsampledZ = NULL)
{
	Eigen::MatrixXd w0, w1;
	std::vector<Eigen::Vector2cd> gradZ0, gradZ1;
	std::vector<std::complex<double>> z0, z1, upsampledZ0, upsampledZ1;

	generateWhirlPool(centerx0, centery0, w0, z0, n0, gradZ ? &gradZ0 : NULL, upsampledZ ? &upsampledZ0 : NULL);
	generateWhirlPool(centerx1, centery1, w1, z1, n1, gradZ ? &gradZ1 : NULL, upsampledZ ? &upsampledZ1 : NULL);

	std::cout << "whirl pool center: " << centerx0 << ", " << centery0 << std::endl;
	std::cout << "whirl pool center: " << centerx1 << ", " << centery1 << std::endl;

	z.resize(triV.rows());
	w.resize(triV.rows(), 2);

	w = w0 + w1;

	for (int i = 0; i < z.size(); i++)
	{
		z[i] = z0[i] * z1[i];
	}

	if (upsampledZ)
	{
		upsampledZ->resize(upsampledTriV.rows());
		for (int i = 0; i < upsampledZ->size(); i++)
		{
			upsampledZ->at(i) = upsampledZ0[i] * upsampledZ1[i];
		}
	}

	if (gradZ)
	{
		*gradZ = gradZ0;
		for (int i = 0; i < gradZ0.size(); i++)
		{
			(*gradZ)[i] = z0[i] * gradZ1[i] + z1[i] * gradZ0[i];
		}
	}
}

void generatePlaneSumWhirl(double centerx, double centery, Eigen::Vector2d v, Eigen::MatrixXd& w, std::vector<std::complex<double>>& z, int pow = 1, std::vector<Eigen::Vector2cd>* gradZ = NULL, std::vector<std::complex<double>>* upsampledZ = NULL)
{
	z.resize(triV.rows());
	w.resize(triV.rows(), 2);
	std::cout << "whirl pool center: " << centerx << ", " << centery << std::endl;
	std::cout << "plane wave direction: " << v.transpose() << std::endl;

	std::vector<Eigen::Vector2cd> gradWZ, gradPZ;
	Eigen::MatrixXd whw, plw;
	std::vector<std::complex<double>> wz, pz, upWz, upPz;

	generatePlaneWave(v, plw, pz, gradZ ? &gradPZ : NULL, upsampledZ ? &upPz : NULL);
	generateWhirlPool(centerx, centery, whw, wz, pow, gradZ ? &gradWZ : NULL, upsampledZ ? &upWz : NULL);



	for (int i = 0; i < z.size(); i++)
	{
		z[i] = pz[i] * wz[i];
		w = plw + whw;
	}

	if (upsampledZ)
	{
		upsampledZ->resize(upsampledTriV.rows());

		for (int i = 0; i < upsampledZ->size(); i++)
		{
			(*upsampledZ)[i] = upPz[i] * upWz[i];
		}
	}

	if (gradZ)
	{
		*gradZ = gradWZ;
		for (int i = 0; i < gradWZ.size(); i++)
		{
			(*gradZ)[i] = pz[i] * gradWZ[i] + wz[i] * gradPZ[i];
		}
	}

}

void generateYshape(Eigen::Vector2d w1, Eigen::Vector2d w2, Eigen::MatrixXd& w, std::vector<std::complex<double>>& z, std::vector<Eigen::Vector2cd>* gradZ = NULL, std::vector<std::complex<double>>* upsampledZ = NULL)
{
	z.resize(triV.rows());
	w.resize(triV.rows(), 2);

	if (gradZ)
		gradZ->resize(triV.rows());

	std::cout << "w1: " << w1.transpose() << std::endl;
	std::cout << "w2: " << w2.transpose() << std::endl;

	Eigen::MatrixXd pw1, pw2;
	std::vector<std::complex<double>> pz1, pz2;
	std::vector<Eigen::Vector2cd> gradPZ1, gradPZ2;
	std::vector<std::complex<double>> upsampledPZ1, upsampledPZ2;

	generatePlaneWave(w1, pw1, pz1, gradZ ? &gradPZ1 : NULL, upsampledZ ? &upsampledPZ1 : NULL);
	generatePlaneWave(w2, pw2, pz2, gradZ ? &gradPZ2 : NULL, upsampledZ ? &upsampledPZ2 : NULL);

	double ymax = triV.col(1).maxCoeff();
	double ymin = triV.col(1).minCoeff();

	for (int i = 0; i < z.size(); i++)
	{

		double weight = (triV(i, 1) - triV.col(1).minCoeff()) / (triV.col(1).maxCoeff() - triV.col(1).minCoeff());
		z[i] = (1 - weight) * pz1[i] + weight * pz2[i];
		Eigen::Vector2cd dz = (1 - weight) * gradPZ1[i] + weight * gradPZ2[i];
		if (gradZ)
			(*gradZ)[i] = dz;

		double wx = 0;
		double wy = 1 / (ymax - ymin);

		w.row(i) = (std::conj(z[i]) * dz).imag() / (std::abs(z[i]) * std::abs(z[i]));
	}

	if (upsampledZ)
	{
		upsampledZ->resize(upsampledTriV.rows());
		for (int i = 0; i < upsampledZ->size(); i++)
		{
			double theta = w1.dot(upsampledTriV.row(i).segment<2>(0));
			double x = std::cos(theta);
			double y = std::sin(theta);
			std::complex<double> z1 = std::complex<double>(x, y);

			theta = w2.dot(upsampledTriV.row(i).segment<2>(0));
			x = std::cos(theta);
			y = std::sin(theta);
			std::complex<double> z2 = std::complex<double>(x, y);

			double weight = (upsampledTriV(i, 1) - upsampledTriV.col(1).minCoeff()) / (upsampledTriV.col(1).maxCoeff() - upsampledTriV.col(1).minCoeff());
			upsampledZ->at(i) = (1 - weight) * z1 + weight * z2;
		}
	}
}

void initialization()
{
	generateSquare(2.0, 2.0, triarea, triV, triF);

	Eigen::SparseMatrix<double> S;
	std::vector<int> facemap;

	meshUpSampling(triV, triF, upsampledTriV, upsampledTriF, loopLevel, &S, &facemap, &bary);
	std::cout << "upsampling finished" << std::endl;

	MeshConnectivity mesh(triF), upsampledMesh(upsampledTriF);

	model = PhaseInterpolation(triV, mesh, upsampledTriV, upsampledMesh, triV, mesh, upsampledTriV, upsampledMesh, &bary);
}



void generateValues(FunctionType funType, Eigen::MatrixXd &vecFields, std::vector<std::complex<double>> &zvalues, std::vector<Eigen::Vector2cd> &gradZvals, std::vector<std::complex<double>> &upZvals, int singularityInd1 = 1, int singularityInd2 = 1, bool isFixedGenerator = false, double fixedx = 0, double fixedy = 0, Eigen::Vector2d fixedv = Eigen::Vector2d::Constant(1.0), double shearingAmp = 0, double shearingAngle = 0)
{
	if (funType == FunctionType::Whirlpool)
	{
		Eigen::Vector2d center = Eigen::Vector2d::Random();
		if (isFixedGenerator)
			center << fixedx, fixedy;
		generateWhirlPool(center(0), center(1), vecFields, zvalues, singularityInd1, &gradZvals, &upZvals);
	}
	else if (funType == FunctionType::PlaneWave)
	{
		Eigen::Vector2d v = Eigen::Vector2d::Random();
		if (isFixedGenerator)
			v = fixedv;
		generatePlaneWave(v, vecFields, zvalues, &gradZvals, &upZvals);
	}
	else if (funType == FunctionType::Summation)
	{
		Eigen::Vector2d center = Eigen::Vector2d::Random();
		Eigen::Vector2d v = Eigen::Vector2d::Random();
		if (isFixedGenerator)
		{
			v = fixedv;
			center << fixedx, fixedy;
		}
		generatePlaneSumWhirl(center(0), center(1), v, vecFields, zvalues, singularityInd1, &gradZvals, &upZvals);
	}
	else if (funType == FunctionType::YShape)
	{
		Eigen::Vector2d w1(1, 0);
		Eigen::Vector2d w2(1, 0);

		w1(0) = 2 * 3.1415926;
		w2(0) = 4 * 3.1415926;
		generateYshape(w1, w2, vecFields, zvalues, &gradZvals, &upZvals);
	}
	else if (funType == FunctionType::TwoWhirlPool)
	{
		Eigen::Vector2d center0 = Eigen::Vector2d::Random();
		Eigen::Vector2d center1 = Eigen::Vector2d::Random();
		if (isFixedGenerator)
		{
			center0 << fixedx, fixedy;
			center1 << 0.8, -0.3;
		}
		generateTwoWhirlPool(center0(0), center0(1), center1(0), center1(1), vecFields, zvalues, singularityInd1, singularityInd2, &gradZvals, &upZvals);
	}
    else if (funType == FunctionType::Shearing || funType == FunctionType::ShearingAmpOmega)
    {
        generateParallelogramWZ(triV, MeshConnectivity(triF), shearingAngle, shearingAmp, vecFields, zvalues);
        // do nothing for theoretical values

        upZvals.resize(upsampledTriV.rows());
        for (int i = 0; i < upZvals.size(); i++)
        {
            upZvals[i] = std::complex<double>(0, 0);
        }
        gradZvals.resize(triV.rows());
        for (int i = 0; i < gradZvals.size(); i++)
        {
            Eigen::Vector2cd tmpGrad;
            tmpGrad.setZero();
            gradZvals[i] = std::complex<double>(0, 0) * tmpGrad;

        }

    }
}

void solveKeyFrames(const Eigen::MatrixXd& sourceVec, const Eigen::MatrixXd& tarVec, const std::vector<std::complex<double>>& sourceZvals, const std::vector<std::complex<double>>& tarZvals, const int numKeyFrames, std::vector<Eigen::MatrixXd>& wFrames, std::vector<std::vector<std::complex<double>>>& zFrames)
{
	//ComputeZandZdot zdotModel = ComputeZandZdot(triV, triF, 6);
	//zdotModel.testPlaneWaveValueDotFromQuad(sourceVec, tarVec, sourceZvals, tarZvals, 1, 0, 2);
	//zdotModel.testZDotSquarePerface(sourceVec, tarVec, sourceZvals, tarZvals, 1, 0);
	//zdotModel.testZDotSquareIntegration(sourceVec, tarVec, sourceZvals, tarZvals, 1);
	if (frameType == IntermediateFrameType::BVP)
    {
        if((tarFunctionType == Shearing && functionType == Shearing) || (functionType == ShearingAmpOmega && tarFunctionType == ShearingAmpOmega))
        {
            std::vector<Eigen::MatrixXd> wTarDir(numKeyFrames + 2);
            std::vector<Eigen::VectorXd> aTar(numKeyFrames + 2);

            for(int i = 0; i < numKeyFrames + 2; i++)
            {
                double angle = sourceShearAngle + (targetShearAngle - sourceShearAngle) / (numKeyFrames + 1) * i;
                double amp = sourceShearAmp + (targetShearAmp - sourceShearAmp) / (numKeyFrames + 1) * i;

                std::vector<std::complex<double>> zvals;

                generateParallelogramWZ(triV, MeshConnectivity(triF), angle, amp, wTarDir[i], zvals);
                aTar[i].setConstant(triV.rows(), amp);
            }

            int mFlag = (functionType == Shearing) ? 0 : 1;

            StrainDrivenModel interpModel = StrainDrivenModel(wTarDir, aTar, triV, triF, sourceVec, tarVec, sourceZvals, tarZvals, numKeyFrames, quadOrder, smoothCoef, fakeThickness, mFlag);

            Eigen::VectorXd x;
            interpModel.convertList2Variable(x);        // linear initialization
            //		std::cout << "starting energy: " << interpModel.computeEnergy(x) << std::endl;
            if (initializationType == InitializationType::Theoretical)
            {
                for(int i = 0; i < numKeyFrames + 2; i++)
                {
                    double angle = sourceShearAngle + (targetShearAngle - sourceShearAngle) / (numKeyFrames + 1) * i;
                    double amp = sourceShearAmp + (targetShearAmp - sourceShearAmp) / (numKeyFrames + 1) * i;

                    std::vector<std::complex<double>> zvals;

                    generateParallelogramWZ(triV, MeshConnectivity(triF), angle, amp, interpModel._wList[i], interpModel._vertValsList[i]);
                    aTar[i].setConstant(triV.rows(), amp);
                }
                interpModel.convertList2Variable(x);
                for(int i = 0; i < numKeyFrames + 2; i++) {
                    std::cout << "potential: " << interpModel.computeSpatialEnergyPerFrame(i) << std::endl;
                }
            }
            else if (initializationType == InitializationType::Random)
            {
                x.setRandom();
                interpModel.convertVariable2List(x);
                interpModel.convertList2Variable(x);
            }
            else
            {
                // do nothing, since it is initialized as the linear interpolation.
            }

            auto initWFrames = interpModel.getWList();
            auto initZFrames = interpModel.getVertValsList();
            if (isForceOptimize) {
                auto funVal = [&](const Eigen::VectorXd &x, Eigen::VectorXd *grad, Eigen::SparseMatrix<double> *hess,
                                  bool isProj) {
                    Eigen::VectorXd deriv;
                    Eigen::SparseMatrix<double> H;
                    double E = interpModel.computeEnergy(x, grad ? &deriv : NULL, hess ? &H : NULL, isProj);

                    if (grad) {
                        (*grad) = deriv;
                    }

                    if (hess) {
                        (*hess) = H;
                    }

                    return E;
                };
                auto maxStep = [&](const Eigen::VectorXd &x, const Eigen::VectorXd &dir) {
                    return 1.0;
                };

                auto getVecNorm = [&](const Eigen::VectorXd &x, double &znorm, double &wnorm) {
                    interpModel.getComponentNorm(x, znorm, wnorm);
                };
                OptSolver::testFuncGradHessian(funVal, x);

                auto x0 = x;
                OptSolver::newtonSolver(funVal, maxStep, x, numIter, gradTol, xTol, fTol, true, getVecNorm);
                std::cout << "x norm, before optimization: " << x0.norm() << ", after optimization: " << x.norm()
                          << ", difference: " << (x - x0).norm() << std::endl;
                std::cout << "energy, before optimizaiton: " << interpModel.computeEnergy(x0) << ", after optimization: " << interpModel.computeEnergy(x) << std::endl;
            }
            interpModel.convertVariable2List(x);

            wFrames = interpModel.getWList();
            zFrames = interpModel.getVertValsList();

            for (int i = 0; i < wFrames.size() - 1; i++)
            {
                double zdotNorm = interpModel._model.zDotSquareIntegration(wFrames[i], wFrames[i + 1], zFrames[i], zFrames[i + 1], 1.0 / (wFrames.size() - 1), NULL, NULL);
                double initZdotNorm = interpModel._model.zDotSquareIntegration(initWFrames[i], initWFrames[i + 1], initZFrames[i], initZFrames[i + 1], 1.0 / (wFrames.size() - 1), NULL, NULL);

                std::cout << "frame " << i << ", before optimization: ||zdot||^2: " << initZdotNorm << ", after optimization, ||zdot||^2 = " << zdotNorm << std::endl;
            }
        }
        else
        {
            InterpolateKeyFrames interpModel = InterpolateKeyFrames(triV, triF, sourceVec, tarVec, sourceZvals, tarZvals, numKeyFrames, quadOrder, smoothCoef, knoppelType, unitNormPenalty);
            Eigen::VectorXd x;
            interpModel.convertList2Variable(x);        // linear initialization
            //		std::cout << "starting energy: " << interpModel.computeEnergy(x) << std::endl;
            if (initializationType == InitializationType::Theoretical)
            {
                interpModel._wList = theoOmegaList;
                interpModel._vertValsList = theoZList;
                interpModel.convertList2Variable(x);
            }
            else if (initializationType == InitializationType::Random)
            {
                x.setRandom();
                interpModel.convertVariable2List(x);
                interpModel.convertList2Variable(x);
            }
            else
            {
                // do nothing, since it is initialized as the linear interpolation.
            }

            auto initWFrames = interpModel.getWList();
            auto initZFrames = interpModel.getVertValsList();
            if (isForceOptimize) {
                auto funVal = [&](const Eigen::VectorXd &x, Eigen::VectorXd *grad, Eigen::SparseMatrix<double> *hess,
                                  bool isProj) {
                    Eigen::VectorXd deriv;
                    Eigen::SparseMatrix<double> H;
                    double E = interpModel.computeEnergy(x, grad ? &deriv : NULL, hess ? &H : NULL, isProj);

                    if (grad) {
                        (*grad) = deriv;
                    }

                    if (hess) {
                        (*hess) = H;
                    }

                    return E;
                };
                auto maxStep = [&](const Eigen::VectorXd &x, const Eigen::VectorXd &dir) {
                    return 1.0;
                };

                auto getVecNorm = [&](const Eigen::VectorXd &x, double &znorm, double &wnorm) {
                    interpModel.getComponentNorm(x, znorm, wnorm);
                };
                OptSolver::testFuncGradHessian(funVal, x);

                auto x0 = x;
                OptSolver::newtonSolver(funVal, maxStep, x, numIter, gradTol, xTol, fTol, true, getVecNorm);
                std::cout << "x norm, before optimization: " << x0.norm() << ", after optimization: " << x.norm()
                          << ", difference: " << (x - x0).norm() << std::endl;
                std::cout << "energy, before optimizaiton: " << interpModel.computeEnergy(x0) << ", after optimization: " << interpModel.computeEnergy(x) << std::endl;
            }
            interpModel.convertVariable2List(x);

            wFrames = interpModel.getWList();
            zFrames = interpModel.getVertValsList();

            for (int i = 0; i < wFrames.size() - 1; i++)
            {
                double zdotNorm = interpModel._model.zDotSquareIntegration(wFrames[i], wFrames[i + 1], zFrames[i], zFrames[i + 1], 1.0 / (wFrames.size() - 1), NULL, NULL);
                double initZdotNorm = interpModel._model.zDotSquareIntegration(initWFrames[i], initWFrames[i + 1], initZFrames[i], initZFrames[i + 1], 1.0 / (wFrames.size() - 1), NULL, NULL);

                std::cout << "frame " << i << ", before optimization: ||zdot||^2: " << initZdotNorm << ", after optimization, ||zdot||^2 = " << zdotNorm << std::endl;
            }
        }



	}
	else if (frameType == IntermediateFrameType::IVP)
	{
        if((tarFunctionType == Shearing && functionType == Shearing) || (functionType == ShearingAmpOmega && tarFunctionType == ShearingAmpOmega))
        {
            std::vector<Eigen::MatrixXd> wTarDir(numKeyFrames + 2);
            std::vector<Eigen::VectorXd> aTar(numKeyFrames + 2);

            for(int i = 0; i < numKeyFrames + 2; i++)
            {
                double angle = sourceShearAngle + (targetShearAngle - sourceShearAngle) / (numKeyFrames + 1) * i;
                double amp = sourceShearAmp + (targetShearAmp - sourceShearAmp) / (numKeyFrames + 1) * i;

                std::vector<std::complex<double>> zvals;
                generateParallelogramWZ(triV, MeshConnectivity(triF), angle, amp, wTarDir[i], zvals);
                aTar[i].setConstant(triV.rows(), amp);
            }

            int mFlag = (functionType == Shearing) ? 0 : 1;

            TimeIntegratedStrainDrivenModel frameModel = TimeIntegratedStrainDrivenModel(wTarDir, aTar, triV, triF, sourceVec, sourceZvals, numKeyFrames, quadOrder, smoothCoef, fakeThickness, mFlag);
            frameModel.solveInterpFrames();

            wFrames = frameModel.getWList();
            zFrames = frameModel.getVertValsList();
        }
        else
        {
            TimeIntegratedFrames frameModel = TimeIntegratedFrames(triV, triF, sourceVec, tarVec, sourceZvals, tarZvals, numKeyFrames, smoothCoef, knoppelType, unitNormPenalty, useInertial, velMag);
            frameModel.solveInterpFrames();

            wFrames = frameModel.getWList();
            zFrames = frameModel.getVertValsList();
        }

	}

}

void updateMagnitudePhase(const std::vector<Eigen::MatrixXd>& wFrames, const std::vector<std::vector<std::complex<double>>>& zFrames, std::vector<Eigen::VectorXd>& magList, std::vector<Eigen::VectorXd>& phaseList)
{
	GetInterpolatedValues interpModel = GetInterpolatedValues(triV, triF, upsampledTriV, upsampledTriF, bary);

	std::vector<std::vector<std::complex<double>>> interpZList(wFrames.size());
	magList.resize(wFrames.size());
	phaseList.resize(wFrames.size());

	auto computeMagPhase = [&](const tbb::blocked_range<uint32_t>& range) {
		for (uint32_t i = range.begin(); i < range.end(); ++i)
		{
			interpZList[i] = interpModel.getZValues(wFrames[i], zFrames[i], NULL, NULL);
			/*Eigen::MatrixXd upsampledW;
			Eigen::MatrixXd NV;
			Eigen::MatrixXi NF;
			upsampleMeshZvals(triV, triF, wFrames[i], zFrames[i], NV, NF, upsampledW, interpZList[i], loopLevel);*/
			magList[i].setZero(interpZList[i].size());
			phaseList[i].setZero(interpZList[i].size());

			for (int j = 0; j < magList[i].size(); j++)
			{
				magList[i](j) = std::abs(interpZList[i][j]);
				phaseList[i](j) = std::arg(interpZList[i][j]);
			}
		}
	};

	tbb::blocked_range<uint32_t> rangex(0u, (uint32_t)interpZList.size(), GRAIN_SIZE);
	tbb::parallel_for(rangex, computeMagPhase);

	/*Eigen::MatrixXd upsampledW;
	upsampleMeshZvals(triV, triF, wFrames[0], zFrames[0], upsampledTriV, upsampledTriF, upsampledW, interpZList[0], loopLevel);*/

	/*for (int i = 0; i < interpZList.size(); i++)
	{
		Eigen::MatrixXd upsampledW;
		upsampleMeshZvals(triV, triF, wFrames[i], zFrames[i], upsampledTriV, upsampledTriF, upsampledW, interpZList[i], loopLevel);
		magList[i].setZero(interpZList[i].size());
		phaseList[i].setZero(interpZList[i].size());

		for (int j = 0; j < magList[i].size(); j++)
		{
			magList[i](j) = std::abs(interpZList[i][j]);
			phaseList[i](j) = std::arg(interpZList[i][j]);
		}
	}*/

}

void updateTheoMagnitudePhase(const std::vector<std::complex<double>>& sourceZvals, const std::vector<std::complex<double>>& tarZvals,
	const std::vector<Eigen::Vector2cd>& sourceGradZvals, const std::vector<Eigen::Vector2cd>& tarGradZvals,
	const std::vector<std::complex<double>>& upSourceZvals, const std::vector<std::complex<double>>& upTarZvals,
	const int num, std::vector<Eigen::VectorXd>& magList, std::vector<Eigen::VectorXd>& phaseList,    // upsampled information
	std::vector<Eigen::MatrixXd>& wList, std::vector<std::vector<std::complex<double>>>& zvalList        // raw information
)
{
	magList.resize(num + 2);
	phaseList.resize(num + 2);
	wList.resize(num + 2);
	zvalList.resize(num + 2);

	double dt = 1.0 / (num + 1);

	auto computeMagPhase = [&](const tbb::blocked_range<uint32_t>& range) {
		for (uint32_t i = range.begin(); i < range.end(); ++i)
		{
			double w = i * dt;
			magList[i].setZero(upSourceZvals.size());
			phaseList[i].setZero(upSourceZvals.size());
			for (int j = 0; j < upSourceZvals.size(); j++)
			{
				std::complex<double> z = (1 - w) * upSourceZvals[j] + w * upTarZvals[j];
				magList[i][j] = std::abs(z);
				phaseList[i][j] = std::arg(z);
			}

			wList[i].setZero(sourceZvals.size(), 2);
			zvalList[i].resize(sourceZvals.size());

			for (int j = 0; j < sourceZvals.size(); j++)
			{
				Eigen::Vector2cd gradf = (1 - w) * sourceGradZvals[j] + w * tarGradZvals[j];
				std::complex<double> fbar = (1 - w) * std::conj(sourceZvals[j]) + w * std::conj(tarZvals[j]);

				wList[i].row(j) = ((gradf * fbar) / (std::abs(fbar) * std::abs(fbar))).imag();
				zvalList[i][j] = (1 - w) * sourceZvals[j] + w * tarZvals[j];
			}
		}
	};

	tbb::blocked_range<uint32_t> rangex(0u, (uint32_t)(num + 2), GRAIN_SIZE);
	tbb::parallel_for(rangex, computeMagPhase);

	//	for (int i = 0; i < num + 2; i++)
	//	{
	//	    double w = i * dt;
	//	    magList[i].setZero(upSourceZvals.size());
	//	    phaseList[i].setZero(upSourceZvals.size());
	//	    for (int j = 0; j < upSourceZvals.size(); j++)
	//	    {
	//	        std::complex<double> z = (1 - w) * upSourceZvals[j] + w * upTarZvals[j];
	//	        magList[i][j] = std::abs(z);
	//	        phaseList[i][j] = std::arg(z);
	//	    }
	//
	//	    wList[i].setZero(sourceZvals.size(), 2);
	//	    zvalList[i].resize(sourceZvals.size());
	//
	//	    for(int j = 0; j < sourceZvals.size(); j++)
	//	    {
	//	        Eigen::Vector2cd gradf = (1 - w) * sourceGradZvals[j] + w * tarGradZvals[j];
	//	        std::complex<double> fbar = (1 - w) * std::conj(sourceZvals[j]) + w * std::conj(tarZvals[j]);
	//
	//	        wList[i].row(j) = ((gradf * fbar) / (std::abs(fbar) * std::abs(fbar))).imag();
	//	        zvalList[i][j] = (1 - w) * sourceZvals[j] + w * tarZvals[j];
	//	    }
	//	}
}

void registerMeshByPart(const Eigen::MatrixXd& basePos, const Eigen::MatrixXi& baseF,
	const Eigen::MatrixXd& upPos, const Eigen::MatrixXi& upF, const double& shifty, const double& ampMax,
	const Eigen::MatrixXd& vec, Eigen::VectorXd ampVec, const Eigen::VectorXd& phaseVec,
	Eigen::VectorXd theoAmpVec, const Eigen::VectorXd& theoPhaseVec,
	Eigen::MatrixXd& renderV, Eigen::MatrixXi& renderF, Eigen::MatrixXd& renderVec, Eigen::MatrixXd& renderColor)
{
	int nverts = basePos.rows();
	int nfaces = baseF.rows();

	int nupverts = upPos.rows();
	int nupfaces = upF.rows();

	int ndataVerts = nverts + 2 * nupverts;
	int ndataFaces = nfaces + 2 * nupfaces;

    if(isShowTheoretical)
    {
        ndataVerts += 2 * nupverts;
        ndataFaces += 2 * nupfaces;
    }
    if(isShowWrinkledMesh)
    {
        ndataVerts += nupverts;
        ndataFaces += nupfaces;
    }

	renderV.resize(ndataVerts, 3);
	renderF.resize(ndataFaces, 3);
	renderColor.setZero(ndataVerts, 3);
	renderVec.setZero(ndataVerts, 3);

	renderColor.col(0).setConstant(1.0);
	renderColor.col(1).setConstant(1.0);
	renderColor.col(2).setConstant(1.0);

	int curVerts = 0;
	int curFaces = 0;

	Eigen::VectorXd normalizedAmp = ampVec / ampMax, normalizedTheoAmp = theoAmpVec / ampMax;


	Eigen::MatrixXd shiftV = basePos;
	shiftV.col(0).setConstant(0);
	shiftV.col(1).setConstant(shifty);
	shiftV.col(2).setConstant(0);

	renderV.block(0, 0, nverts, 3) = basePos - shiftV;
	renderF.block(0, 0, nfaces, 3) = baseF;
	for (int i = 0; i < nverts; i++)
		renderVec.row(i) << vec(i, 0), vec(i, 1), 0;
	curVerts += nverts;
	curFaces += nfaces;

	double shiftx = 1.5 * (basePos.col(0).maxCoeff() - basePos.col(0).minCoeff());

	shiftV = upPos;
	shiftV.col(0).setConstant(shiftx);
	shiftV.col(1).setConstant(shifty);
	shiftV.col(2).setConstant(0);


	Eigen::MatrixXi shiftF = upF;
	shiftF.setConstant(curVerts);

	// interpolated phase
	renderV.block(curVerts, 0, nupverts, 3) = upPos - shiftV;
	renderF.block(curFaces, 0, nupfaces, 3) = upF + shiftF;

	mPaint.setNormalization(false);
	Eigen::MatrixXd phiColor = mPaint.paintPhi(phaseVec);
	renderColor.block(curVerts, 0, nupverts, 3) = phiColor;

	curVerts += nupverts;
	curFaces += nupfaces;

	// interpolated amp
	shiftF.setConstant(curVerts);
	shiftV.col(0).setConstant(2 * shiftx);
	renderV.block(curVerts, 0, nupverts, 3) = upPos - shiftV;
	renderF.block(curFaces, 0, nupfaces, 3) = upF + shiftF;

	mPaint.setNormalization(false);
	Eigen::MatrixXd ampColor = mPaint.paintAmplitude(ampVec / globalAmpMax);
	renderColor.block(curVerts, 0, nupverts, 3) = ampColor;

	curVerts += nupverts;
	curFaces += nupfaces;

    if(isShowWrinkledMesh)
    {
        // wrinkled mesh
        shiftF.setConstant(curVerts);
        shiftV.col(0).setConstant(-shiftx);
        Eigen::MatrixXd wrinkledPos = upPos;
        for(int i = 0; i < upPos.rows(); i++)
        {
            wrinkledPos(i, 2) += cos(phaseVec(i)) * ampVec(i) * ampEnlargeRatio;
        }
        renderV.block(curVerts, 0, nupverts, 3) = wrinkledPos - shiftV;
        renderF.block(curFaces, 0, nupfaces, 3) = upF + shiftF;

        Eigen::RowVector3d color;
        igl::colormap(igl::COLOR_MAP_TYPE_VIRIDIS, 2.0 / 9.0, color.data());
        Eigen::MatrixXd wrinkledColor = wrinkledPos;
        wrinkledColor.col(0).setConstant(color(0));
        wrinkledColor.col(1).setConstant(color(1));
        wrinkledColor.col(2).setConstant(color(2));
        renderColor.block(curVerts, 0, nupverts, 3) = wrinkledColor;
    }

    if(isShowTheoretical)
    {
        // theoretical phase
        shiftF.setConstant(curVerts);
        shiftV.col(0).setConstant(3 * shiftx);
        renderV.block(curVerts, 0, nupverts, 3) = upPos - shiftV;
        renderF.block(curFaces, 0, nupfaces, 3) = upF + shiftF;

        mPaint.setNormalization(false);
        phiColor = mPaint.paintPhi(theoPhaseVec);
        renderColor.block(curVerts, 0, nupverts, 3) = phiColor;

        curVerts += nupverts;
        curFaces += nupfaces;

        // theoretical amp
        shiftF.setConstant(curVerts);
        shiftV.col(0).setConstant(4 * shiftx);
        renderV.block(curVerts, 0, nupverts, 3) = upPos - shiftV;
        renderF.block(curFaces, 0, nupfaces, 3) = upF + shiftF;

        mPaint.setNormalization(false);
        ampColor = mPaint.paintAmplitude(theoAmpVec / globalAmpMax);
        renderColor.block(curVerts, 0, nupverts, 3) = ampColor;
    }

}

void registerMesh(int frameId)
{
	Eigen::MatrixXd sourceP, tarP, interpP;
	Eigen::MatrixXi sourceF, tarF, interpF;

	Eigen::MatrixXd sourceVec, tarVec, interpVec;
	Eigen::MatrixXd sourceColor, tarColor, interpColor;
	double shiftx = 1.5 * (triV.col(0).maxCoeff() - triV.col(0).minCoeff());
	double shifty = 1.5 * (triV.col(1).maxCoeff() - triV.col(1).minCoeff());
	int totalfames = ampFieldsList.size();
	registerMeshByPart(triV, triF, upsampledTriV, upsampledTriF, 0, globalAmpMax, omegaList[0], ampFieldsList[0], phaseFieldsList[0], theoAmpFieldsList[0], theoPhaseFieldsList[0], sourceP, sourceF, sourceVec, sourceColor);
	registerMeshByPart(triV, triF, upsampledTriV, upsampledTriF, shifty, globalAmpMax, omegaList[totalfames - 1], ampFieldsList[totalfames - 1], phaseFieldsList[totalfames - 1], theoAmpFieldsList[totalfames - 1], theoPhaseFieldsList[totalfames - 1], tarP, tarF, tarVec, tarColor);
	registerMeshByPart(triV, triF, upsampledTriV, upsampledTriF, 2 * shifty, globalAmpMax, omegaList[frameId], ampFieldsList[frameId], phaseFieldsList[frameId], theoAmpFieldsList[frameId], theoPhaseFieldsList[frameId], interpP, interpF, interpVec, interpColor);

	Eigen::MatrixXi shifF = sourceF;

	int nPartVerts = sourceP.rows();
	int nPartFaces = sourceF.rows();

	dataV.setZero(3 * nPartVerts, 3);
	dataVec.setZero(3 * nPartVerts, 3);
	curColor.setZero(3 * nPartVerts, 3);
	dataF.setZero(3 * nPartFaces, 3);

	shifF.setConstant(nPartVerts);

	dataV.block(0, 0, nPartVerts, 3) = sourceP;
	dataVec.block(0, 0, nPartVerts, 3) = sourceVec;
	curColor.block(0, 0, nPartVerts, 3) = sourceColor;
	dataF.block(0, 0, nPartFaces, 3) = sourceF;

	dataV.block(nPartVerts, 0, nPartVerts, 3) = tarP;
	dataVec.block(nPartVerts, 0, nPartVerts, 3) = tarVec;
	curColor.block(nPartVerts, 0, nPartVerts, 3) = tarColor;
	dataF.block(nPartFaces, 0, nPartFaces, 3) = tarF + shifF;

	dataV.block(nPartVerts * 2, 0, nPartVerts, 3) = interpP;
	dataVec.block(nPartVerts * 2, 0, nPartVerts, 3) = interpVec;
	curColor.block(nPartVerts * 2, 0, nPartVerts, 3) = interpColor;
	dataF.block(nPartFaces * 2, 0, nPartFaces, 3) = interpF + 2 * shifF;

	polyscope::registerSurfaceMesh("input mesh", dataV, dataF);

}

void updateFieldsInView(int frameId)
{
	registerMesh(frameId);
	polyscope::getSurfaceMesh("input mesh")->addVertexColorQuantity("VertexColor", curColor);
	polyscope::getSurfaceMesh("input mesh")->getQuantity("VertexColor")->setEnabled(true);

	polyscope::getSurfaceMesh("input mesh")->addVertexVectorQuantity("vertex vector field", dataVec * vecratio, polyscope::VectorType::AMBIENT);
	polyscope::getSurfaceMesh("input mesh")->getQuantity("vertex vector field")->setEnabled(true);
}


void callback() {
    ImGui::PushItemWidth(100);
    if (ImGui::Button("Reset", ImVec2(-1, 0))) {
        curFrame = 0;
        updateFieldsInView(curFrame);
    }

    if (ImGui::InputDouble("triangle area", &triarea)) {
        if (triarea > 0)
            initialization();
    }
    if (ImGui::Checkbox("Two Triangle Mesh", &isTwoTriangles)) {
        initialization();
    }
    if (ImGui::InputInt("upsampled times", &loopLevel)) {
        if (loopLevel >= 0)
            initialization();
    }

    if (ImGui::CollapsingHeader("source Vector Fields Info", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Combo("source vec types", (int *) &functionType,
                         "Whirl pool\0plane wave\0sum\0Y shape\0Two Whirl Pool\0Shearing\0ShearingAmpOmega\0\0")) {}
        if (ImGui::Checkbox("Fixed source center and dir", &isFixedSource)) {}

        if (ImGui::CollapsingHeader("source whirl  pool Info"))
        {
            if (ImGui::InputInt("source singularity index 1", &singIndSource)){}
            if (ImGui::InputDouble("source center 1 x: ", &sourceCenter1x)) {}
            ImGui::SameLine();
            if (ImGui::InputDouble("source center 1 y: ", &sourceCenter1y)) {}

            if (ImGui::InputInt("source singularity index 2", &singIndSource1)){}
            if (ImGui::InputDouble("source center 2 x: ", &sourceCenter2x)) {}
            ImGui::SameLine();
            if (ImGui::InputDouble("source center 2 y: ", &sourceCenter2y)) {}
        }

        if (ImGui::CollapsingHeader("source plane wave Info"))
        {
            if (ImGui::InputInt("source num waves", &numWavesSource)){}
            if (ImGui::InputDouble("source dir x: ", &sourceDirx)) {}
            ImGui::SameLine();
            if (ImGui::InputDouble("source dir y: ", &sourceDiry)) {}
        }

        if (ImGui::CollapsingHeader("source shearing Info"))
        {
            if (ImGui::InputDouble("source shearing angle", &sourceShearAngle)){}
            if (ImGui::InputDouble("source shearing amp: ", &sourceShearAmp)) {}
        }

    }
    if (ImGui::CollapsingHeader("target Vector Fields Info", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Combo("target vec types", (int *) &tarFunctionType,
                         "Whirl pool\0plane wave\0sum\0Y shape\0Two Whirl Pool\0Shearing\0ShearingAmpOmega\0\0")) {}

        if (ImGui::Checkbox("Fixed target center and dir", &isFixedTar)) {}
        if (ImGui::CollapsingHeader("target whirl  pool Info"))
        {
            if (ImGui::InputInt("target singularity index 1", &singIndTar)) {}
            if (ImGui::InputDouble("target center 1 x: ", &targetCenter1x)) {}
            ImGui::SameLine();
            if (ImGui::InputDouble("target center 1 y: ", &targetCenter1y)) {}

            if (ImGui::InputInt("target singularity index 2", &singIndTar1)) {}
            if (ImGui::InputDouble("target center 2 x: ", &targetCenter2x)) {}
            ImGui::SameLine();
            if (ImGui::InputDouble("target center 2 y: ", &targetCenter2y)) {}
        }

        if (ImGui::CollapsingHeader("target plane wave Info"))
        {
            if (ImGui::InputInt("target num waves", &numWaveTar)){}
            if (ImGui::InputDouble("target dir x: ", &targetDirx)) {}
            ImGui::SameLine();
            if (ImGui::InputDouble("target dir y: ", &targetDiry)) {}
        }

        if (ImGui::CollapsingHeader("target shearing Info"))
        {
            if (ImGui::InputDouble("target shearing angle", &targetShearAngle)){}
            if (ImGui::InputDouble("target shearing amp: ", &targetShearAmp)) {}
        }

    }

    if (ImGui::InputInt("num of frames", &numFrames)) {
        if (numFrames <= 0)
            numFrames = 10;
    }

    if (ImGui::InputDouble("drag speed", &dragSpeed)) {
        if (dragSpeed <= 0)
            dragSpeed = 0.5;
    }

    if (ImGui::DragInt("current frame", &curFrame, dragSpeed, 0, numFrames + 1)) {
        updateFieldsInView(curFrame);
    }
    if (ImGui::CollapsingHeader("Visualization Info", ImGuiTreeNodeFlags_DefaultOpen)) {
        if(ImGui::Checkbox("Show WrinkledMesh", &isShowWrinkledMesh))
            updateFieldsInView(curFrame);
        if(ImGui::Checkbox("Show Theoretical", &isShowTheoretical))
            updateFieldsInView(curFrame);
        if (ImGui::DragFloat("vec ratio", &(vecratio), 0.001, 0, 1))
            updateFieldsInView(curFrame);
        if (ImGui::DragFloat("amp ratio", &(ampEnlargeRatio), 0.05, 0, 100))
            updateFieldsInView(curFrame);

    }


    if (ImGui::Combo("frame types", (int *) &frameType, "BVP\0IVP\0\0")) {}
    if (ImGui::Combo("initialization types", (int *) &initializationType, "Random\0Linear\0Theoretical\0")) {}

    ImGui::Checkbox("Try Optimization", &isForceOptimize);

    if (ImGui::CollapsingHeader("model parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::InputDouble("spatial smoothness coef", &smoothCoef)) {
            if (smoothCoef < 0)
                smoothCoef = 0;
        }
        if (ImGui::InputDouble("unit norm penalty coef", &unitNormPenalty)) {
            if (unitNormPenalty < 0)
                unitNormPenalty = 0;
        }
        if (ImGui::InputDouble("fake thickness", &fakeThickness)) {
            if (fakeThickness < 0)
                fakeThickness = 0.01;
        }
    }


    if (ImGui::CollapsingHeader("optimzation parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::InputInt("num iterations", &numIter)) {
            if (numIter < 0)
                numIter = 1000;
        }
        if (ImGui::InputDouble("grad tol", &gradTol)) {
            if (gradTol < 0)
                gradTol = 1e-6;
        }
        if (ImGui::InputDouble("x tol", &xTol)) {
            if (xTol < 0)
                xTol = 0;
        }
        if (ImGui::InputDouble("f tol", &fTol)) {
            if (fTol < 0)
                fTol = 0;
        }
        if (ImGui::InputInt("quad order", &quadOrder)) {
            if (quadOrder <= 0 || quadOrder > 20)
                quadOrder = 4;
        }
    }


    if (ImGui::CollapsingHeader("dynamic parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("use inertial", &useInertial);
        if (ImGui::Combo("Knoppel potential type", (int *) &knoppelType, "w-wtar\0z-wtar\0wz-wtar\0z-w\0")) {}
        if (ImGui::InputDouble("vel mag", &velMag)) {
            if (velMag < 0)
                velMag = 1.0;
        }
    }


    if (ImGui::Button("update values", ImVec2(-1, 0))) {
        double fixedx = 0;
        double fixedy = 0;
        double ampForShearing = 0;
        double angleForShearing = 0;
        Eigen::Vector2d fixedv(1, 0);
        // source vector fields
        if(isFixedSource)
        {
            fixedx = sourceCenter1x;
            fixedy = sourceCenter1y;
            fixedv << sourceDirx, sourceDiry;
            fixedv *= numWavesSource * 2 * M_PI;

        }
        ampForShearing = sourceShearAmp;
        angleForShearing = sourceShearAngle;
        generateValues(functionType, sourceOmegaFields, sourceZvals, sourceTheoGradZvals, upsampledTheoZVals, singIndSource, singIndSource1, isFixedSource, fixedx, fixedy, fixedv, ampForShearing, angleForShearing);

        if(isFixedTar)
        {
            fixedx = targetCenter1x;
            fixedy = targetCenter1y;
            fixedv << targetDirx, targetDiry;
            fixedv *= numWaveTar * 2 * M_PI;
        }
        ampForShearing = targetShearAmp;
        angleForShearing = targetShearAngle;
        // target vector fields
        generateValues(tarFunctionType, tarOmegaFields, tarZvals, tarTheoGradZvals, upsampledTarTheoZVals, singIndTar, singIndTar1, isFixedTar, fixedx, fixedy, fixedv, ampForShearing, angleForShearing);

        // update the theoretic ones
        updateTheoMagnitudePhase(sourceZvals, tarZvals, sourceTheoGradZvals, tarTheoGradZvals, upsampledTheoZVals, upsampledTarTheoZVals, numFrames, theoAmpFieldsList, theoPhaseFieldsList, theoOmegaList, theoZList);

        // solve for the path from source to target
        solveKeyFrames(sourceOmegaFields, tarOmegaFields, sourceZvals, tarZvals, numFrames, omegaList, zList);
        // get interploated amp and phase frames
        updateMagnitudePhase(omegaList, zList, ampFieldsList, phaseFieldsList);

        // update amp max
        if (theoAmpFieldsList.size() > 0) {
            globalAmpMax = theoAmpFieldsList[0].maxCoeff();
            for (int i = 0; i < theoAmpFieldsList.size(); i++) {
                globalAmpMax = std::max(theoAmpFieldsList[i].maxCoeff(), globalAmpMax);
                globalAmpMax = std::max(ampFieldsList[i].maxCoeff(), globalAmpMax);
            }
        }

        updateFieldsInView(curFrame);

    }
    if (ImGui::Button("output images", ImVec2(-1, 0)))
    {
        for (curFrame = 0; curFrame < ampFieldsList.size(); curFrame++)
        {
            updateFieldsInView(curFrame);
            polyscope::options::screenshotExtension = ".png";
            polyscope::screenshot();
        }
        curFrame = 0;
    }

    if (ImGui::Button("test deriv", ImVec2(-1, 0))) {
        int testNumFrames = 3;
        triMesh = MeshConnectivity(triF);
        Eigen::MatrixXd testW0 = Eigen::MatrixXd::Random(triMesh.nEdges(), 2);
        Eigen::MatrixXd testW1 = Eigen::MatrixXd::Random(triMesh.nEdges(), 2);

        std::vector<std::complex<double>> testZ0(triV.rows()), testZ1(triV.rows());

        for(int i = 0; i < testZ0.size(); i++)
        {
            Eigen::Vector2d rv;
            rv.setRandom();
            testZ0[i] = std::complex<double>(rv(0), rv(1));
            rv.setRandom();
            testZ1[i] = std::complex<double>(rv(0), rv(1));
        }

        std::vector<Eigen::MatrixXd> wTarDir(testNumFrames + 2);
        std::vector<Eigen::VectorXd> aTar(testNumFrames + 2);

        for(int i = 0; i < testNumFrames + 2; i++)
        {
            wTarDir[i].setRandom(triMesh.nEdges(), 2);
            aTar[i].setRandom(triV.rows());
        }
        Eigen::VectorXd faceArea;
        Eigen::MatrixXd cotEntries;

        igl::cotmatrix_entries(triV, triF, cotEntries);
        igl::doublearea(triV, triF, faceArea);

        IntrinsicFormula::IntrinsicKnoppelDrivenFormula testModel(triMesh, faceArea, cotEntries, wTarDir, aTar,testZ0, testZ1, testW0, testW1, testNumFrames, 1, 4);

        Eigen::VectorXd x;
        testModel.convertList2Variable(x);
        testModel.testEnergy(x);


//        int mFlag = (functionType == Shearing) ? 0 : 1;
//
//        StrainDrivenModel testModel = StrainDrivenModel(wTarDir, aTar, triV, triF, testW0, testW1, testZ0, testZ1, testNumFrames, quadOrder, smoothCoef, fakeThickness, mFlag);
//
//        Eigen::VectorXd x;
//        testModel.convertList2Variable(x);
//
//        testModel.testSpatialEnergyPerFramePerVertex(0, 0);
//        testModel.testEnergy(x);
    }

    ImGui::PopItemWidth();
}





int main(int argc, char** argv)
{
	initialization();

	// Options
	polyscope::options::autocenterStructures = true;
	polyscope::view::windowWidth = 1024;
	polyscope::view::windowHeight = 1024;

	// Initialize polyscope
	polyscope::init();


	// Register the mesh with Polyscope
	polyscope::registerSurfaceMesh("input mesh", triV, triF);



	// Add the callback
	polyscope::state::userCallback = callback;
	polyscope::options::groundPlaneHeightFactor = 0.25; // adjust the plane height
	// Show the gui
	polyscope::show();

	return 0;
}