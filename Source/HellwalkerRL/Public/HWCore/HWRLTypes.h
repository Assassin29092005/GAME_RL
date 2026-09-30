// HellwalkerRL — engine-free core. The RL keeper's fixed layouts (RL.md §3): action space, observation vector,
// history tokens, the player-answer classes of the read head, and the fairness constants.
//
// These numbers are a CONTRACT between the C++ core (observer, policy forward pass, environment) and the Python
// trainer (RL/). Changing any of them changes ObsLayoutVersion; the weights file records the version it was trained
// with and the loader refuses a mismatch.

#pragma once

#include "HWMoves.h"

namespace HW
{
	namespace RL
	{
		// Bump whenever the observation / token / action layout below changes meaning.
		inline constexpr int32_t ObsLayoutVersion = 2; // 2: + the killer move's cooldown (ObsSelfKillerCooldown)

		// ------------------------------------------------------------------------------------------------------
		// Actions: Discrete(23) = the 22 boss moves of EMoveId (BFastSlash .. BRetreat, in enum order) + Wait.
		// ------------------------------------------------------------------------------------------------------
		inline constexpr int32_t FirstBossMove = static_cast<int32_t>(EMoveId::BFastSlash);
		inline constexpr int32_t NumBossMoves = static_cast<int32_t>(EMoveId::Count) - FirstBossMove; // 22
		inline constexpr int32_t ActionWait = NumBossMoves;                                           // 22
		inline constexpr int32_t NumActions = NumBossMoves + 1;                                       // 23
		static_assert(NumBossMoves == 22, "RL.md fixes the boss's action space at 22 moves + Wait");

		inline constexpr EMoveId ActionMove(int32_t A) { return (A >= 0 && A < NumBossMoves) ? static_cast<EMoveId>(FirstBossMove + A) : EMoveId::None; }
		inline constexpr int32_t MoveAction(EMoveId M)
		{
			const int32_t I = static_cast<int32_t>(M) - FirstBossMove;
			return (I >= 0 && I < NumBossMoves) ? I : -1;
		}
		/** "Move" name for an action index (the move's name, or "Wait"). */
		const char* ActionName(int32_t A);

		// ------------------------------------------------------------------------------------------------------
		// Cadence and fairness (RL.md §3.2, §4.5)
		// ------------------------------------------------------------------------------------------------------
		inline constexpr int32_t PerceptionFrames = 6;     // everything about the player is seen this many frames late
		inline constexpr int32_t DecisionGapFrames = 6;    // min frames between two decisions; also the length of a Wait
		inline constexpr float   ReachSlack = 60.f;        // an attack is legal only if (perceived) distance <= Range + this
		inline constexpr int32_t MaxStringAttacks = 3;     // the scripts' longest string: 3 attacks, then a non-attack
		inline constexpr int32_t GrabCooldownFrames = 180; // no re-grab within 3 s (no lock-down loops)
		// The killer thrust (unblockable, red telegraph) at most once per 10 s: RL-1 found "drain the guard with heavy
		// sweeps, then killer on an empty guard" and spammed it (161 of 289 decisions). The scripts throw it once a cycle.
		inline constexpr int32_t KillerCooldownFrames = 600;
		inline constexpr float   ReadMeterMinP = 0.55f;    // the READ banner needs the read head this sure (and right)

		// ------------------------------------------------------------------------------------------------------
		// The read head's classes: the 12 player symbols (ESym Neutral .. Switch). Label = the player's answer to
		// the boss action taken at a decision: the first player commitment (its symbol) after the decision and
		// before the next one; if none, Block when guard was held through it, else the movement symbol.
		// ------------------------------------------------------------------------------------------------------
		inline constexpr int32_t NumAnswerClasses = NumPlayerSymbols; // 12

		// ------------------------------------------------------------------------------------------------------
		// History tokens: the last HistoryTokens exchanges, newest first (index 0), each TokenFields small integers.
		// 0 in every field = padding (no token). One token per boss decision (a Wait extends the previous Wait token),
		// built only from what was PERCEIVED (events at least PerceptionFrames old), kept for the whole session.
		// ------------------------------------------------------------------------------------------------------
		inline constexpr int32_t HistoryTokens = 32;
		inline constexpr int32_t TokenFields = 6;
		enum ETokenField : int32_t
		{
			TokBoss = 0,     // 1..8 = boss symbol of the move (BFast .. BRetreat), 9 = Wait
			TokAnswer = 1,   // 1..12 = the player's answer symbol + 1 (Neutral .. Switch)
			TokOutcome = 2,  // 1 none (not an attack / unresolved), 2 hit, 3 whiff, 4 blocked, 5 parried  (the boss's swing)
			TokExchange = 3, // 1 nobody hurt, 2 boss dealt damage, 3 boss took damage, 4 both
			TokTiming = 4,   // 1 no timing sample; 2..9 lead bucket of the answer vs the PERCEIVED impact (see LeadBucket)
			TokBite = 5,     // 1 not a bait; 2 pressed at the bait (bit); 3 waited for the real strike
		};
		inline constexpr int32_t TokenVocab[TokenFields] = { 10, 13, 6, 5, 10, 4 };
		/** Lead (frames the answer was pressed BEFORE the perceived impact; negative = after) -> 2..9. */
		int32_t LeadBucket(int32_t LeadFrames);

