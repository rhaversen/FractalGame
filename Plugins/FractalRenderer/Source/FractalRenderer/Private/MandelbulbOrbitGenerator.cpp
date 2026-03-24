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
	Result.bHasDerivatives = false;

	// Reserve space for orbit points
	Result.Points.Reserve(MaxIterations + 1);

	// Initial point for Mandelbulb DE uses the evaluation point itself (z_0 = C)
	FVector3d Z = ReferenceCenter;
	FOrbitDerivative DzDc = FOrbitDerivative::Identity();
	Result.Points.Add(FOrbitPoint(Z, DzDc, FOrbitDerivative::Zero(), 0, false));

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

		// Apply Mandelbulb power transform
		const FVector3d Transformed = SphericalPowerTransform(Z, Power);

		// Compute Jacobian of the transform with respect to Z
		const FOrbitDerivative Jacobian = ComputeJacobianFiniteDifference(Z, Power);
		Result.Points.Last().TransformJacobian = Jacobian;
		const FOrbitDerivative NextDzDc = Jacobian.Multiply(DzDc) + FOrbitDerivative::Identity();

		// Advance to next orbit point and accumulate derivative
		Z = Transformed + ReferenceCenter;
		DzDc = NextDzDc;

		Result.Points.Add(FOrbitPoint(Z, DzDc, FOrbitDerivative::Zero(), Iteration + 1, false));
	}

	// Mark as valid if we have at least one point
	Result.bValid = Result.Points.Num() > 0;
	Result.bHasDerivatives = Result.bValid;

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

void FMandelbulbOrbitGenerator::BuildOrbitBuffer(
	const FReferenceOrbit& Orbit,
	TArray<FPackedOrbitSample>& OutSamples
)
{
	const int32 NumPoints = Orbit.Points.Num();
	OutSamples.Reset(NumPoints);
	OutSamples.Reserve(NumPoints);

	for (const FOrbitPoint& Point : Orbit.Points)
	{
		FPackedOrbitSample Sample;
		Sample.Position = FVector4f(
			static_cast<float>(Point.Position.X),
			static_cast<float>(Point.Position.Y),
			static_cast<float>(Point.Position.Z),
			0.0f);

		for (int32 ColumnIndex = 0; ColumnIndex < 3; ++ColumnIndex)
		{
			const FVector3d& Column = Point.TransformJacobian.GetColumn(ColumnIndex);
			Sample.TransformJacobian[ColumnIndex] = FVector4f(
				static_cast<float>(Column.X),
				static_cast<float>(Column.Y),
				static_cast<float>(Column.Z),
				0.0f);
		}

		OutSamples.Add(Sample);
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

FOrbitDerivative FMandelbulbOrbitGenerator::ComputeJacobianFiniteDifference(const FVector3d& Z, double Power)
{
	const double Step = 1e-7;
	FOrbitDerivative Jacobian;

	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		FVector3d Forward = Z;
		FVector3d Backward = Z;
		Forward[Axis] += Step;
		Backward[Axis] -= Step;

		const FVector3d ForwardValue = SphericalPowerTransform(Forward, Power);
		const FVector3d BackwardValue = SphericalPowerTransform(Backward, Power);

		const FVector3d Column = (ForwardValue - BackwardValue) * (0.5 / Step);
		Jacobian.SetColumn(Axis, Column);
	}

	return Jacobian;
}
