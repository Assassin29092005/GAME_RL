// Hellwalker — engine-free core. Deterministic random stream.
//
// Arithmetic ported from Unreal's FRandomStream (Math/RandomStream.h) so that a seed means the same
// thing in the simulator and in the engine. This is the ONLY source of randomness in the boss brain
// (PLAN §2.2 "No RNG in the model", B1 determinism test).

#pragma once

#include <cstdint>
#include <cstring>

namespace HW
{
	class FRandom
	{
	public:
		FRandom() = default;
		explicit FRandom(int32_t InSeed) { Initialize(InSeed); }

		void Initialize(int32_t InSeed) { InitialSeed = InSeed; Seed = static_cast<uint32_t>(InSeed); }
		void Reset() { Seed = static_cast<uint32_t>(InitialSeed); }
		int32_t GetInitialSeed() const { return InitialSeed; }
		uint32_t GetCurrentSeed() const { return Seed; }

		/** Uniform in [0, 1). */
		float GetFraction()
		{
			MutateSeed();
			const uint32_t Bits = 0x3F800000u | (Seed >> 9);
			float Result;
			std::memcpy(&Result, &Bits, sizeof(float));
			return Result - 1.0f;
		}

		float FRand() { return GetFraction(); }

		uint32_t GetUnsignedInt()
		{
			MutateSeed();
			return Seed;
		}

		/** Uniform integer in [0, A). Bit-exact with FRandomStream::RandHelper (float multiply, truncation). */
		int32_t RandHelper(int32_t A)
		{
			return A > 0 ? static_cast<int32_t>(GetFraction() * static_cast<float>(A)) : 0;
		}

		/** Uniform integer in [Min, Max]. */
		int32_t RandRange(int32_t Min, int32_t Max)
		{
			const int32_t Range = (Max - Min) + 1;
			return Min + RandHelper(Range);
		}

		float FRandRange(float Min, float Max) { return Min + (Max - Min) * GetFraction(); }

		/** Approximately normal (Irwin-Hall, 4 draws), mean 0, sd 1. Used by simulated players only. */
		float Gaussian()
		{
			const float S = GetFraction() + GetFraction() + GetFraction() + GetFraction();
			return (S - 2.0f) * 1.7320508f; // var of sum of 4 U(0,1) = 1/3
		}

		bool Chance(float P) { return GetFraction() < P; }

	private:
		void MutateSeed() { Seed = (Seed * 196314165u) + 907633515u; }

		int32_t InitialSeed = 0;
		uint32_t Seed = 0;
	};
}
