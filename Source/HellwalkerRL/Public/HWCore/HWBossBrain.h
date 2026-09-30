// Hellwalker — engine-free core. The boss brain (PLAN §2.4, B1, B3).
//
// The SCRIPT owns whether and when the boss acts. The model only decides WHICH action fills a slot.
// One brain, one flag (Pathbreaker = scripted control arm, Hellwalker = adaptive) — never two
// separately authored brains (B1). Plain C++ scoring so every candidate score is inspectable (C3).

#pragma once

#include "HWDuel.h"
#include "HWPlaystyleModel.h"
#include "HWRandom.h"

namespace HW
{
	/** Pathbreaker = the fixed-pattern control arm. Hellwalker = reads you, adapts, counterattacks. */
	enum class EBrainMode : uint8_t { Pathbreaker, Hellwalker };
	const char* BrainModeName(EBrainMode M);

	struct FScriptSlot
	{
		ESlotType Type = ESlotType::Attack;
		EMoveId   Move = EMoveId::None;
		bool      bChain = false;   // continues the previous attack string at its cancel frame
	};

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
		// The authored matrix is kept for comparison (B0 ablation) and as documentation of intent.
		bool bDerivedPayoffs = true;
		// Animation reading, not input reading: a player swing counts as "seen" only once it has been
		// winding up this many frames (~100 ms). Earlier than that, the boss cannot have reacted to it.
		int32_t ThreatPerceptionFrames = 6;
		float InRangeFactor = 0.9f;       // attack is feasible when Distance <= Range * this
	};

	struct FCandidateScore
	{
		EMoveId Move = EMoveId::None;
		float   Expected = 0.f;       // E[payoff] under the prediction for this candidate's boss symbol
		float   Score = 0.f;          // per-frame normalised (+ pressure bias)
		float   Decision = 0.f;       // Score - Margin for non-scripted candidates; argmax is chosen
	};

	enum class EDecisionKind : uint8_t { Script, Substitution, LuckyDrawPress, LuckyDrawAbort, PerfectPunish, Spacing, CapForcedScript };
	const char* DecisionKindName(EDecisionKind K);

	struct FBrainDecision
	{
		int32_t        Frame = 0;
		int32_t        ScriptIndex = 0;
		ESlotType      Slot = ESlotType::Attack;
		bool           bChain = false;
		EMoveId        Scripted = EMoveId::None;
		EMoveId        Chosen = EMoveId::None;
		EDecisionKind  Kind = EDecisionKind::Script;
		EHitOutcome    PrevOutcome = EHitOutcome::None;
		int32_t        FrameAdvantage = 0;
		float          ReadBits = 0.f;
		float          Margin = 0.f;
		float          PressureBias = 0.f;
		float          FloorCorrection = 0.f; // the correction term applied at this decision (A2 CSV)
		ESym           Predicted = ESym::Neutral;
		float          PredictedP = 0.f;
		ESym           Ctx0 = ESym::Count;
		ESym           Ctx1 = ESym::Count;
		FCandidateScore Top[3];
		int32_t        NumTop = 0;
		bool           bArgmaxHolds = true;   // C3: chosen == argmax(Decision) on every decision
		char           Reason[192] = {};
	};

	/** Shown ~0.4 s immediately AFTER a model-driven counter lands. Testimony, not telegraph. */
	struct FReadMeterEvent
	{
		int32_t Frame = 0;
		ESym    Predicted = ESym::Neutral;
		float   Confidence = 0.f;
		EMoveId Counter = EMoveId::None;
	};

	class FBossBrain
	{
	public:
		FBossBrain();

		/** Per-encounter reset — ResetEncounter(). The model is NOT owned: it persists across encounters. */
		void Reset(int32_t Seed);
		void BindModel(FPlaystyleModel* InModel) { Model = InModel; }
		void SetMode(EBrainMode InMode) { Mode = InMode; }
		EBrainMode GetMode() const { return Mode; }
		bool IsAdaptive() const { return Mode == EBrainMode::Hellwalker; }
		FBrainConfig& Config() { return Cfg; }
		const FBrainConfig& Config() const { return Cfg; }

		void SetScript(const FScriptSlot* Slots, int32_t Count);
		int32_t ScriptLength() const { return ScriptLen; }
		const FScriptSlot& ScriptAt(int32_t I) const { return Script[I % ScriptLen]; }

		/**
		 * Called once per frame BEFORE FDuel::Step. Commits at most one boss move on the duel.
		 * Distance: centre-to-centre, cm. Returns true if a decision record was produced.
		 */
		bool Think(FDuel& Duel, float Distance, FBrainDecision* OutDecision);

		/** Feed every duel event after each step. */
		void OnEvent(const FDuelEvent& E, const FDuel& Duel);

		/** The pure decision function (B1/B3 automation targets it directly). */
		FBrainDecision Decide(const FDuel& Duel, const FScriptSlot& InSlot, int32_t InScriptIndex, EHitOutcome InPrevOutcome,
			bool bInOwnDefence, float Distance) const;

		// Read Meter
		bool PopReadMeter(FReadMeterEvent& Out);

		// Telemetry (A2) — also the floor controller's measurement.
		float SwingsPerMin(int32_t Frame) const;
		float DamagePerMin(int32_t Frame) const;
		int32_t Swings() const { return SwingCount; }
		float DamageDealt() const { return DamageTotal; }
		float GetPressureBias() const { return PressureBias; }
		int32_t ShadowSwingDeficit() const { return ShadowScriptedSwings - ShadowChosenSwings; }
		/** The floor controller's matched-player reference: the script's swing rate in this fight's slots (swings/min). */
		float ShadowScriptRate(int32_t Frame) const;
		int32_t Substitutions() const { return SubCount; }
		int32_t Decisions() const { return DecisionCount; }
		int32_t CountersLanded() const { return CounterHits; }
		const FBrainDecision& LastDecision() const { return Last; }

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
		EBrainMode Mode = EBrainMode::Pathbreaker;
		FBrainConfig Cfg;
		mutable FRandom Rng;   // tie-breaks only — the ONLY randomness in the brain
		float LastFloorCorrection = 0.f;

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

	/** The default boss script: "The Ninefold Warden". Pathbreaker plays it verbatim. */
	int32_t DefaultWardenScript(const FScriptSlot*& OutSlots);

	/** Boss scripts by index: 0 the Ninefold Warden, 1 the Monkey Sage. */
	int32_t BossScript(int32_t Index, const FScriptSlot*& OutSlots);
	inline constexpr int32_t NumBossScripts = 2;

	/**
	 * Per-script brain tuning, applied wherever a script is set (the simulator and the game alike). The Sage's
	 * script repositions a lot, so its floor controller aims further above the shadow script's swing rate (B0).
	 * Every value is set explicitly, so switching back to the Warden restores the defaults.
	 */
	void ApplyScriptTuning(int32_t Index, FBrainConfig& Cfg);
}