		// ------------------------------------------------------------------------------------------------------
		// The observation vector (floats, already normalised). Blocks, in order:
		//   SELF    — the keeper's own state (current: it knows its own body)
		//   PLAYER  — the player's VISIBLE state, delayed by PerceptionFrames (animation reading, never input reading)
		//   CONTEXT — frame advantage from what it perceives, fight clock, session position, aggression budget
		// ------------------------------------------------------------------------------------------------------
		enum EObs : int32_t
		{
			// ---- SELF (51)
			ObsSelfHealth = 0,                         // health / max
			ObsSelfShaChi,                             // sha-chi / max
			ObsSelfState,                              // +7 one-hot EFighterState
			ObsSelfMove = ObsSelfState + 7,            // +22 one-hot current boss move (all 0 when not Acting)
			ObsSelfPhase = ObsSelfMove + NumBossMoves, // +4 one-hot: attack startup / attack active / attack recovery / other move
			ObsSelfProgress = ObsSelfPhase + 4,        // T / TotalFrames of the current move (0 when not Acting)
			ObsSelfUntilActionable,                    // FramesUntilActionable / 60, clipped to [0, 2]
			ObsSelfArmor,                              // hyper armor now
			ObsSelfInvulnerable,                       // i-frames now
			ObsSelfStun,                               // StunLeft / 90, clipped to [0, 1]
			ObsSelfLastOutcome,                        // +5 one-hot last own swing outcome: none / hit / whiff / blocked / parried
			ObsSelfSinceOutcome = ObsSelfLastOutcome + 5, // frames since that outcome / 120, clipped to [0, 1] (1 if none)
			ObsSelfDefended,                           // own guard / counter caught a player swing since the last decision
			ObsSelfString,                             // attacks in the current string / MaxStringAttacks
			ObsSelfGrabCooldown,                       // grab cooldown left / GrabCooldownFrames
			ObsSelfKillerCooldown,                     // killer thrust cooldown left / KillerCooldownFrames
			ObsSelfChainWindow,                        // acting in an attack's cancel window (a chain is possible now)
			// ---- PLAYER, perceived (47)
			ObsPlayerDistance,                         // distance / 500, clipped to [0, 3]
			ObsPlayerRadialVel,                        // closing speed, cm/frame / 10, clipped to [-2, 2] (+ = approaching)
			ObsPlayerLateralVel,                       // sideways speed, cm/frame / 10, clipped to [-2, 2] (+ = to the player's own left)
			ObsPlayerBearingSin,                       // bearing of the player from the keeper's committed facing (sin, cos)
			ObsPlayerBearingCos,
			ObsPlayerHealth,
			ObsPlayerShaChi,
			ObsPlayerState,                            // +7 one-hot EFighterState
			ObsPlayerMove = ObsPlayerState + 7,        // +13 one-hot current player move (PLight1 .. PSwitch), 0 when not Acting
			ObsPlayerPhase = ObsPlayerMove + 13,       // +4 one-hot: attack startup / active / recovery / other move
			ObsPlayerProgress = ObsPlayerPhase + 4,
			ObsPlayerUntilActionable,                  // perceived FramesUntilActionable minus the delay, / 60, clipped [0, 2]
			ObsPlayerGuardHeld,
			ObsPlayerGuarding,                         // guard effective (raised long enough)
			ObsPlayerParryLive,
			ObsPlayerInvulnerable,
			ObsPlayerArmor,
			ObsPlayerWeapon,                           // +2 one-hot twin blades / glaive
			ObsPlayerChain = ObsPlayerWeapon + 2,      // light-chain depth / 3
			ObsPlayerLastOutcome,                      // +5 one-hot the player's last swing outcome (perceived)
			ObsPlayerSinceCommit = ObsPlayerLastOutcome + 5, // frames since the player's last commitment / 60, clipped [0, 2]
			// ---- CONTEXT (5)
			ObsFrameAdvantage,                         // keeper's frame advantage from what it perceives / 30, clipped [-2, 2]
			ObsFightTime,                              // seconds into the fight / 180, clipped [0, 1]
			ObsFightIndex,                             // fight index in the session / 3, clipped [0, 1]
			ObsSwingDeficit,                           // (target swings/min - own swings/min over the last 30 s) / 60, clipped [-1, 1]
			ObsSinceOwnSwing,                          // frames since the keeper's last attack commit / 300, clipped [0, 1]
			ObsDim
		};
		static_assert(ObsDim == 103, "observation layout changed: bump ObsLayoutVersion and retrain");
		/** Human-readable name of observation feature I (debugging, the F3 overlay, RL/ logs). */
		const char* ObsFeatureName(int32_t I);
	}
}
