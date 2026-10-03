// HellwalkerRL — tools only. Smoke test + benchmark of the training environment (RL-0): throughput, determinism across
// thread counts, mask sanity, fight / session rollover, habit switches, and the C++ evaluation arms.
//
//   envbench [num_envs] [steps] [policy.hwrl]

#include "HWRLEnv.h"

#include "HWCore/HWRLTypes.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace HW;

namespace
{
	HWRLPlayerSpec BaseSpec()
	{
		HWRLPlayerSpec S{};
		S.kind = 3;
		S.skill = 0.6f;
		S.fights_in_session = 2;
		S.habit_switch_after = -1;
		S.react_mean = S.react_sigma = S.timing_sigma = S.parry_aim = S.step_aim = S.feint_read = S.killer_read = -1.f;
		S.punish_rate = S.aggro_rate = S.heavy_rate = S.switch_rate = S.preferred_range = -1.f;
		S.policy_id = -1;
		S.keeper_skill = -1.f;   // the Hellwalker tier
		S.keeper_identity = -1;  // the Warden
		return S;
	}

	/** Env i's player: reference bots and habit players (some switching habits), deterministic in i. */
	HWRLPlayerSpec SpecFor(int32_t I)
	{
		HWRLPlayerSpec S = BaseSpec();
		S.tag = I % 7;
		S.keeper_skill = (I % 5 == 4) ? -1.f : 0.25f * static_cast<float>(I % 5); // every difficulty, and the default
		S.keeper_identity = I % 3;
		if (I % 7 < 6)
		{
			S.kind = I % 6;
			S.skill = 0.2f + 0.1f * static_cast<float>(I % 8);
		}
		else
		{
			S.kind = 6;
			for (int32_t T = 0; T < 2; ++T)
			{
				for (int32_t C = 0; C < 4; ++C)
				{
					for (int32_t R = 0; R < 8; ++R) { S.habit[T][C][R] = (R == (C + T * 3 + I) % 8) ? 8.f : 0.3f; }
				}
			}
			S.habit_switch_after = 12 + I % 20;
			S.habit_noise = 0.05f;
			S.fights_in_session = 3;
			S.learn_rate = (I / 7) % 2 == 0 ? 0.2f : 0.f; // half of them learn
		}
		return S;
	}

	uint32_t Hash(uint32_t H, const void* Data, size_t N)
	{
		const uint8_t* P = static_cast<const uint8_t*>(Data);
		for (size_t I = 0; I < N; ++I) { H = (H ^ P[I]) * 16777619u; }
		return H;
	}

	/** A deterministic "random policy": uniform among the allowed actions, from (env, step). */
	int32_t PickAction(const uint8_t* Mask, int32_t Env, int32_t Step)
	{
		uint32_t X = static_cast<uint32_t>(Env) * 2654435761u ^ static_cast<uint32_t>(Step) * 40503u ^ 0x9E3779B9u;
		X ^= X >> 16; X *= 0x7feb352du; X ^= X >> 15;
		int32_t Allowed = 0;
		for (int32_t A = 0; A < RL::NumActions; ++A) { Allowed += Mask[A] ? 1 : 0; }
		int32_t K = static_cast<int32_t>(X % static_cast<uint32_t>(Allowed > 0 ? Allowed : 1));
		for (int32_t A = 0; A < RL::NumActions; ++A)
		{
			if (!Mask[A]) { continue; }
			if (K-- == 0) { return A; }
		}
		return RL::ActionWait;
	}

	struct FBuffers
	{
		std::vector<float> Obs;
		std::vector<int8_t> Tokens;
		std::vector<uint8_t> Mask;
		std::vector<int32_t> Actions;
		std::vector<float> Reward, Cost, DmgDealt, DmgTaken;
		std::vector<uint8_t> FightDone, SessionDone, Result, ReadCounter;
		std::vector<int8_t> AuxLabel;
		std::vector<int32_t> Frames, Swings, Tag, FightIndex, HabitPhase, Identity, Hits;
		std::vector<float> Skill, Style;
		std::vector<uint8_t> StyleEvents;
		HWRLStepOut Out{};

