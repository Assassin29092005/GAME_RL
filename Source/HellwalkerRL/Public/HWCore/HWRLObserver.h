// HellwalkerRL — engine-free core. The RL keeper's senses and hands (RL.md §3.2 - §3.4, §4.5).
//
// FRLObserver is shared VERBATIM by the training environment (RL/native, actions from Python) and the in-game brain
// (FRLBrain, actions from the C++ forward pass), so the keeper sees, decides and acts identically in both:
//   * WHEN it decides  — IsDecisionPoint(): the keeper is actionable (end of a move, a cancel window, after a stun)
//                        and at least DecisionGapFrames have passed since its last decision.
//   * WHAT it sees     — BuildObservation(): its own state; the player's VISIBLE state PerceptionFrames late; the
//                        session's history tokens (perceived exchanges). Never the player's inputs.
//   * WHAT it may do   — BuildMask(): legality + the fairness rules as hard masks (never punished, never allowed).
//   * HOW it acts      — ApplyAction(): commit the move (or wait), open the exchange token.
//   * the read head's training LABEL — AnswerToLastDecision(): what the player actually did (ground truth).
//   * READ banner      — a read counter (a hit on the answer the read head predicted, confidently and correctly).
//
// Frame protocol (the same order FEncounter uses):
//   BeginEncounter(Session, Duel, Geo)                          once per fight, after FDuel::Reset
//   per frame:  [if IsDecisionPoint(Duel): BuildObservation / BuildMask / ApplyAction(Duel, A, Read)]
//               <player input> <FDuel::Step>  RecordFrame(Events, Duel, Geo, PlayerMovement)

#pragma once

#include "HWBrain.h"
#include "HWRLTypes.h"

#include <vector>

namespace HW
{
	/** Runtime knobs (not part of the layout contract). Skill and identity are read at BeginEncounter (fixed for a fight). */
	struct FRLConfig
	{
		/** Aggression target (swings/min) at skill 1 — ObsSwingDeficit measures against RL::TargetSwingsPerMin(this, Skill);
		 *  the trainer's constraint uses the same value. */
		float TargetSwingsPerMin = 66.f;
		/** The READ banner needs the read head at least this sure of the answer it predicted. */
		float ReadMeterMinP = RL::ReadMeterMinP;
		/** Difficulty in [0, 1] (RL::SkillParams): 1 = the Hellwalker tier. An input of the network (ObsSkill). */
		float Skill = 1.f;
		/** Which keeper (RL::EKeeper): an input of the network (ObsIdentity). */
		int32_t Identity = 0;
		/** The game's Easy breather (RL::EasySwingGap): an attack that opens a string needs at least this many frames since
		 *  the keeper's last attack COMMIT (so the rest after a slow attack is shorter); 0 = off. Never set in training and
		 *  not an input of the network (unlike the grab / killer cooldowns, which are observed): the policy is unchanged, but
		 *  it meets a mask it never trained with, so its effect is measured (eval.py ladder), not assumed. The swing-deficit
		 *  input's target is capped at what the breather allows (BeginEncounter). */
		int32_t MinSwingGap = 0;
	};

	/**
	 * What the keeper keeps about YOU for a whole session (RL.md §3.2: carried from keeper to keeper, dropped on quit):
	 * the recurrent memory and the perceived exchange history. The trainer keeps the recurrent state on its side
	 * (batched on the GPU); the in-game brain keeps it here.
	 */
	struct FRLSession
	{
		static constexpr int32_t MaxHidden = RL::MaxHidden;
		float   Hidden[MaxHidden] = {};
		int32_t HiddenSize = 0;          // 0 = fresh (all zeros)
		int8_t  Tokens[RL::HistoryTokens][RL::TokenFields] = {}; // newest first; all-zero rows = padding
		int32_t NumTokens = 0;
		int32_t FightsBegun = 0;         // FRLObserver::BeginEncounter: FightIndex = FightsBegun++
		int32_t FightIndex = 0;          // 0 for the first fight of the session
		int32_t BossSwingsSeen = 0;      // boss attacks committed in the session (habit-switch schedules, telemetry)

