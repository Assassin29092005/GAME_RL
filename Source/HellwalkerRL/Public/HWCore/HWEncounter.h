// Hellwalker — engine-free core. One encounter: duel + symbol observer + boss brain + telemetry.
//
// Frame order, identical in Unreal and in the simulator:
//   1. ThinkBoss(Distance)     — the brain may commit; it sees nothing the player pressed THIS frame
//   2. <caller applies player input to Duel>  (timestamped input, PLAN §5.2)
//   3. StepFrame(Oracle, Move) — resolve contacts, advance, dispatch events to model / brain / stats
//
// The observer implements PLAN §2.1's cadence: a symbol at every commitment event, a movement symbol
// when a boss damage window resolves with no player commitment, and a 45-frame watchdog.

#pragma once

#include "HWBossBrain.h"

#include <vector>

namespace HW
{
	struct FSymbolRecord
	{
		ESym    Sym = ESym::Neutral;
		int32_t Frame = 0;
		int32_t Lead = FPlaystyleModel::NoTiming;
	};

	class FSymbolObserver
	{
	public:
		static constexpr int32_t WatchdogFrames = 45;
		/**
		 * Timing samples (the sidecar's "when") are only taken when the impact the player timed to lands at least
		 * this far after the boss commits (~300 ms). Against a faster wind-up the press time is set by the
		 * player's reaction, not chosen: a parry that comes 1 frame late against a 12-frame slash says nothing
		 * about when this player presses against a 24-frame heavy — and averaging the two (with feint reads
		 * measured from the fake) made the brain believe a player who parries on time "presses late", and throw
		 * fast attacks and heavies into their parry (B0 net-exchange finding).
		 */
		static constexpr int32_t TimingHorizonFrames = 18;

		void Reset();
		/** Process one frame's events, in order, then the watchdog. Emits into Model (may be null). */
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
		int32_t DecisionKinds[8] = {};   // indexed by EDecisionKind
		int32_t CountersLanded = 0;
		int32_t Symbols = 0;
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
		float   BossDamageAfterKind[8] = {};           // ... and to the last decision's kind (EDecisionKind)
		float   PlayerDamageByKind[8] = {};
		int32_t ArgmaxViolations = 0;      // C3
		// §1.2 floor controller, as logged: its matched-player reference at the end, and its mean output.
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
		FSymbolObserver Observer;
		FBossBrain      Brain;
		FEncounterStats Stats;

		bool bRecordSymbols = false;
		bool bRecordRows = true;
		bool bImmortal = false; // rate-measurement mode: nobody dies, would-be deaths are counted

		/** ResetEncounter(): duel + brain reset, seeded; the session model persists (PLAN §2.3). */
		void Begin(FPlaystyleModel* SessionModel, EBrainMode Mode, int32_t Seed, bool bInImmortal = false);

		/** Step 1 of a frame. Returns true if the brain made a slot decision this frame. */
		bool ThinkBoss(float Distance);
		/** Step 3 of a frame. PlayerMovement: Neutral / Advance / Retreat from locomotion this frame. */
		void StepFrame(IContactOracle& Oracle, ESym PlayerMovement);

		const std::vector<FDuelEvent>& FrameEvents() const { return Events; }
		const FBrainDecision& LastDecision() const { return Brain.LastDecision(); }
		std::vector<FSymbolRecord>& SymbolLog() { return Symbols; }
		/** Completed exchange rows since the last call. */
		void TakeRows(std::vector<FExchangeRow>& Out) { Out.swap(Rows); Rows.clear(); }
		bool IsOver() const { return !bImmortal && Duel.IsOver(); }

	private:
		void OpenRow(const FBrainDecision& D);
		void CloseRow();

		FPlaystyleModel* Model = nullptr;
		std::vector<FDuelEvent> Events;
		std::vector<FSymbolRecord> Symbols;
		std::vector<FExchangeRow> Rows;
		FExchangeRow OpenExchange;
		bool bRowOpen = false;
		EMoveId LastBossMove = EMoveId::None;
	};
}
