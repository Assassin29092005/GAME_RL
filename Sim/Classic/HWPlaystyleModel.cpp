// Hellwalker — engine-free core. Playstyle model implementation.

#include "Classic/HWPlaystyleModel.h"

#include <cmath>
#include <cstring>

namespace HW
{
	namespace
	{
		// Hand-authored typical-player frequency vector (order-0 seed, PLAN §2.2).
		// Neutral Advance Retreat Light Heavy Block Parry StepF StepB StepL StepR Switch
		constexpr float TypicalPlayer[NumPlayerSymbols] = {
			0.08f, 0.10f, 0.06f, 0.24f, 0.06f, 0.12f, 0.10f, 0.02f, 0.08f, 0.06f, 0.06f, 0.02f
		};

		float HalfLifeToDecay(float HalfLife)
		{
			return HalfLife > 0.f ? std::pow(0.5f, 1.0f / HalfLife) : 0.f;
		}
	}

	FPlaystyleModel::FPlaystyleModel()
	{
		RecomputeLambdas();
		Reset();
	}

	void FPlaystyleModel::RecomputeLambdas()
	{
		for (int32_t K = 0; K < 3; ++K)
		{
			Lambda[K] = HalfLifeToDecay(Config.HalfLife[K]);
		}
		PriorDecay = HalfLifeToDecay(Config.PriorHalfLife);
		LeadDecay = HalfLifeToDecay(Config.LeadHalfLife);
		BiteDecay = HalfLifeToDecay(Config.BiteHalfLife);
	}

	void FPlaystyleModel::Reset()
	{
		std::memset(N0, 0, sizeof(N0));
		std::memset(N1, 0, sizeof(N1));
		std::memset(N2, 0, sizeof(N2));
		Gain[0] = Gain[1] = Gain[2] = 1.f;
		for (int32_t S = 0; S < NP; ++S)
		{
			Prior[S] = TypicalPlayer[S];
			LeadMeanArr[S] = 0.f;
			LeadWArr[S] = 0.f;
			BiteMeanArr[S] = 1.f;
			BiteWArr[S] = 0.f;
		}
		PriorMass = Config.PriorWeight;
		H[0] = H[1] = 0;
		Depth = 0;
		NumPending = 0;
		TotalObserved = 0;
	}

	void FPlaystyleModel::Observe(ESym S, int32_t LeadFrames, int32_t Bit)
	{
		if (NumPending >= MaxPending)
		{
			Flush();
		}
		FPending& P = Pending[NumPending++];
		P.C0 = static_cast<int16_t>(H[0]);
		P.C1 = static_cast<int16_t>(H[1]);
		P.CtxDepth = static_cast<uint8_t>(Depth);
		P.Sym = static_cast<uint8_t>(SymIndex(S));
		P.Lead = static_cast<int16_t>(LeadFrames <= NoTiming ? NoTiming : (LeadFrames > 30000 ? 30000 : (LeadFrames < -30000 ? -30000 : LeadFrames)));
		P.Bit = static_cast<int8_t>(Bit == 0 || Bit == 1 ? Bit : NoBait);

		H[0] = H[1];
		H[1] = SymIndex(S);
		if (Depth < 2) { ++Depth; }
		++TotalObserved;
	}

	void FPlaystyleModel::Flush()
	{
		for (int32_t I = 0; I < NumPending; ++I)
		{
			const FPending& P = Pending[I];
			// Recency by gain inflation: the increment grows instead of every cell decaying.
			Gain[0] /= Lambda[0];
			Gain[1] /= Lambda[1];
			Gain[2] /= Lambda[2];

			N0[P.Sym] += Gain[0];
			if (P.CtxDepth >= 1)
			{
				N1[P.C1][P.Sym] += Gain[1];
			}
			if (P.CtxDepth >= 2)
			{
				N2[P.C0 * A + P.C1][P.Sym] += Gain[2];
			}

			if (P.Sym < NP)
			{
				PriorMass *= PriorDecay;
				if (P.Lead != NoTiming)
				{
					float& W = LeadWArr[P.Sym];
					float& Mean = LeadMeanArr[P.Sym];
					W = W * LeadDecay + 1.f;
					Mean += (static_cast<float>(P.Lead) - Mean) / W;
				}
				if (P.Bit != NoBait)
				{
					float& W = BiteWArr[P.Sym];
					float& Mean = BiteMeanArr[P.Sym];
					W = W * BiteDecay + 1.f;
					Mean += (static_cast<float>(P.Bit) - Mean) / W;
				}
			}
			RenormaliseIfNeeded();
		}
		NumPending = 0;
	}