		void Reset();
		/** Newest first; the oldest falls off the end. */
		void PushToken(const int8_t* Token);
	};

	/** What the read head said about the action being taken (for the READ banner and the decision record). */
	struct FRLRead
	{
		ESym  Predicted = ESym::Neutral;
		float P = 0.f;
	};

	class FRLObserver
	{
	public:
		FRLConfig Config;

		/** Start a fight. Session must outlive the encounter. Finalises any tokens left open by the previous fight. */
		void BeginEncounter(FRLSession* InSession, const FDuel& Duel, const FDuelGeometry& Geo);

		bool IsDecisionPoint(const FDuel& Duel) const;

		/** Out: RL::ObsDim floats. Tokens: RL::HistoryTokens x RL::TokenFields int8 (newest first, zero-padded). */
		void BuildObservation(const FDuel& Duel, float* OutObs, int8_t* OutTokens) const;
		/** Out: RL::NumActions bytes, 1 = allowed. Wait is always allowed. */
		void BuildMask(const FDuel& Duel, uint8_t* OutMask) const;

		/**
		 * Commit action A (an index into the action space) on the duel at this decision point. A masked or failing
		 * action degrades to Wait (and is counted). Re-choosing the locomotion move the keeper is already running
		 * continues it (no re-commit). Read: the read head's prediction for this action (may be null).
		 * Returns the move committed (None for Wait / continue).
		 */
		EMoveId ApplyAction(FDuel& Duel, int32_t A, const FRLRead* Read = nullptr);

		/** After FDuel::Step: this frame's events, the geometry at the start of the frame, the player's walk symbol. */
		void RecordFrame(const std::vector<FDuelEvent>& Events, const FDuel& Duel, const FDuelGeometry& Geo, ESym PlayerMovement);

		/**
		 * Ground truth (training label, NOT an input): the player's answer to the most recent decision's action, from
		 * everything that happened since that decision. -1 before the first decision. RL::NumAnswerClasses classes.
		 */
		int32_t AnswerToLastDecision() const;

		/** READ banner: a read counter landed since the last call (see FReadMeterEvent). */
		bool PopReadMeter(FReadMeterEvent& Out);
		int32_t ReadCountersLanded() const { return ReadCounters; }

		// Telemetry for the environment's reward / cost bookkeeping.
		/** The keeper's own attack rate over the last 30 s of this fight (at least a 10 s span), swings/min. */
		float SwingsPerMinute(int32_t Frame) const;
		int32_t AttacksCommitted() const { return Attacks; }      // this fight
		int32_t DecisionsMade() const { return Decisions; }        // this fight
		int32_t IllegalActions() const { return Illegal; }         // masked actions that were requested anyway
		int32_t LastDecisionFrame() const { return LastDecision; }
		int32_t LastAction() const { return LastActionIndex; }
		/** The skill's perception delay, decision gap, string length and cooldowns for this fight. */
		const RL::FSkillParams& Params() const { return SkillP; }
		/** The aggression target for this fight (Config.TargetSwingsPerMin at the fight's skill). */
		float TargetSwingsPerMin() const { return Target; }

	private:
		struct FSnap
		{
			int32_t Frame = 0;
			float PX = 0.f, PY = 0.f, BX = 0.f, BY = 0.f;   // simulator handedness (mirrored if Geo.bMirrorY)
			EFighterState State = EFighterState::Idle;
			EMoveId Move = EMoveId::None;
			int32_t T = 0;
			int32_t UntilActionable = 0;
			bool bGuardHeld = false, bGuarding = false, bParryLive = false, bInvulnerable = false, bArmor = false;
			int32_t Weapon = 0;
			int32_t ChainDepth = 0;
			float Health = 0.f, HealthMax = 1.f, ShaChi = 0.f, ShaChiMax = 1.f;
		};
		struct FOpenToken
		{
			int32_t OpenFrame = 0;         // the decision frame
			int32_t CloseFrame = -1;       // the next decision's frame (-1 = still the current decision)
			int8_t  Boss = 0;              // TokBoss value
			EMoveId Move = EMoveId::None;  // the boss move committed (None = Wait / continue)
			int32_t MoveCommitFrame = -1;
			int32_t AnswerFrame = -1;      // first player commitment in the window
			ESym    Answer = ESym::Count;  // Count = none yet
			EHitOutcome Outcome = EHitOutcome::None;
			bool bDealt = false, bTaken = false;
			bool bGuardSeen = false;       // player guard held during the window (answer fallback: Block)
			ESym Movement = ESym::Neutral; // the player's walk symbol at the last frame seen (answer fallback)
			FRLRead Read;                  // what the read head predicted (READ banner)
		};

