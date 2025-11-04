#include "MandelbulbOrbitGenerator.h"
#include "Misc/ScopeLock.h"

DEFINE_LOG_CATEGORY_STATIC(LogMandelbulbOrbit, Log, All);

FMandelbulbOrbitGenerator::FMandelbulbOrbitGenerator()
{
}

FMandelbulbOrbitGenerator::~FMandelbulbOrbitGenerator()
{
}

FReferenceOrbit FMandelbulbOrbitGenerator::GenerateOrbit(
	const FVector3d& ReferenceCenter,
	double Power,
	int32 MaxIterations,
	double BailoutRadius
) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FMandelbulbOrbitGenerator::GenerateOrbit);

	FReferenceOrbit Result;
	Result.ReferenceCenter = ReferenceCenter;
	Result.Power = Power;
	Result.BailoutRadius = BailoutRadius;
	Result.EscapeIteration = -1;
	Result.bValid = false;

	// Reserve space for orbit points
	Result.Points.Reserve(MaxIterations);

	// Initial point: z_0 = 0
	FVector3d Z = FVector3d::ZeroVector;
	Result.Points.Add(FOrbitPoint(Z, 0, false));

	// Iterate Mandelbulb formula: z_{n+1} = g_p(z_n) + C_0
	// Following the research pseudocode exactly
	for (int32 Iteration = 0; Iteration < MaxIterations; ++Iteration)
	{
		// Extract components
		double X = Z.X;
		double Y = Z.Y;
		double ZVal = Z.Z;

		// Compute radius: r = sqrt(x^2 + y^2 + z^2)
		double R = FMath::Sqrt(X * X + Y * Y + ZVal * ZVal);

		// Check bailout condition
		if (R > BailoutRadius)
		{
			Result.EscapeIteration = Iteration;
			Result.Points.Last().bEscaped = true;
			break;
		}

		// Convert to spherical coordinates
		// Handle edge cases where r is very small to avoid division by zero
		double Theta = 0.0;
		double Phi = 0.0;

		if (R > 1e-10)
		{
			// theta = acos(z/r), clamped to [-1, 1] for numerical stability
			double CosTheta = FMath::Clamp(ZVal / R, -1.0, 1.0);
			Theta = FMath::Acos(CosTheta);

			// phi = atan2(y, x)
			Phi = FMath::Atan2(Y, X);
		}

		// Apply power transformation in spherical space
		// r_new = r^p
		double RPowered = FMath::Pow(R, Power);

		// theta_new = p * theta
		double ThetaNew = Power * Theta;

		// phi_new = p * phi
		double PhiNew = Power * Phi;

		// Convert back to Cartesian coordinates
		// x = r * sin(theta) * cos(phi)
		// y = r * sin(theta) * sin(phi)
		// z = r * cos(theta)
		double SinTheta = FMath::Sin(ThetaNew);
		double CosTheta = FMath::Cos(ThetaNew);
		double SinPhi = FMath::Sin(PhiNew);
		double CosPhi = FMath::Cos(PhiNew);

		Z.X = RPowered * SinTheta * CosPhi;
		Z.Y = RPowered * SinTheta * SinPhi;
		Z.Z = RPowered * CosTheta;

		// Add constant C (reference center)
		Z += ReferenceCenter;

		// Store orbit point
		Result.Points.Add(FOrbitPoint(Z, Iteration + 1, false));
	}

	// Mark as valid if we have at least one point
	Result.bValid = Result.Points.Num() > 0;

	UE_LOG(LogMandelbulbOrbit, Verbose, 
		TEXT("Generated orbit: Center=(%.6f, %.6f, %.6f), Power=%.2f, Iterations=%d, Escaped=%s at iter %d"),
		ReferenceCenter.X, ReferenceCenter.Y, ReferenceCenter.Z,
		Power,
		Result.Points.Num(),
		Result.EscapeIteration >= 0 ? TEXT("Yes") : TEXT("No"),
		Result.EscapeIteration
	);

	return Result;
}

void FMandelbulbOrbitGenerator::ConvertOrbitToFloat(
	const FReferenceOrbit& Orbit,
	TArray<FVector4f>& OutFloatData
)
{
	OutFloatData.Reset();
	OutFloatData.Reserve(Orbit.Points.Num());

	for (const FOrbitPoint& Point : Orbit.Points)
	{
		// Pack as (x, y, z, unused)
		// Convert from double to float (acceptable precision loss after relative offset)
		OutFloatData.Add(FVector4f(
			static_cast<float>(Point.Position.X),
			static_cast<float>(Point.Position.Y),
			static_cast<float>(Point.Position.Z),
			0.0f
		));
	}
}

FVector3d FMandelbulbOrbitGenerator::MandelbulbIteration(
	const FVector3d& Z,
	const FVector3d& C,
	double Power
)
{
	// Apply spherical power transform then add constant
	return SphericalPowerTransform(Z, Power) + C;
}

FVector3d FMandelbulbOrbitGenerator::CartesianToSpherical(const FVector3d& Cartesian)
{
	double X = Cartesian.X;
	double Y = Cartesian.Y;
	double Z = Cartesian.Z;

	// r = sqrt(x^2 + y^2 + z^2)
	double R = FMath::Sqrt(X * X + Y * Y + Z * Z);

	// Avoid division by zero
	if (R < 1e-10)
	{
		return FVector3d(0.0, 0.0, 0.0);
	}

	// theta = acos(z/r), clamped for numerical stability
	double CosTheta = FMath::Clamp(Z / R, -1.0, 1.0);
	double Theta = FMath::Acos(CosTheta);

	// phi = atan2(y, x)
	double Phi = FMath::Atan2(Y, X);

	return FVector3d(R, Theta, Phi);
}

FVector3d FMandelbulbOrbitGenerator::SphericalToCartesian(double R, double Theta, double Phi)
{
	double SinTheta = FMath::Sin(Theta);
	double CosTheta = FMath::Cos(Theta);
	double SinPhi = FMath::Sin(Phi);
	double CosPhi = FMath::Cos(Phi);

	return FVector3d(
		R * SinTheta * CosPhi,  // x
		R * SinTheta * SinPhi,  // y
		R * CosTheta            // z
	);
}

FVector3d FMandelbulbOrbitGenerator::SphericalPowerTransform(const FVector3d& Z, double Power)
{
	// Convert to spherical
	FVector3d Spherical = CartesianToSpherical(Z);
	double R = Spherical.X;
	double Theta = Spherical.Y;
	double Phi = Spherical.Z;

	// Apply power transformation
	// r_new = r^p
	double RPowered = FMath::Pow(R, Power);

	// theta_new = p * theta
	double ThetaNew = Power * Theta;

	// phi_new = p * phi
	double PhiNew = Power * Phi;

	// Convert back to Cartesian
	return SphericalToCartesian(RPowered, ThetaNew, PhiNew);
}
