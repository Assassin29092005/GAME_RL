// HellwalkerRL — tools only. RL-4, the exploiter league: a PLAYER-side agent whose only goal is to beat the frozen keeper
// (RL.md §6: "exploiters find the holes the population never pokes; they join the population").
//
// FPlayerAgent is the exploiter's senses and hands, shared VERBATIM by
//   * the exploiter's training env (FRLPlayerEnv: decisions from Python, the keeper frozen in C++), and
//   * the keeper's training env (FRLEnv, kind-7 players: FExploiterDriver runs the trained policy in C++),
// so the exploiter plays in the keeper's population exactly as it was trained.
//
// Fairness, mirrored from the keeper's: the player sees its own body now and the KEEPER's body PlayerPerceptionFrames
// late (a human reading animations), never the keeper's inputs or its network. It decides every PlayerDecisionGap
// frames (it may hold guard / walk between commitments); commitments obey FDuel's rules (actionable, sha-chi).

#pragma once

#include "HWCore/HWDuel.h"
#include "HWCore/HWRandom.h"
#include "HWCore/HWRLPolicy.h"
#include "HWCore/HWRLTypes.h"
#include "HWCore/HWSim.h"

#include <vector>

namespace HW
{
	namespace RLPlayer
	{
		inline constexpr int32_t PerceptionFrames = 10;  // ~167 ms: it reads the keeper's animation like a person
		inline constexpr int32_t DecisionGapFrames = 6;   // as often as the keeper decides (and its forward pass is the cost)

		/** The player's actions. Walks and Guard persist until the next decision; the rest are commitments. */
		enum EAction : int32_t
		{
			ActHold = 0, ActWalkF, ActWalkB, ActWalkL, ActWalkR, ActGuard,
			ActLight, ActHeavy, ActParry, ActStepF, ActStepB, ActStepL, ActStepR, ActSwitch,
			NumActions
		};
		const char* ActionName(int32_t A);

		/** The observation (floats, normalised). */
		enum EObs : int32_t
		{
			// ---- SELF, current (42)
			ObsHealth = 0, ObsShaChi,
			ObsState,                                // +7 EFighterState
			ObsMove = ObsState + 7,                  // +13 PLight1 .. PSwitch
			ObsPhase = ObsMove + 13,                 // +4 attack startup / active / recovery / other
			ObsProgress = ObsPhase + 4,
			ObsUntilActionable,                      // / 60, [0, 2]
			ObsGuardHeld, ObsGuarding, ObsParryLive, ObsInvulnerable,
			ObsWeapon,                               // +2
			ObsChain = ObsWeapon + 2,                // / 3
			ObsLastOutcome,                          // +5 its own last swing's outcome
			ObsWalkF = ObsLastOutcome + 5, ObsWalkL, // the walk intent it is holding
			// ---- KEEPER, perceived PerceptionFrames late (44)
			ObsKHealth, ObsKShaChi,
			ObsKState,                               // +7
			ObsKMove = ObsKState + 7,                // +22 the keeper's moves (the RL action order)
			ObsKPhase = ObsKMove + RL::NumBossMoves, // +4
			ObsKProgress = ObsKPhase + 4,
			ObsKUntilActionable,                     // perceived minus the delay, / 60, [0, 2]
			ObsKArmor, ObsKInvulnerable,
			ObsKLastOutcome,                         // +5 the keeper's last swing's outcome, as perceived
			// ---- GEOMETRY (5)
			ObsDistance = ObsKLastOutcome + 5,       // / 500, [0, 3]
			ObsRadialVel, ObsLateralVel,             // the keeper's motion, perceived (cm/frame / 10, [-2, 2])
			ObsSwingSin, ObsSwingCos,                // where the keeper's committed swing points relative to the player (0, 1 if none)
			// ---- CONTEXT (8)
			ObsFrameAdvantage,                       // / 30, [-2, 2]
			ObsFightTime, ObsFightIndex,
			ObsKSkill,
			ObsKIdentity,                            // +3
			ObsSinceKCommit = ObsKIdentity + RL::NumKeepers, // frames since the keeper's last commitment (perceived) / 60, [0, 2]
			ObsDim
		};
		static_assert(ObsDim == 99, "player observation layout changed: retrain the exploiters");
		const char* ObsFeatureName(int32_t I);
	}