	void FPlaystyleModel::RenormaliseIfNeeded()
	{
		if (Gain[0] > Config.RenormThreshold)
		{
			const float Inv = 1.f / Gain[0];
			for (float& C : N0) { C *= Inv; }
			Gain[0] = 1.f;
		}
		if (Gain[1] > Config.RenormThreshold)
		{
			const float Inv = 1.f / Gain[1];
			for (int32_t R = 0; R < A; ++R) { for (int32_t S = 0; S < A; ++S) { N1[R][S] *= Inv; } }
			Gain[1] = 1.f;
		}
		if (Gain[2] > Config.RenormThreshold)
		{
			const float Inv = 1.f / Gain[2];
			for (int32_t R = 0; R < A * A; ++R) { for (int32_t S = 0; S < A; ++S) { N2[R][S] *= Inv; } }
			Gain[2] = 1.f;
		}
	}

	void FPlaystyleModel::BlendRow(const float* Row, float G, float* InOutP, float& OutN, bool& bOutUsed) const
	{
		float Eff[NP];
		float N = 0.f;
		float U = 0.f;
		const float InvG = 1.f / G;
		for (int32_t S = 0; S < NP; ++S)
		{
			Eff[S] = Row[S] * InvG;
			N += Eff[S];
			U += Eff[S] < 1.f ? Eff[S] : 1.f; // soft type count: decayed evidence counts fractionally
		}
		OutN = N;
		bOutUsed = false;
		// Thin-data floor (correction 2): a row with less evidence than the floor is not blended at all.
		if (N < Config.NEffFloor || N <= 0.f) { return; }
		const float W = N / (N + Config.Beta * U);
		for (int32_t S = 0; S < NP; ++S)
		{
			InOutP[S] = W * (Eff[S] / N) + (1.f - W) * InOutP[S];
		}
		bOutUsed = true;
	}

	FPrediction FPlaystyleModel::Predict(int32_t C0, int32_t C1, int32_t CtxDepth) const
	{
		FPrediction Out;
		float* P = Out.P;
		for (int32_t S = 0; S < NP; ++S) { P[S] = 1.f / static_cast<float>(NP); } // uniform root

		// Order 0 — seeded prior plus decayed counts. The prior is always blendable (non-degenerate from symbol 1).
		{
			float C[NP];
			float N = 0.f;
			float U = 0.f;
			const float InvG = 1.f / Gain[0];
			for (int32_t S = 0; S < NP; ++S)
			{
				C[S] = N0[S] * InvG + PriorMass * Prior[S];
				N += C[S];
				U += C[S] < 1.f ? C[S] : 1.f;
			}
			if (N > 0.f)
			{
				const float W = N / (N + Config.Beta * U);
				for (int32_t S = 0; S < NP; ++S)
				{
					P[S] = W * (C[S] / N) + (1.f - W) * P[S];
				}
				Out.OrderUsed = 0;
				Out.Evidence = N;
			}
		}

		if (CtxDepth >= 1)
		{
			float N = 0.f;
			bool bUsed = false;
			BlendRow(N1[C1], Gain[1], P, N, bUsed);
			if (bUsed) { Out.OrderUsed = 1; Out.Evidence = N; }
			Out.Ctx1 = static_cast<ESym>(C1);
		}
		if (CtxDepth >= 2)
		{
			float N = 0.f;
			bool bUsed = false;
			BlendRow(N2[C0 * A + C1], Gain[2], P, N, bUsed);
			if (bUsed) { Out.OrderUsed = 2; Out.Evidence = N; }
			Out.Ctx0 = static_cast<ESym>(C0);
		}

		// Normalise defensively, then entropy / read / top-1. Deterministic order: lowest index wins ties.
		float Sum = 0.f;
		for (int32_t S = 0; S < NP; ++S) { Sum += P[S]; }
		float H2 = 0.f;
		Out.TopP = -1.f;
		for (int32_t S = 0; S < NP; ++S)
		{
			P[S] = Sum > 0.f ? P[S] / Sum : 1.f / static_cast<float>(NP);
			if (P[S] > 0.f) { H2 -= P[S] * std::log2(P[S]); }
			if (P[S] > Out.TopP) { Out.TopP = P[S]; Out.Top = static_cast<ESym>(S); }
		}
		Out.Entropy = H2;
		Out.ReadBits = MaxReadBits - H2;
		if (Out.ReadBits < 0.f) { Out.ReadBits = 0.f; }
		return Out;
	}

	FPrediction FPlaystyleModel::PredictAfter(ESym BossSym) const
	{
		const int32_t B = SymIndex(BossSym);
		// Context = (most recent live symbol, the hypothetical boss commitment).
		return Predict(Depth >= 1 ? H[1] : 0, B, Depth >= 1 ? 2 : 1);
	}

	FPrediction FPlaystyleModel::PredictNow() const
	{
		return Predict(H[0], H[1], Depth);
	}

	float FPlaystyleModel::EffectiveCount(int32_t Order, ESym C0, ESym C1, ESym S) const
	{
		const int32_t Si = SymIndex(S);
		switch (Order)
		{
		case 0:  return N0[Si] / Gain[0];
		case 1:  return N1[SymIndex(C1)][Si] / Gain[1];
		default: return N2[SymIndex(C0) * A + SymIndex(C1)][Si] / Gain[2];
		}
	}
}