		explicit FBuffers(int32_t N)
		{
			const size_t K = static_cast<size_t>(N);
			Obs.resize(K * RL::ObsDim); Tokens.resize(K * RL::HistoryTokens * RL::TokenFields); Mask.resize(K * RL::NumActions);
			Actions.resize(K); Reward.resize(K); Cost.resize(K); DmgDealt.resize(K); DmgTaken.resize(K);
			FightDone.resize(K); SessionDone.resize(K); Result.resize(K); ReadCounter.resize(K); AuxLabel.resize(K);
			Frames.resize(K); Swings.resize(K); Tag.resize(K); FightIndex.resize(K); HabitPhase.resize(K);
			Out.reward = Reward.data(); Out.cost = Cost.data(); Out.fight_done = FightDone.data(); Out.session_done = SessionDone.data();
			Out.result = Result.data(); Out.aux_label = AuxLabel.data(); Out.frames = Frames.data(); Out.swings = Swings.data();
			Out.dmg_dealt = DmgDealt.data(); Out.dmg_taken = DmgTaken.data(); Out.read_counter = ReadCounter.data(); Out.tag = Tag.data();
			Out.fight_index = FightIndex.data(); Out.habit_phase = HabitPhase.data();
			Identity.resize(K); Hits.resize(K); Skill.resize(K); Style.resize(K); StyleEvents.resize(K * 4);
			Out.identity = Identity.data(); Out.hits = Hits.data(); Out.skill = Skill.data(); Out.style = Style.data();
			Out.style_events = StyleEvents.data();
		}
	};

	struct FRunStats
	{
		double Seconds = 0.0;
		int64_t Decisions = 0;
		int64_t Frames = 0;
		int64_t FightsDone = 0;
		int64_t SessionsDone = 0;
		int64_t Labels = 0;
		double RewardSum = 0.0;
		double CostSum = 0.0;
		uint32_t Hash = 2166136261u;
		bool bFinite = true;
		double Style = 0.0;
		int64_t Events[4] = {};
		int64_t Hits = 0;
		int64_t Swings = 0;
	};

	FRunStats Run(int32_t N, int32_t Threads, int32_t Steps, uint64_t Seed, FRLEnvBatch** OutBatch = nullptr)
	{
		HWRLEnvConfig Cfg{ 180, 66.f, 450.f, 800.f, 0, Threads, 1.f, 0 };
		FRLEnvBatch* Batch = new FRLEnvBatch(N, Seed, Cfg);
		for (int32_t I = 0; I < N; ++I) { Batch->SetNextPlayer(I, SpecFor(I)); }
		FBuffers B(N);
		FRunStats S;
		const auto T0 = std::chrono::steady_clock::now();
		Batch->Reset();
		for (int32_t Step = 0; Step < Steps; ++Step)
		{
			Batch->Observe(B.Obs.data(), B.Tokens.data(), B.Mask.data());
			for (int32_t I = 0; I < N; ++I) { B.Actions[static_cast<size_t>(I)] = PickAction(B.Mask.data() + static_cast<size_t>(I) * RL::NumActions, I, Step); }
			S.Hash = Hash(S.Hash, B.Obs.data(), B.Obs.size() * sizeof(float));
			S.Hash = Hash(S.Hash, B.Tokens.data(), B.Tokens.size());
			S.Hash = Hash(S.Hash, B.Mask.data(), B.Mask.size());
			Batch->Step(B.Actions.data(), nullptr, nullptr, B.Out);
			S.Hash = Hash(S.Hash, B.Reward.data(), B.Reward.size() * sizeof(float));
			S.Hash = Hash(S.Hash, B.AuxLabel.data(), B.AuxLabel.size());
			S.Hash = Hash(S.Hash, B.StyleEvents.data(), B.StyleEvents.size());
			S.Hash = Hash(S.Hash, B.Hits.data(), B.Hits.size() * sizeof(int32_t));
			for (int32_t I = 0; I < N; ++I)
			{
				const size_t K = static_cast<size_t>(I);
				S.Style += B.Style[K];
				for (int32_t E = 0; E < 4; ++E) { S.Events[E] += B.StyleEvents[K * 4 + static_cast<size_t>(E)]; }
				S.Hits += B.Hits[K];
				S.Swings += B.Swings[K];
			}
			for (int32_t I = 0; I < N; ++I)
			{
				const size_t K = static_cast<size_t>(I);
				S.Frames += B.Frames[K];
				S.FightsDone += B.FightDone[K];
				S.SessionsDone += B.SessionDone[K];
				S.Labels += B.AuxLabel[K] >= 0 ? 1 : 0;
				S.RewardSum += B.Reward[K];
				S.CostSum += B.Cost[K];
				S.bFinite = S.bFinite && B.Reward[K] == B.Reward[K] && B.Cost[K] == B.Cost[K];
			}
			S.Decisions += N;
		}
		S.Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - T0).count();
		if (OutBatch != nullptr) { *OutBatch = Batch; } else { delete Batch; }
		return S;
	}
}

