// HellwalkerRL — engine-free core. What every boss brain shares: the interface the encounter drives, the
// decision record the HUD / telemetry read, the Read Meter event, and the boss scripts.
//
// Three brains implement IBossBrain:
//   FScriptBrain   (HWScriptBrain.h)   Pathbreaker — the fixed script, played verbatim (the control arm).
//   FRLBrain       (HWRLBrain.h)       Hellwalker — the reinforcement-learning keeper (RL.md). The game's adaptive boss.
//   FClassicBrain  (Sim/Classic/)      the reference project's tally brain, kept OUT of the game: the simulator and
//                                      the RL evaluation use it as the benchmark the RL keeper is measured against.
//
// Frame order (identical in Unreal and in the simulator):
//   1. Think(Duel, Geometry)     — may commit one boss move; it sees nothing the player pressed THIS frame
//   2. <caller applies player input to the duel>
//   3. FDuel::Step, then OnFrame(Events, Duel, Geometry, PlayerMovement)

#pragma once

#include "HWDuel.h"

#include <vector>

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

	/** The default boss script: "The Ninefold Warden". Pathbreaker plays it verbatim. */
	int32_t DefaultWardenScript(const FScriptSlot*& OutSlots);
	/** Boss scripts by index: 0 the Ninefold Warden, 1 the Monkey Sage. */
	int32_t BossScript(int32_t Index, const FScriptSlot*& OutSlots);
	inline constexpr int32_t NumBossScripts = 2;

	/**
	 * Where the fighters stand on the arena floor, in cm (world X/Y; the simulator's arena, or the actors in Unreal).
	 * Geometry is on screen, so any brain may look at it; brains that must perceive with a delay keep their own history.
	 *
	 * Handedness: the simulator's floor is right-handed (+Y is to the LEFT of +X, counter-clockwise). Unreal's is
	 * left-handed (+Y is to the right of +X). Unreal sets bMirrorY, and brains that care about left / right (the RL
	 * keeper, trained in the simulator) mirror Y — positions and the fighters' commit facings — so "the player stepped
	 * to its left" means the same thing in training and in the game.
	 */
	struct FDuelGeometry
	{
		float PlayerX = 0.f;
		float PlayerY = 0.f;
		float BossX = 650.f;
		float BossY = 0.f;
		bool  bMirrorY = false;
		float Distance() const;
	};

	enum class EDecisionKind : uint8_t
	{
		Script, Substitution, LuckyDrawPress, LuckyDrawAbort, PerfectPunish, Spacing, CapForcedScript, // classic / script
		Policy,                                                                                          // the RL keeper
		Count
	};
	const char* DecisionKindName(EDecisionKind K);
	inline constexpr int32_t NumDecisionKinds = static_cast<int32_t>(EDecisionKind::Count);

	struct FCandidateScore
	{
		EMoveId Move = EMoveId::None;
		float   Expected = 0.f;       // classic: E[payoff] under the prediction. RL: the masked logit
		float   Score = 0.f;          // classic: per-frame normalised score. RL: the policy's probability
		float   Decision = 0.f;       // classic: Score - Margin; argmax is chosen. RL: equal to Score
	};

	struct FBrainDecision
	{
		int32_t        Frame = 0;
		int32_t        ScriptIndex = 0;       // script brains: the slot. RL: -1
		ESlotType      Slot = ESlotType::Attack;
		bool           bChain = false;
		EMoveId        Scripted = EMoveId::None;  // script brains: the slot's move. RL: None
		EMoveId        Chosen = EMoveId::None;    // RL: None when the keeper chose to wait
		EDecisionKind  Kind = EDecisionKind::Script;
		EHitOutcome    PrevOutcome = EHitOutcome::None;
		int32_t        FrameAdvantage = 0;
		float          ReadBits = 0.f;        // certainty about the player's answer: log2(12) - entropy of the prediction
		float          Margin = 0.f;          // classic only
		float          PressureBias = 0.f;    // classic only
		float          FloorCorrection = 0.f; // classic only
		ESym           Predicted = ESym::Neutral;
		float          PredictedP = 0.f;
		ESym           Ctx0 = ESym::Count;
		ESym           Ctx1 = ESym::Count;
		FCandidateScore Top[3];
		int32_t        NumTop = 0;
		bool           bArgmaxHolds = true;   // C3: chosen == argmax on every decision
		float          Value = 0.f;           // RL: the critic's estimate of the position
		char           Reason[192] = {};
	};

	/** Shown ~0.4 s immediately AFTER a read counter lands. Testimony, not telegraph. */
	struct FReadMeterEvent
	{
		int32_t Frame = 0;
		ESym    Predicted = ESym::Neutral;
		float   Confidence = 0.f;
		EMoveId Counter = EMoveId::None;
	};

	class IBossBrain
	{
	public:
		virtual ~IBossBrain() = default;

		virtual EBrainMode Mode() const = 0;
		/** Short label for logs / HUD, e.g. "Pathbreaker (script)", "Hellwalker (RL)". */
		virtual const char* Name() const = 0;

		/** ResetEncounter(): per-fight state, seeded. Session memory (whatever the brain learned about you) persists. */
		virtual void BeginEncounter(int32_t Seed) = 0;
		/** Step 1 of a frame. Commits at most one boss move. Returns true if a decision record was produced. */
		virtual bool Think(FDuel& Duel, const FDuelGeometry& Geo, FBrainDecision* OutDecision) = 0;
		/** Step 3 of a frame, after FDuel::Step: that frame's events. PlayerMovement: Neutral / Advance / Retreat. */
		virtual void OnFrame(const std::vector<FDuelEvent>& Events, const FDuel& Duel, const FDuelGeometry& Geo, ESym PlayerMovement) = 0;

		virtual bool PopReadMeter(FReadMeterEvent& Out) = 0;
		virtual const FBrainDecision& LastDecision() const = 0;
		virtual int32_t Decisions() const = 0;
		/** Read counters that landed (the Read Meter's count). */
		virtual int32_t CountersLanded() const = 0;

		// Telemetry some brains have (defaults: none).
		virtual int32_t Substitutions() const { return 0; }
		virtual float GetPressureBias() const { return 0.f; }
		virtual int32_t ShadowSwingDeficit() const { return 0; }
		virtual float ShadowScriptRate(int32_t Frame) const { (void)Frame; return 0.f; }
		virtual int32_t SymbolsObserved() const { return 0; }
	};
}
