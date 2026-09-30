// HellwalkerRL — CLASSIC (reference) brain, tools only. NOT compiled into the game.
//
// This is the reference Hellwalker project's boss brain, kept verbatim as the BENCHMARK the RL keeper is measured
// against (RL.md §7: "classic vs RL", "the same margin the classic brain achieves"). Opponent modelling + a best
// response: a variable-order Markov model counts the player's answers (FPlaystyleModel), a payoff table measured
// through FDuel scores every boss move against every answer (FPayoffTable), and the brain substitutes the best
// counter into its script when it beats the scripted move by a margin that shrinks with certainty.
//
// The SCRIPT owns whether and when the boss acts; the model only decides WHICH action fills a slot. One brain,
// one flag (Pathbreaker / Hellwalker) — in the tools, the game's FScriptBrain plays the Pathbreaker arm.

#pragma once

#include "HWCore/HWBrain.h"
#include "HWCore/HWRandom.h"
#include "Classic/HWPlaystyleModel.h"

namespace HW
{
	struct FBrainConfig
	{
		// §2.4 step 3: Margin = lerp(MarginHigh, MarginLow, clamp(Read / ReadForMinMargin, 0, 1))
		float MarginHigh = 3.0f;
		float MarginLow = 0.4f;
		float ReadForMinMargin = 2.0f;

		// §1.2 aggression floor — a runtime invariant, not a playtest observation.
		float RefSwingsPerMin = 0.f;      // from the scripted boss (B0 / A2); 0 disables the controller
		float FloorGain = 0.02f;
		float PressureBiasMax = 1.5f;
		float PressureDecay = 0.97f;      // per slot, when there is no deficit
		int32_t FloorWarmupFrames = 900;  // 15 s before the controller trusts its own rate
		float AbortSuppressBias = 1.0f;   // above this, Lucky-Draw aborts are suppressed

		// §2.5 guard rails — cap adaptation strength.
		int32_t MaxConsecutiveSubs = 3;
		int32_t SubWindow = 12;
		int32_t MaxSubsInWindow = 8;

		// Timing the counter: timing-aware payoffs need this much sidecar evidence.
		float MinLeadEvidence = 3.0f;

		// Aggression floor, matched-player form: in the SAME slots, the script's swing rate (its swings over
		// its moves' commitment frames) vs the brain's. A deficit (swings/min) raises pressure every slot.
		float ShadowGain = 0.03f;
		float FloorMarginSpm = 1.0f;

		bool bLuckyDraw = true;
		// Payoffs measured from the combat rules (HWPayoffTable) instead of the hand-authored matrix.
		bool bDerivedPayoffs = true;
		// Animation reading, not input reading: a player swing counts as "seen" only once it has been
		// winding up this many frames (~100 ms). Earlier than that, the boss cannot have reacted to it.
		int32_t ThreatPerceptionFrames = 6;
		float InRangeFactor = 0.9f;       // attack is feasible when Distance <= Range * this
	};

	/**
	 * Per-script brain tuning, applied wherever a script is set. The Sage's script repositions a lot, so its floor
	 * controller aims further above the shadow script's swing rate (B0). Every value is set explicitly.
	 */
	void ApplyScriptTuning(int32_t Index, FBrainConfig& Cfg);

	struct FSymbolRecord
	{
		ESym    Sym = ESym::Neutral;
		int32_t Frame = 0;
		int32_t Lead = FPlaystyleModel::NoTiming;
	};

	/**
	 * The classic brain's eyes (PLAN §2.1 cadence): a symbol at every commitment event, a movement symbol when a boss
	 * damage window resolves with no player commitment, and a 45-frame watchdog. Feeds the playstyle model.
	 */
	class FSymbolObserver
	{
	public:
		static constexpr int32_t WatchdogFrames = 45;
		/** Timing samples only when the impact the player timed to lands at least this far after the boss commits. */
		static constexpr int32_t TimingHorizonFrames = 18;

		void Reset();
		void ProcessFrame(const std::vector<FDuelEvent>& Events, const FDuel& Duel, ESym PlayerMovement,
			FPlaystyleModel* Model, std::vector<FSymbolRecord>* OutLog);
		int32_t Emitted() const { return NumEmitted; }

	private:
		void Emit(ESym S, int32_t Frame, int32_t Lead, FPlaystyleModel* Model, std::vector<FSymbolRecord>* OutLog,
			int32_t Bit = FPlaystyleModel::NoBait);

		int32_t LastEmitFrame = 0;
		int32_t LastBossCommitFrame = -1;
		EMoveId PendingBossMove = EMoveId::None;
		bool    bBossSwingPending = false;
		bool    bPlayerCommittedSinceBossSwing = false;
		int32_t NumEmitted = 0;
	};

	class FClassicBrain : public IBossBrain
	{
	public:
		FClassicBrain();

		/** Full reset of per-encounter state (the model is NOT owned: it persists across encounters). */
		void Reset(int32_t Seed);
		void BindModel(FPlaystyleModel* InModel) { Model = InModel; }
		void SetMode(EBrainMode InMode) { BrainMode = InMode; }
		EBrainMode GetMode() const { return BrainMode; }
		bool IsAdaptive() const { return BrainMode == EBrainMode::Hellwalker; }
		FBrainConfig& Config() { return Cfg; }
		const FBrainConfig& Config() const { return Cfg; }