int main(int Argc, char** Argv)
{
	const int32_t N = Argc > 1 ? std::atoi(Argv[1]) : 4096;
	const int32_t Steps = Argc > 2 ? std::atoi(Argv[2]) : 40;
	const char* PolicyPath = Argc > 3 ? Argv[3] : nullptr;
	bool bOk = true;

	// ---- throughput ------------------------------------------------------------------------------------------------
	FRLEnvBatch* Big = nullptr;
	const FRunStats All = Run(N, 0, Steps, 12345, &Big);
	std::printf("all threads (%d): %d envs x %d steps: %.0f decisions/s, %.0f sim frames/s, %.1f frames/decision\n", Big->Threads(), N, Steps,
		static_cast<double>(All.Decisions) / All.Seconds, static_cast<double>(All.Frames) / All.Seconds,
		static_cast<double>(All.Frames) / static_cast<double>(All.Decisions));
	int64_t Illegal = 0;
	int64_t Switches = 0;
	int64_t Fights = 0;
	for (int32_t I = 0; I < Big->Size(); ++I)
	{
		Illegal += Big->Env(I).IllegalActions();
		Switches += Big->Env(I).HabitSwitches();
		Fights += Big->Env(I).FightsPlayed();
	}
	delete Big;
	std::printf("  fights ended %lld, sessions ended %lld, fights started %lld, habit switches %lld, labelled steps %.1f%%, illegal actions %lld\n",
		static_cast<long long>(All.FightsDone), static_cast<long long>(All.SessionsDone), static_cast<long long>(Fights),
		static_cast<long long>(Switches), 100.0 * static_cast<double>(All.Labels) / static_cast<double>(All.Decisions), static_cast<long long>(Illegal));
	std::printf("  mean reward/decision %.5f, mean cost/decision %.4f, all finite: %s\n", All.RewardSum / static_cast<double>(All.Decisions),
		All.CostSum / static_cast<double>(All.Decisions), All.bFinite ? "yes" : "NO");
	std::printf("  style events: %lld feint bites, %lld evasions, %lld guard breaks, %lld pressure blocks (style reward %.3f total); clean hits %lld of %lld swings\n",
		static_cast<long long>(All.Events[0]), static_cast<long long>(All.Events[1]), static_cast<long long>(All.Events[2]),
		static_cast<long long>(All.Events[3]), All.Style, static_cast<long long>(All.Hits), static_cast<long long>(All.Swings));
	bOk = bOk && Illegal == 0 && All.bFinite && All.FightsDone > 0 && All.SessionsDone > 0 && Switches > 0 && All.Hits > 0 && All.Hits <= All.Swings;

	const FRunStats One = Run(256, 1, Steps, 12345);
	std::printf("1 thread: 256 envs x %d steps: %.0f decisions/s\n", Steps, static_cast<double>(One.Decisions) / One.Seconds);

	// ---- determinism across thread counts --------------------------------------------------------------------------
	const FRunStats A = Run(96, 1, 400, 777);
	const FRunStats Bm = Run(96, 16, 400, 777);
	std::printf("determinism (96 envs x 400 steps): 1 thread %08x vs 16 threads %08x -> %s\n", A.Hash, Bm.Hash, A.Hash == Bm.Hash ? "identical" : "DIFFERENT");
	bOk = bOk && A.Hash == Bm.Hash;
	const FRunStats C = Run(96, 16, 400, 778);
	std::printf("different seed -> %08x (%s)\n", C.Hash, C.Hash != A.Hash ? "differs, as it should" : "SAME?!");
	bOk = bOk && C.Hash != A.Hash;

	// ---- evaluation arms ---------------------------------------------------------------------------------------------
	FRLPolicy Policy;
	const bool bPolicy = PolicyPath != nullptr && Policy.LoadFromFile(PolicyPath);
	if (PolicyPath != nullptr && !bPolicy) { std::printf("cannot load %s: %s\n", PolicyPath, Policy.GetError().c_str()); }
	for (int32_t Arm = 0; Arm < 3; ++Arm)
	{
		if (Arm == 2 && !bPolicy) { std::printf("eval arm 2 (RL): skipped (no policy given)\n"); continue; }
		HWRLPlayerSpec S = BaseSpec();
		S.kind = 2;
		S.skill = 0.8f;
		S.fights_in_session = 3;
		const HWRLEvalStats E = EvalSessions(Arm, bPolicy ? &Policy : nullptr, S, 4, 99, true, 90, 0, 0.f);
		std::printf("eval arm %d vs Habitual 0.8 (4 sessions x 3 x 90 s, immortal): %d fights, dmg/min %.1f, boss dmg/min %.1f, swings/min %.1f, hits %d, reads %d\n",
			Arm, E.fights, E.player_dmg_taken * 60.f / E.seconds, E.boss_dmg_taken * 60.f / E.seconds, E.boss_swings * 60.f / E.seconds, E.boss_hits, E.read_counters);
		bOk = bOk && E.fights == 12;
	}
	if (bPolicy && Policy.IsBossPlayable())
	{
		// The reading test's attack log: the same records for any thread count.
		HWRLPlayerSpec Specs[3] = { SpecFor(6), SpecFor(13), SpecFor(2) };
		std::vector<HWRLAttackRecord> L1(20000), L8(20000);
		HWRLEvalStats S1{}, S8{};
		const int32_t N1 = EvalAttackLog(Policy, Specs, 3, 24, 4242, true, 60, 1, L1.data(), 20000, &S1);
		const int32_t N8 = EvalAttackLog(Policy, Specs, 3, 24, 4242, true, 60, 8, L8.data(), 20000, &S8);
		const bool bSame = N1 == N8 && N1 > 0 && std::memcmp(L1.data(), L8.data(), sizeof(HWRLAttackRecord) * static_cast<size_t>(N1 > 0 ? N1 : 0)) == 0;
		int32_t Resolved = 0;
		for (int32_t K = 0; K < N1; ++K) { Resolved += L1[static_cast<size_t>(K)].outcome != 0 ? 1 : 0; }
		std::printf("attack log (24 sessions): %d attacks (%d resolved, %d swings in the stats), 1 thread vs 8 threads %s\n", N1, Resolved, S1.boss_swings,
			bSame ? "identical" : "DIFFERENT");
		bOk = bOk && bSame && N1 == S1.boss_swings;
	}
	std::printf("envbench: %s\n", bOk ? "PASS" : "FAIL");
	return bOk ? 0 : 1;
}
