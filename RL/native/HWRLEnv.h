// HellwalkerRL — tools only. The RL training environment (RL.md §3): the real combat rules (FDuel via FEncounter), a
// simulated player from the population (FPlayerBot: the reference bots or a procedural habit player), and the keeper's
// decision loop — paused at every decision point for an action from the trainer.
//
// The keeper's senses / masks / action application are the game's own FRLObserver, so the policy the trainer shapes
// here is exactly the one the game runs. Nothing in this folder is compiled into the game.
//
// Episode = one fight (death, or the timeout). Session = fights_in_session fights against the same player; the keeper's
// memory (the trainer's recurrent state + the observer's history tokens) is carried across the fights of a session
// and reset between sessions. Fights and sessions roll over automatically inside Step().

#pragma once

#include "hwrl_capi.h"

#include "HWCore/HWEncounter.h"
#include "HWCore/HWRLObserver.h"
#include "HWCore/HWRLPolicy.h"
#include "HWCore/HWSim.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace HW
{
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
		float   DmgDealt = 0.f;
		float   DmgTaken = 0.f;
		bool    bReadCounter = false;
		int32_t Tag = 0;
		int32_t FightIndex = 0;
		int32_t HabitPhase = 0;
	};

	/** The player profile a spec describes (reference profile from skill, overrides, habit tables). */
	FBotProfile MakeProfileFromSpec(const HWRLPlayerSpec& Spec);

	class FRLEnv
	{
	public:
		FRLEnv();
		~FRLEnv();

		void Configure(const HWRLEnvConfig& InCfg);
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
	};

	/** RL.md §7: sessions of fights against one player spec with a brain deciding in C++ (0 script, 1 classic, 2 RL). */
	HWRLEvalStats EvalSessions(int32_t Arm, const FRLPolicy* Policy, const HWRLPlayerSpec& Spec, int32_t Sessions, uint64_t Seed,
		bool bImmortal, int32_t FightSeconds, int32_t ScriptIndex);

	/** N environments stepped together on a pool of threads (RL.md §3.1: Python overhead per step would dominate). */
	class FRLEnvBatch
	{
	public:
		FRLEnvBatch(int32_t NumEnvs, uint64_t Seed, const HWRLEnvConfig& Cfg);
		~FRLEnvBatch();

		int32_t Size() const { return static_cast<int32_t>(Envs.size()); }
		void SetNextPlayer(int32_t Env, const HWRLPlayerSpec& Spec);
		void Reset();
		void Observe(float* Obs, int8_t* Tokens, uint8_t* Mask) const;
		void Step(const int32_t* Actions, const int8_t* AuxTop, const float* AuxTopP, HWRLStepOut& Out);
		const FRLEnv& Env(int32_t I) const { return *Envs[static_cast<size_t>(I)]; }
		int32_t Threads() const { return NumThreads; }

	private:
		struct FPool;
		/** Run Fn(i) for i in [0, N) across the pool; returns when all are done. Each env is touched by one thread. */
		void ParallelFor(int32_t N, const std::function<void(int32_t)>& Fn) const;

		std::vector<std::unique_ptr<FRLEnv>> Envs;
		std::unique_ptr<FPool> Pool;
		uint64_t BaseSeed = 0;
		int32_t NumThreads = 1;
	};
}
