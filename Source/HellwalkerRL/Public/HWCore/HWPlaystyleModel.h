// Hellwalker — engine-free core. The playstyle model (PLAN §2.2) — the heart of the project.
//
// An interleaved variable-order Markov model over ONE symbol stream of player and boss
// commitments. It predicts the player's next symbol from the last two symbols; "playstyle" is the
// low-entropy structure in that stream. Orders 0-2 only (judge correction 1). Witten-Bell
// interpolation with a thin-data floor and beta-scaled escape (correction 2). Recency by gain
// inflation, per order. A timing sidecar answers WHEN; the n-gram answers WHAT.
//
// Flat arrays, direct-indexed, no allocation after construction, deterministic iteration order,
// no randomness.

#pragma once

#include "HWTypes.h"

namespace HW
{
	struct FModelConfig
	{
		float HalfLife[3] = { 48.f, 96.f, 160.f }; // symbols; per order (correction 1: raised, per-order)
		float Beta = 2.5f;                          // Witten-Bell escape: w = n / (n + Beta*U) (correction 2)
		float NEffFloor = 2.0f;                     // a row with less effective evidence is not blended at all
		float PriorWeight = 10.f;                   // seeded order-0 typical-player prior, in observations
		float PriorHalfLife = 12.f;                 // player symbols until the prior's weight halves
		float LeadHalfLife = 10.f;                  // observations; timing sidecar EMA
		float BiteHalfLife = 10.f;                  // observations; bait sidecar EMA (does this player bite?)
		float RenormThreshold = 1.0e12f;            // gain renormalisation point (~1300 symbols at HL 48)
	};

	struct FPrediction
	{
		float   P[NumPlayerSymbols] = {};
		float   Entropy = 0.f;      // bits, over the 12 player symbols
		float   ReadBits = 0.f;     // log2(12) - Entropy, in [0, 3.585]
		ESym    Top = ESym::Neutral;
		float   TopP = 0.f;
		ESym    Ctx0 = ESym::Count; // context used (Count = absent)
		ESym    Ctx1 = ESym::Count;
		float   Evidence = 0.f;     // effective observations in the deepest blended row
		int32_t OrderUsed = -1;     // deepest order whose row was blended (-1 = uniform root only)
		float Prob(ESym S) const { return IsPlayerSym(S) ? P[SymIndex(S)] : 0.f; }
	};

	class FPlaystyleModel
	{
	public:
		static constexpr int32_t A = NumSymbols;        // 20
		static constexpr int32_t NP = NumPlayerSymbols; // 12
		static constexpr int32_t MaxPending = 256;
		static constexpr float   MaxReadBits = 3.5849625f; // log2(12)
		static constexpr int32_t NoTiming = -32768;
		static constexpr int32_t NoBait = -1;          // the swing answered was not a bait (no fake / held wind-up)

		FPlaystyleModel();

		/** Reset on quit (PLAN §2.5). Also re-seeds the order-0 prior. */
		void Reset();
		void SetConfig(const FModelConfig& InConfig) { Config = InConfig; RecomputeLambdas(); }
		const FModelConfig& GetConfig() const { return Config; }

		/**
		 * Append a symbol to the stream. The CONTEXT advances immediately; the COUNTS are held until
		 * Flush() — legibility rule (c): the boss acts on a habit one exchange later, never instantly.
		 * LeadFrames: for a player symbol committed in response to a boss swing, how many frames BEFORE
		 * the impact the player timed it to it was pressed (negative = after). NoTiming when not a response.
		 * Bit: for a response to a BAIT swing (a feint's fake, a held wind-up), 1 if the press was timed to the
		 * bait, 0 if the player waited for the real strike; NoBait otherwise.
		 */
		void Observe(ESym S, int32_t LeadFrames = NoTiming, int32_t Bit = NoBait);

		/** Commit pending counts. Called at an exchange boundary (the boss opening a new string). */
		void Flush();

		/** P(player's next symbol | last symbol, hypothetical boss commitment BossSym). */
		FPrediction PredictAfter(ESym BossSym) const;
		/** P(player's next symbol | last two symbols of the live stream). */
		FPrediction PredictNow() const;

		/**
		 * Timing sidecar — WHEN (the n-gram answers WHAT). EMA of how many frames before a swing's PERCEIVED
		 * impact this player presses this symbol, and its evidence weight. Anchored to the perceived impact,
		 * not the boss's commitment: players time to what the wind-up suggests, which is exactly the gap a
		 * feint or a held wind-up exploits.
		 */
		float LeadMean(ESym PlayerSym) const { return IsPlayerSym(PlayerSym) ? LeadMeanArr[SymIndex(PlayerSym)] : 0.f; }
		float LeadEvidence(ESym PlayerSym) const { return IsPlayerSym(PlayerSym) ? LeadWArr[SymIndex(PlayerSym)] : 0.f; }
		/**
		 * Bait sidecar — also WHEN: against a bait, does this player press at the bait or at the real strike?
		 * EMA of the fraction timed to the bait, and its evidence weight. A mean and a weight, like the lead:
		 * one timing habit is "presses 4 frames early", another is "has stopped falling for the feint".
		 */
		float BiteMean(ESym PlayerSym) const { return IsPlayerSym(PlayerSym) ? BiteMeanArr[SymIndex(PlayerSym)] : 1.f; }
		float BiteEvidence(ESym PlayerSym) const { return IsPlayerSym(PlayerSym) ? BiteWArr[SymIndex(PlayerSym)] : 0.f; }

		int32_t SymbolsObserved() const { return TotalObserved; }
		int32_t PendingCount() const { return NumPending; }
		ESym LastSymbol() const { return Depth >= 1 ? static_cast<ESym>(H[1]) : ESym::Count; }

		/** Effective count of player symbol S in the order-k row for the given context (tests / debug). */
		float EffectiveCount(int32_t Order, ESym C0, ESym C1, ESym S) const;

	private:
		FPrediction Predict(int32_t C0, int32_t C1, int32_t CtxDepth) const;
		void BlendRow(const float* Row, float G, float* InOutP, float& OutN, bool& bOutUsed) const;
		void RecomputeLambdas();
		void RenormaliseIfNeeded();

		FModelConfig Config;
		float Lambda[3] = { 1.f, 1.f, 1.f };

		float N0[A];
		float N1[A][A];
		float N2[A * A][A];
		float Gain[3];

		float Prior[NP];
		float PriorMass = 0.f;
		float PriorDecay = 1.f;

		float LeadMeanArr[NP];
		float LeadWArr[NP];
		float LeadDecay = 1.f;
		float BiteMeanArr[NP];
		float BiteWArr[NP];
		float BiteDecay = 1.f;

		int32_t H[2] = { 0, 0 };  // live context: H[0] older, H[1] most recent
		int32_t Depth = 0;        // symbols in live history, capped at 2

		struct FPending
		{
			int16_t C0;
			int16_t C1;
			int16_t Lead;
			uint8_t Sym;
			uint8_t CtxDepth;
			int8_t  Bit;
		};
		FPending Pending[MaxPending];
		int32_t  NumPending = 0;
		int32_t  TotalObserved = 0;
	};
}