	/** The exploiter's senses and hands (see the file comment). One per fight; Begin per fight. */
	class FPlayerAgent
	{
	public:
		void BeginFight(const FDuel& Duel, const FDuelGeometry& Geo, float KeeperSkill, int32_t KeeperIdentity, int32_t FightIndex);
		bool IsDecisionPoint(const FDuel& Duel) const;
		void BuildObservation(const FDuel& Duel, const FDuelGeometry& Geo, float* Out) const;
		void BuildMask(const FDuel& Duel, uint8_t* Out) const;
		/** Apply an action at a decision point (masked -> Hold). Returns false if it degraded. */
		bool ApplyAction(FDuel& Duel, const FSimArena& Arena, int32_t A);
		/** Every frame, after any decision and before FDuel::Step: hold the guard state. */
		void HoldState(FDuel& Duel) const;
		/** After FDuel::Step: this frame's events, the geometry at the start of the frame. */
		void RecordFrame(const std::vector<FDuelEvent>& Events, const FDuel& Duel, const FDuelGeometry& Geo);
		float WalkForward() const { return Fwd; }
		float WalkLateral() const { return Lat; }
		ESym MovementSym() const;
		int32_t IllegalActions() const { return Illegal; }

	private:
		struct FSnap
		{
			int32_t Frame = -1;
			float PX = 0.f, PY = 0.f, BX = 0.f, BY = 0.f;
			EFighterState State = EFighterState::Idle;
			EMoveId Move = EMoveId::None;
			int32_t T = 0;
			int32_t UntilActionable = 0;
			bool bArmor = false, bInvulnerable = false;
			float CommitFacingX = 1.f, CommitFacingY = 0.f;
			float Health = 0.f, HealthMax = 1.f, ShaChi = 0.f, ShaChiMax = 1.f;
		};
		static constexpr int32_t SnapRing = 32;
		static constexpr int32_t EventRing = 32;
		FSnap SnapAt(int32_t Frame) const;
		void Snapshot(const FDuel& Duel, const FDuelGeometry& Geo, int32_t Frame);

		FSnap Snaps[SnapRing];
		int32_t FirstSnap = 0, LatestSnap = -1;
		FDuelEvent Pending[EventRing];
		int32_t PendingHead = 0, PendingCount = 0;
		EHitOutcome PerceivedKOutcome = EHitOutcome::None;
		int32_t PerceivedKCommit = -100000;
		EHitOutcome OwnOutcome = EHitOutcome::None;
		float Skill = 1.f;
		int32_t Identity = 0;
		int32_t Fight = 0;
		int32_t NextDecision = 0;
		bool bGuard = false;
		float Fwd = 0.f, Lat = 0.f;
		int32_t Illegal = 0;
	};

	/** A trained exploiter playing the player in the keeper's env (kind 7): FPlayerAgent + a side-1 policy, sampled. */
	class FExploiterDriver
	{
	public:
		/** Policy: shared, const, side 1, player layout (checked: false = refuse). */
		bool Bind(const FRLPolicy* InPolicy);
		bool IsBound() const { return Policy != nullptr; }
		/** A new session: fresh recurrent state, the session's random stream. */
		void BeginSession(int32_t Seed);
		void BeginFight(const FDuel& Duel, const FDuelGeometry& Geo, float KeeperSkill, int32_t KeeperIdentity, int32_t FightIndex);
		/** The frame's input (between the keeper's think and the step): decide when due, hold guard. Walk intent out. */
		void Act(FDuel& Duel, const FSimArena& Arena, float& OutFwd, float& OutLat);
		void RecordFrame(const std::vector<FDuelEvent>& Events, const FDuel& Duel, const FDuelGeometry& Geo) { Agent.RecordFrame(Events, Duel, Geo); }
		ESym MovementSym() const { return Agent.MovementSym(); }

	private:
		const FRLPolicy* Policy = nullptr;
		FPlayerAgent Agent;
		FRandom Rng;
		std::vector<float> Hidden;
		std::vector<float> Scratch;
		float Obs[RLPlayer::ObsDim] = {};
		uint8_t Mask[RLPlayer::NumActions] = {};
	};
}
