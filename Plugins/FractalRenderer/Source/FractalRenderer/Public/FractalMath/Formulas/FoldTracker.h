// Signed-permutation folds (abs, descending sort, tetrahedral fold) of the IFS formulas (Inverted Menger,
// Sierpinski Tetrahedron, Kaleidoscopic IFS), in any precision, plus what their exact GPU perturbation needs
// (Shaders/FractalFoldTracker.ush) and their series-skip Jacobian and branch distances.
//
// Each of these folds maps a point to a signed permutation of its components, so after any sequence of them
// slot k of the folded reference holds Sign[k] * X[Index[k]], X being the reference at the start of the
// iteration, and every branch test compares two such values: the tests are signs of +-X_i +- X_j (i != j),
// or of X_i itself for the abs fold. These lie next to O(1) values while pixel offsets are ~1e-27, so the
// GPU gets the six pair values X_i + X_j, X_i - X_j (i < j) rounded from the orbit's precision, and the
// reference's resulting signed permutation (TSignedPermutationFold::Code).

#pragma once

#include "../FractalScalar.h"

#if defined(_MSC_VER) || defined(__clang__)
#pragma float_control(precise, on, push)
#endif

namespace FractalMath
{

namespace Folds
{
	/**
	 * Relative uncertainty of an orbit point rounded to double (2^-52, twice the unit roundoff). The series skip
	 * sees the double-rounded reference, which can sit a few ulps from the exact one: a branch distance it
	 * measures must be shrunk by this much (a reference closer to a fold plane than that gives 0).
	 */
	constexpr double RoundingMargin = 2.220446049250313e-16;

	/**
	 * A point being folded by signed permutations. Value[k] == Sign[k] * X[Index[k]] exactly (abs, swaps and
	 * negations are exact in every precision). BranchDistance is the largest |d| for which X + d takes the same
	 * branch in every fold applied so far (the folds are isometries, so the folded offset has length |d|):
	 * |X_i| for an abs test, |v| / sqrt(2) for a pair test of value v, each less the uncertainty of X if it was
	 * rounded to double. (Rounding is monotonic and odd, so it never flips a test, it can only make it a tie.)
	 */
	template <typename T>
	struct TSignedPermutationFold
	{
		TFractalVec3<T> Value;
		int Index[3] = {0, 1, 2};
		int Sign[3] = {1, 1, 1};
		double BranchDistance = 1.0e300;

		explicit TSignedPermutationFold(const TFractalVec3<T>& X) : Value(X) {}

		/** z = abs(z). */
		void Abs()
		{
			for (int K = 0; K < 3; ++K)
			{
				NoteDistance(std::fabs(RToDouble(Value[K])) * (1.0 - RoundingMargin));
				if (Value[K] < T(0.0))
				{
					Value[K] = -Value[K];
					Sign[K] = -Sign[K];
				}
			}
		}

		/** if (z_a < z_b) swap(z_a, z_b). */
		void SortStep(int A, int B)
		{
			NotePairDistance(Value[A] - Value[B], A, B);
			if (Value[A] < Value[B])
			{
				Exchange(A, B, 1);
			}
		}

		/** Descending sort: if x<y swap x,y; if x<z swap x,z; if y<z swap y,z. */
		void SortDescending()
		{
			SortStep(0, 1);
			SortStep(0, 2);
			SortStep(1, 2);
		}

		/** if (z_a + z_b < 0) (z_a, z_b) = (-z_b, -z_a). */
		void TetrahedralStep(int A, int B)
		{
			const T Sum = Value[A] + Value[B];
			NotePairDistance(Sum, A, B);
			if (Sum < T(0.0))
			{
				Exchange(A, B, -1);
			}
		}

		/** Tetrahedral fold: (x, y), (x, z), (y, z) in this order. */
		void TetrahedralFold()
		{
			TetrahedralStep(0, 1);
			TetrahedralStep(0, 2);
			TetrahedralStep(1, 2);
		}

