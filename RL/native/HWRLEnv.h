// HellwalkerRL — tools only. The RL training environment (RL.md §3): the real combat rules (FDuel via FEncounter), a
// simulated player from the population (FPlayerBot: the reference bots or a procedural habit player, learning or not; or
// a trained exploiter, RL-4), and the keeper's decision loop — paused at every decision point for an action from the
// trainer.
//
// The keeper's senses / masks / action application are the game's own FRLObserver, so the policy the trainer shapes
// here is exactly the one the game runs. Nothing in this folder is compiled into the game.
//
// Episode = one fight (death, or the timeout). Session = fights_in_session fights against the same player; the keeper's
// memory (the trainer's recurrent state + the observer's history tokens) is carried across the fights of a session
// and reset between sessions. Fights and sessions roll over automatically inside Step(). Each session also fixes the
// keeper's side of the fight: its skill and identity (the spec's keeper_skill / keeper_identity).
//
// Also here: FRLPlayerEnv (RL-4: the EXPLOITER's training env — decisions for the player, the keeper frozen in C++),
// C++-side evaluation (EvalSessions, EvalAttackLog), and the worker pool both batches share.

#pragma once

#include "hwrl_capi.h"

#include "HWCore/HWEncounter.h"
#include "HWCore/HWRLBrain.h"
#include "HWCore/HWRLObserver.h"
#include "HWCore/HWRLPolicy.h"
#include "HWCore/HWSim.h"
#include "HWRLPlayer.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace HW
{
	/** A barrier-style worker pool: ParallelFor(N, Fn) runs Fn(0..N-1) on the pool and the calling thread and returns
	 *  when all are done. Each index is run by exactly one thread; results never depend on the thread count. */
	class FWorkerPool
	{
	public:
		explicit FWorkerPool(int32_t Threads);
		~FWorkerPool();
		FWorkerPool(const FWorkerPool&) = delete;
		FWorkerPool& operator=(const FWorkerPool&) = delete;
		void ParallelFor(int32_t N, const std::function<void(int32_t)>& Fn);
		int32_t Threads() const { return NumThreads; }

	private:
		struct FImpl;
		std::unique_ptr<FImpl> Impl;
		int32_t NumThreads = 1;
	};

	/** One step's results (see HWRLStepOut for the meaning of each field). */
	struct FRLStepResult
	{
		float   Reward = 0.f;
		float   Cost = 0.f;
		bool    bFightDone = false;
		bool    bSessionDone = false;
		uint8_t Result = 0;
		int8_t  AuxLabel = -1;
		int32_t Frames = 0;
		int32_t Swings = 0;
		int32_t Hits = 0;
		float   DmgDealt = 0.f;
		float   DmgTaken = 0.f;
		bool    bReadCounter = false;
		int32_t Tag = 0;
		int32_t FightIndex = 0;
		int32_t HabitPhase = 0;
		int32_t Identity = 0;
		float   Skill = 1.f;
		float   Style = 0.f;
		uint8_t StyleEvents[4] = {}; // feint bites, evasions, guard breaks, guard-pressure blocks
	};

	/** The player profile a spec describes (reference profile from skill, overrides, habit tables, learning). */
	FBotProfile MakeProfileFromSpec(const HWRLPlayerSpec& Spec);
	/** The keeper's skill / identity of a spec (defaults: 1, Warden). */
	float SpecKeeperSkill(const HWRLPlayerSpec& Spec);
	int32_t SpecKeeperIdentity(const HWRLPlayerSpec& Spec);

	/** Per-identity style events of one frame (DESIGN.md §7), counted into Out[4]; BiteSwing de-duplicates feint bites. */
	void CountStyleEvents(const std::vector<FDuelEvent>& Events, const FDuel& Duel, int32_t& BiteSwing, uint8_t* Out);

	class FRLEnv
	{
	public:
		FRLEnv();
		~FRLEnv();

		void Configure(const HWRLEnvConfig& InCfg);
		/** Player policies for kind-7 specs (owned by the batch; may be null). */
		void SetPlayerPolicies(const std::vector<const FRLPolicy*>* InPolicies) { PlayerPolicies = InPolicies; }
		/** The player for the NEXT session. */
		void SetNextPlayer(const HWRLPlayerSpec& Spec);
		/** Start a new session (pending player), run to the first decision. Seed: the env's own stream. */
		void Reset(uint64_t Seed);
		/** The current decision: RL::ObsDim floats, RL::HistoryTokens x RL::TokenFields int8, RL::NumActions bytes. */
		void Observe(float* OutObs, int8_t* OutTokens, uint8_t* OutMask) const;
		/** Apply Action at the current decision, simulate to the next one (rolling fights / sessions over). */
		FRLStepResult Step(int32_t Action, const FRLRead* Read = nullptr);

		const FEncounterStats& FightStats() const;
		const HWRLPlayerSpec& CurrentPlayer() const { return Current; }
		/** Masked / failing actions the trainer requested since the env was created (must stay 0). */
		int64_t IllegalActions() const;
		int64_t FightsPlayed() const;
		int64_t SessionsPlayed() const;
		int64_t HabitSwitches() const;

	private:
		struct FImpl;
		void StartSession();
		void StartFight();
		void FrameStart();
		void FrameRest(FRLStepResult& R);
		bool FightOver() const;

		std::unique_ptr<FImpl> Impl;
		HWRLPlayerSpec Current{};
		HWRLPlayerSpec Next{};
		const std::vector<const FRLPolicy*>* PlayerPolicies = nullptr;
	};

	/** RL.md §7: sessions of fights against one player spec with a brain deciding in C++ (0 script, 1 classic, 2 RL). */
	HWRLEvalStats EvalSessions(int32_t Arm, const FRLPolicy* Policy, const HWRLPlayerSpec& Spec, int32_t Sessions, uint64_t Seed,
		bool bImmortal, int32_t FightSeconds, int32_t ScriptIndex, float Temperature = 0.f, int32_t MinSwingGap = 0,
		FBotDiag* OutBotDiag = nullptr);

	/** The reading test's raw material: one record per RL-keeper attack (see hwrl_eval_attack_log). */
	int32_t EvalAttackLog(const FRLPolicy& Policy, const HWRLPlayerSpec* Specs, int32_t NumSpecs, int32_t Sessions, uint64_t Seed,
		bool bImmortal, int32_t FightSeconds, int32_t Threads, HWRLAttackRecord* Out, int32_t MaxRecords, HWRLEvalStats* OutStats);

	/** N environments stepped together on a pool of threads (RL.md §3.1: Python overhead per step would dominate). */
	class FRLEnvBatch
	{
	public:
		FRLEnvBatch(int32_t NumEnvs, uint64_t Seed, const HWRLEnvConfig& Cfg);
		~FRLEnvBatch();

		int32_t Size() const { return static_cast<int32_t>(Envs.size()); }
		/** Register a player-side policy for kind-7 specs; returns its id or -1 (not a player policy). */
		int32_t AddPlayerPolicy(const FRLPolicy* Policy);
		void SetNextPlayer(int32_t Env, const HWRLPlayerSpec& Spec);
		void Reset();
		void Observe(float* Obs, int8_t* Tokens, uint8_t* Mask);
		void Step(const int32_t* Actions, const int8_t* AuxTop, const float* AuxTopP, HWRLStepOut& Out);
		const FRLEnv& Env(int32_t I) const { return *Envs[static_cast<size_t>(I)]; }
		int32_t Threads() const { return Pool.Threads(); }

	private:
		std::vector<std::unique_ptr<FRLEnv>> Envs;
		std::vector<const FRLPolicy*> PlayerPolicies;
		FWorkerPool Pool;
		uint64_t BaseSeed = 0;
	};

	// ==============================================================================================
	// RL-4: the exploiter's training env
	// ==============================================================================================

	struct FRLKeeperSpec
	{
		float   Skill = 1.f;
		int32_t Identity = 0;
		int32_t Fights = 1;
		int32_t Tag = 0;
	};

	class FRLPlayerEnv
	{
	public:
		FRLPlayerEnv();
		~FRLPlayerEnv();
		void Configure(const HWRLEnvConfig& InCfg, const FRLPolicy* InBoss);
		void SetNextKeeper(const FRLKeeperSpec& Spec) { Next = Spec; }
		void Reset(uint64_t Seed);
		void Observe(float* OutObs, uint8_t* OutMask) const;
		FRLStepResult Step(int32_t Action);

	private:
		struct FImpl;
		void StartSession();
		void StartFight();
		void FrameStart();
		void FrameRest(FRLStepResult& R);
		bool FightOver() const;

		std::unique_ptr<FImpl> Impl;
		FRLKeeperSpec Current;
		FRLKeeperSpec Next;
	};

	class FRLPlayerEnvBatch
	{
	public:
		FRLPlayerEnvBatch(int32_t NumEnvs, uint64_t Seed, const HWRLEnvConfig& Cfg, const FRLPolicy* Boss);
		int32_t Size() const { return static_cast<int32_t>(Envs.size()); }
		void SetNextKeeper(int32_t Env, const FRLKeeperSpec& Spec);
		void Reset();
		void Observe(float* Obs, uint8_t* Mask);
		void Step(const int32_t* Actions, HWRLStepOut& Out);

	private:
		std::vector<std::unique_ptr<FRLPlayerEnv>> Envs;
		FWorkerPool Pool;
		uint64_t BaseSeed = 0;
	};
}