		FSnap SnapAt(int32_t Frame) const;        // perceived snapshot (clamped to what was recorded)
		void Snapshot(const FDuel& Duel, const FDuelGeometry& Geo, int32_t Frame);
		void FinaliseTokens(int32_t Now, bool bAll);
		static void EncodeToken(const FOpenToken& T, int8_t* Out);
		static ESym AnswerOf(const FOpenToken& T);

		// Fixed capacity everywhere: thousands of observers run side by side in training, and nothing may grow with
		// the length of a fight.
		static constexpr int32_t SnapRing = 32;    // > MaxPerceptionFrames + the velocity window
		static_assert(SnapRing > RL::MaxPerceptionFrames + 4 + 1, "the snapshot ring must cover the slowest skill's perception");
		static constexpr int32_t EventRing = 64;   // player events awaiting perception
		static constexpr int32_t MaxOpen = 8;      // tokens not yet perceivable in full
		static constexpr int32_t SwingRing = 128;  // own attack commits (the 30 s swing-rate window: chained fast slashes
		                                           // can commit every 22 frames, ~82 in 1800 frames, so 64 could undercount)

		FRLSession* Session = nullptr;
		RL::FSkillParams SkillP;                   // this fight's (from Config.Skill at BeginEncounter)
		float   Target = 66.f;                     // this fight's aggression target
		int32_t Identity = 0;                      // this fight's keeper (from Config.Identity, clamped)
		int32_t MinGap = 0;                        // this fight's breather (from Config.MinSwingGap)
		bool    bMirrorY = false;                  // the last geometry's handedness (the keeper's commit facing is mirrored too)
		FSnap   Snaps[SnapRing];                   // Snaps[F % SnapRing] = the state at the START of frame F
		int32_t FirstSnapFrame = 0;                // oldest frame still recorded this fight
		int32_t LatestSnapFrame = -1;
		FDuelEvent PendingPlayerEvents[EventRing]; // player commits / player swing outcomes, in order, until perceived
		int32_t PendingHead = 0, PendingCount = 0;
		EHitOutcome PerceivedPlayerOutcome = EHitOutcome::None; // the player's last swing outcome, as perceived
		int32_t PerceivedPlayerCommitFrame = -100000;           // the player's last commitment, as perceived
		FOpenToken Open[MaxOpen];                  // oldest first
		int32_t NumOpen = 0;
		int32_t SwingFrames[SwingRing] = {};
		int32_t SwingHead = 0, SwingCount = 0;

		int32_t NextDecisionFrame = 0;
		int32_t LastDecision = -1;
		int32_t LastActionIndex = -1;
		int32_t StringAttacks = 0;                 // attacks in the current string
		int32_t LastGrabFrame = -100000;
		int32_t LastKillerFrame = -100000;
		int32_t LastOwnOutcomeFrame = -1;
		EHitOutcome LastOwnOutcome = EHitOutcome::None;
		bool    bDefendedSinceDecision = false;
		int32_t LastOwnSwingFrame = -100000;
		int32_t Attacks = 0;
		int32_t Decisions = 0;
		int32_t Illegal = 0;
		int32_t ReadCounters = 0;
		bool    bReadMeterReady = false;
		FReadMeterEvent ReadMeter;
	};

	/**
	 * "No faster-than-scripted adaptive attacks" (RL.md §4.5), derived from the boss scripts (not hand-picked): in an
	 * attack's cancel window, a chained attack must have at least the startup of the fastest attack any script chains
	 * after a move of the same symbol class. Returns a large number if no script chains after that class.
	 */
	int32_t ChainStartupFloor(ESym PreviousAttackSymbol);
}