		void SetScript(const FScriptSlot* Slots, int32_t Count);
		int32_t ScriptLength() const { return ScriptLen; }
		const FScriptSlot& ScriptAt(int32_t I) const { return Script[I % ScriptLen]; }

		/** Record the observer's symbol stream (tests / CSV). */
		bool bRecordSymbols = false;
		std::vector<FSymbolRecord>& SymbolLog() { return Symbols; }

		// IBossBrain
		EBrainMode Mode() const override { return BrainMode; }
		const char* Name() const override { return BrainMode == EBrainMode::Hellwalker ? "Hellwalker (classic)" : "Pathbreaker (classic)"; }
		void BeginEncounter(int32_t Seed) override;
		bool Think(FDuel& Duel, const FDuelGeometry& Geo, FBrainDecision* OutDecision) override;
		void OnFrame(const std::vector<FDuelEvent>& Events, const FDuel& Duel, const FDuelGeometry& Geo, ESym PlayerMovement) override;
		bool PopReadMeter(FReadMeterEvent& Out) override;
		const FBrainDecision& LastDecision() const override { return Last; }
		int32_t Decisions() const override { return DecisionCount; }
		int32_t CountersLanded() const override { return CounterHits; }
		int32_t Substitutions() const override { return SubCount; }
		float GetPressureBias() const override { return PressureBias; }
		int32_t ShadowSwingDeficit() const override { return ShadowScriptedSwings - ShadowChosenSwings; }
		float ShadowScriptRate(int32_t Frame) const override;
		int32_t SymbolsObserved() const override { return Observer.Emitted(); }

		/** Called once per frame BEFORE FDuel::Step (the reference API). Distance: centre-to-centre, cm. */
		bool ThinkAt(FDuel& Duel, float Distance, FBrainDecision* OutDecision);
		/** Feed one duel event (OnFrame does this for every event, then runs the observer). */
		void OnEvent(const FDuelEvent& E, const FDuel& Duel);

		/** The pure decision function (B1/B3 automation targets it directly). */
		FBrainDecision Decide(const FDuel& Duel, const FScriptSlot& InSlot, int32_t InScriptIndex, EHitOutcome InPrevOutcome,
			bool bInOwnDefence, float Distance) const;

		// Telemetry (A2) — also the floor controller's measurement.
		float SwingsPerMin(int32_t Frame) const;
		float DamagePerMin(int32_t Frame) const;
		int32_t Swings() const { return SwingCount; }
		float DamageDealt() const { return DamageTotal; }

		/** Authored payoff for a boss move vs a predicted player symbol, in [-3, 3], before timing. */
		static float BasePayoff(EMoveId BossMove, ESym PlayerSym);
		/** Payoff with the timing sidecar applied (PLAN §2.4 "timing the counter"). */
		float Payoff(EMoveId BossMove, ESym PlayerSym) const;
		/** The lead (frames before perceived impact) this player presses Sym with, or the typical one. */
		int32_t LeadFor(ESym PlayerSym) const;
		/** How often this player times a press to a bait rather than the real strike (1 = always bites). */
		float BiteFor(ESym PlayerSym) const;
		static bool IsWhiffable(ESym PlayerSym);

	private:
		void UpdateFloor(int32_t Frame);
		void ScoreCandidate(EMoveId Cand, EMoveId ScriptedMove, float Margin, float Distance, int32_t IncomingIn, FCandidateScore& Out) const;
		static float IncomingThreatPayoff(const FMoveData& M, int32_t IncomingIn);
		void FinishDecision(FBrainDecision& D, const FCandidateScore* Cands, int32_t NumCands) const;
		bool InRange(EMoveId Id, float Distance) const;
		bool CapReached() const;

		FPlaystyleModel* Model = nullptr;
		EBrainMode BrainMode = EBrainMode::Hellwalker;
		FBrainConfig Cfg;
		mutable FRandom Rng;   // tie-breaks only — the ONLY randomness in the brain
		float LastFloorCorrection = 0.f;

		FSymbolObserver Observer;
		std::vector<FSymbolRecord> Symbols;

		static constexpr int32_t MaxScript = 64;
		FScriptSlot Script[MaxScript];
		int32_t ScriptLen = 0;
		int32_t ScriptIndex = 0;

		// Slot state
		bool    bSlotOpen = false;
		FScriptSlot CurrentSlot;
		int32_t CurrentSlotIndex = 0;

		// Lucky Draw inputs (on-screen facts, not habits)
		EHitOutcome LastSwingOutcome = EHitOutcome::None;
		EMoveId     LastSwingMove = EMoveId::None;
		bool        bOwnDefenceSucceeded = false;

		// Floor controller
		float PressureBias = 0.f;
		int32_t ShadowScriptedSwings = 0;
		int32_t ShadowChosenSwings = 0;
		int32_t ShadowScriptedFrames = 0;
		int32_t ShadowChosenFrames = 0;

		// Guard rails
		int32_t ConsecutiveSubs = 0;
		bool    RecentSubs[32] = {};
		int32_t RecentHead = 0;

		// Telemetry
		int32_t SwingCount = 0;
		float   DamageTotal = 0.f;
		int32_t SubCount = 0;
		int32_t DecisionCount = 0;
		int32_t CounterHits = 0;

		// Read Meter
		EMoveId PendingCounterMove = EMoveId::None;
		ESym    PendingCounterPred = ESym::Neutral;
		float   PendingCounterConf = 0.f;
		bool    bReadMeterReady = false;
		FReadMeterEvent ReadMeter;

		FBrainDecision Last;
	};
}