		/**
		 * The signed permutation as an integer below 512, exact in float: bits 3k..3k+1 hold Index[k], bit 3k+2
		 * is set when Sign[k] is negative (decoded by FP_FoldTrackerOffset in FractalFoldTracker.ush).
		 */
		int Code() const
		{
			int Result = 0;
			for (int K = 0; K < 3; ++K)
			{
				Result |= (Index[K] | (Sign[K] < 0 ? 4 : 0)) << (3 * K);
			}
			return Result;
		}

		/** Jacobian of the folds in the current branch (a signed permutation matrix). */
		void Jacobian(double J[3][3]) const
		{
			for (int K = 0; K < 3; ++K)
			{
				for (int I = 0; I < 3; ++I)
				{
					J[K][I] = (I == Index[K]) ? static_cast<double>(Sign[K]) : 0.0;
				}
			}
		}

	private:
		static constexpr double InvSqrt2 = 0.70710678118654752;

		void NoteDistance(double Distance)
		{
			Distance = Distance > 0.0 ? Distance : 0.0;
			BranchDistance = Distance < BranchDistance ? Distance : BranchDistance;
		}

		/** Pair test of value V = +-Value[A] +- Value[B]. */
		void NotePairDistance(const T& V, int A, int B)
		{
			const double Uncertainty = RoundingMargin * (std::fabs(RToDouble(Value[A])) + std::fabs(RToDouble(Value[B])));
			NoteDistance((std::fabs(RToDouble(V)) - Uncertainty) * InvSqrt2);
		}

		/** (z_a, z_b) = (S z_b, S z_a) for S = +-1. */
		void Exchange(int A, int B, int S)
		{
			const T ValueA = Value[A];
			Value[A] = S > 0 ? Value[B] : -Value[B];
			Value[B] = S > 0 ? ValueA : -ValueA;
			const int IndexA = Index[A];
			Index[A] = Index[B];
			Index[B] = IndexA;
			const int SignA = Sign[A];
			Sign[A] = S * Sign[B];
			Sign[B] = S * SignA;
		}
	};

	/**
	 * Pair table (6 floats) of the reference X at the start of an iteration, rounded from T precision:
	 *   X0 + X1, X0 + X2, X1 + X2, X0 - X1, X0 - X2, X1 - X2
	 * Every branch test of the folds and every difference between a pixel slot and a reference slot holding
	 * different components is +- one of these, so they carry the exact signs and keep relative accuracy at
	 * the fold planes (the sums and differences are formed before rounding).
	 */
	template <typename T>
	inline void PackPairTable(const TFractalVec3<T>& X, float* Out)
	{
		Out[0] = ClampToFloatRange(RToDouble(X.X + X.Y));
		Out[1] = ClampToFloatRange(RToDouble(X.X + X.Z));
		Out[2] = ClampToFloatRange(RToDouble(X.Y + X.Z));
		Out[3] = ClampToFloatRange(RToDouble(X.X - X.Y));
		Out[4] = ClampToFloatRange(RToDouble(X.X - X.Z));
		Out[5] = ClampToFloatRange(RToDouble(X.Y - X.Z));
	}

	/**
	 * Common payload head of the signed-permutation IFS formulas (Out: 10 floats):
	 *   (Z~, reference fold code) | (X0+X1, X0+X2, X1+X2, X0-X1) | (X0-X2, X1-X2, ...)
	 */
	template <typename T>
	inline void PackSignedPermutationFold(const TFractalVec3<T>& X, const TSignedPermutationFold<T>& Fold, float* Out)
	{
		for (int I = 0; I < 3; ++I)
		{
			Out[I] = ClampToFloatRange(RToDouble(X[I]));
		}
		Out[3] = static_cast<float>(Fold.Code());
		PackPairTable(X, Out + 4);
	}
} // namespace Folds

} // namespace FractalMath

#if defined(_MSC_VER) || defined(__clang__)
#pragma float_control(pop)
#endif
