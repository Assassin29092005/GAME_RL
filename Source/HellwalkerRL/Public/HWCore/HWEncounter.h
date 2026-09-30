// HellwalkerRL — engine-free core. One encounter: duel + boss brain + telemetry.
//
// Frame order, identical in Unreal and in the simulator:
//   1. ThinkBoss(Geometry)     — the brain may commit; it sees nothing the player pressed THIS frame
//   2. <caller applies player input to Duel>  (timestamped input, PLAN §5.2)
//   3. StepFrame(Oracle, Move) — resolve contacts, advance, dispatch events to the brain and the stats
//
// The brain is NOT owned: the caller keeps it (and whatever session memory it has) alive across encounters.

#pragma once

#include "HWBrain.h"

#include <vector>

namespace HW
{
	struct FEncounterStats
	{
		int32_t Frames = 0;
		float   PlayerDamageTaken = 0.f;
		float   BossDamageTaken = 0.f;
		int32_t BossSwings = 0;
		int32_t PlayerSwings = 0;
		int32_t BossOutcomes[5] = {};    // indexed by EHitOutcome
		int32_t PlayerOutcomes[5] = {};
		int32_t Decisions = 0;
		int32_t DecisionKinds[NumDecisionKinds] = {};   // indexed by EDecisionKind
		int32_t CountersLanded = 0;
		int32_t Symbols = 0;             // classic brain only: symbols its observer emitted
		int32_t PlayerGuardBreaks = 0;
		int32_t BossExposed = 0;
		int32_t PlayerDeaths = 0;        // immortal mode counts would-be deaths
		bool    bPlayerDied = false;
		bool    bBossDied = false;
		int32_t DeathFrame = -1;
		int32_t MoveSelect[NumMoves] = {}; // per-mode move-selection frequencies (B4)
		float   DamageByMove[NumMoves] = {}; // damage dealt, by the move that dealt it (either side)
		int32_t HitsByMove[NumMoves] = {};
		float   BossDamageByPhase[static_cast<int32_t>(EDefenderPhase::Count)] = {}; // where the boss gets hurt
		float   BossDamageAfterMove[NumMoves] = {}; // boss damage taken, attributed to the boss's last committed move
		int32_t BossOutcomeByMove[NumMoves][5] = {}; // boss swing outcomes, by move (indexed by EHitOutcome)
		int32_t BossExposedFrames = 0;     // frames the boss spent posture-broken (no swings possible, x1.5 damage)
		static constexpr int32_t MaxSlots = 64;
		float   BossDamageAfterSlot[MaxSlots] = {};   // boss damage taken, attributed to the last decision's script slot
		float   PlayerDamageBySlot[MaxSlots] = {};    // boss damage dealt, attributed the same way
		int32_t DecisionsBySlot[MaxSlots] = {};
		int32_t SwingsBySlot[MaxSlots] = {};
		float   BossDamageAfterKind[NumDecisionKinds] = {};  // ... and to the last decision's kind (EDecisionKind)
		float   PlayerDamageByKind[NumDecisionKinds] = {};
		int32_t ArgmaxViolations = 0;      // C3
		// §1.2 floor controller, as logged (classic brain): its matched-player reference at the end, and its mean output.
		float   ShadowScriptSpm = 0.f;
		float   PressureBiasSum = 0.f;     // summed over decisions
		float   FloorCorrectionSum = 0.f;  // summed over decisions

		float SwingsPerMin() const { return Frames > 0 ? BossSwings * 3600.f / static_cast<float>(Frames) : 0.f; }
		float DamagePerMin() const { return Frames > 0 ? PlayerDamageTaken * 3600.f / static_cast<float>(Frames) : 0.f; }
	};

	/** A2: one CSV row per exchange (a boss decision and what became of it). */
	struct FExchangeRow
	{
		FBrainDecision Decision;
		EHitOutcome    Outcome = EHitOutcome::None;
		int32_t        OutcomeFrame = -1;
		int32_t        FrameAdvantageAfter = 0;
		float          Damage = 0.f;
		float          PlayerHealth = 0.f;
		float          BossHealth = 0.f;
		float          SwingsPerMin = 0.f;
		float          DamagePerMin = 0.f;
	};
	/** CSV header + formatter shared by the simulator and the game (A2 must reconcile against the overlay). */
	const char* ExchangeCsvHeader();
	void FormatExchangeRow(const FExchangeRow& R, const char* RunId, EBrainMode Mode, float MeanFrameMs, char* Out, int32_t OutSize);

	class FEncounter
	{
	public:
		FDuel           Duel;
		FEncounterStats Stats;

		bool bRecordRows = true;
		bool bImmortal = false; // rate-measurement mode: nobody dies, would-be deaths are counted

		/** ResetEncounter(): duel reset, the brain begins a new fight (its session memory persists — PLAN §2.3). */
		void Begin(IBossBrain* InBrain, int32_t Seed, bool bInImmortal = false);

		/** Step 1 of a frame. Returns true if the brain made a decision this frame. */
		bool ThinkBoss(const FDuelGeometry& Geo);
		/** Step 3 of a frame. PlayerMovement: Neutral / Advance / Retreat from locomotion this frame. */
		void StepFrame(IContactOracle& Oracle, ESym PlayerMovement);

		IBossBrain* Brain() const { return BossBrain; }
		const std::vector<FDuelEvent>& FrameEvents() const { return Events; }
		const FBrainDecision& LastDecision() const;
		bool PopReadMeter(FReadMeterEvent& Out) { return BossBrain != nullptr && BossBrain->PopReadMeter(Out); }
		/** Completed exchange rows since the last call. */
		void TakeRows(std::vector<FExchangeRow>& Out) { Out.swap(Rows); Rows.clear(); }
		bool IsOver() const { return !bImmortal && Duel.IsOver(); }

	private:
		void OpenRow(const FBrainDecision& D);
		void CloseRow();

		IBossBrain* BossBrain = nullptr;
		FDuelGeometry Geometry;      // as of this frame's ThinkBoss; handed to the brain after the step
		std::vector<FDuelEvent> Events;
		std::vector<FExchangeRow> Rows;
		FExchangeRow OpenExchange;
		bool bRowOpen = false;
		EMoveId LastBossMove = EMoveId::None;
	};
}
