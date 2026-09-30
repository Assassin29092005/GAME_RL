// HellwalkerRL — tools only. The RL training environment (see HWRLEnv.h and RL/DESIGN.md §7).

#include "HWRLEnv.h"

#include "HWCore/HWRLBrain.h"
#include "HWCore/HWScriptBrain.h"
#include "Classic/HWClassicBrain.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>

namespace HW
{
	namespace
	{
		uint64_t SplitMix64(uint64_t X)
		{
			uint64_t Z = X + 0x9E3779B97F4A7C15ull;
			Z = (Z ^ (Z >> 30)) * 0xBF58476D1CE4E5B9ull;
			Z = (Z ^ (Z >> 27)) * 0x94D049BB133111EBull;
			return Z ^ (Z >> 31);
		}

		int32_t SeedFrom(uint64_t X) { return static_cast<int32_t>(SplitMix64(X) & 0x7fffffffull); }

		/** FPlayerBot's seed convention (RunEncounter), without signed overflow. */
		int32_t BotSeed(int32_t FightSeed) { return static_cast<int32_t>(static_cast<uint32_t>(FightSeed) * 7919u + 17u); }

		/**
		 * The encounter's brain during training: decisions come from the trainer (the env applies them through the
		 * observer at the paused frame), so Think never commits; every frame's events go to the observer.
		 */
		class FTrainingBrain : public IBossBrain
		{
		public:
			FRLObserver* Obs = nullptr;
			EBrainMode Mode() const override { return EBrainMode::Hellwalker; }
			const char* Name() const override { return "Hellwalker (RL, training)"; }
			void BeginEncounter(int32_t Seed) override { (void)Seed; }
			bool Think(FDuel& Duel, const FDuelGeometry& Geo, FBrainDecision* OutDecision) override
			{
				(void)Duel; (void)Geo; (void)OutDecision;
				return false;
			}
			void OnFrame(const std::vector<FDuelEvent>& Events, const FDuel& Duel, const FDuelGeometry& Geo, ESym PlayerMovement) override
			{
				Obs->RecordFrame(Events, Duel, Geo, PlayerMovement);
			}
			bool PopReadMeter(FReadMeterEvent& Out) override { return Obs->PopReadMeter(Out); }
			const FBrainDecision& LastDecision() const override { return None; }
			int32_t Decisions() const override { return Obs->DecisionsMade(); }
			int32_t CountersLanded() const override { return Obs->ReadCountersLanded(); }

		private:
			FBrainDecision None;
		};

		float Clamp01(float X) { return X < 0.f ? 0.f : (X > 1.f ? 1.f : X); }

		constexpr int32_t CostWarmupFrames = 900; // 15 s: the aggression floor is not measured during the opening approach
	}

	FBotProfile MakeProfileFromSpec(const HWRLPlayerSpec& Spec)
	{
		int32_t Kind = Spec.kind;
		if (Kind == 7 || Kind < 0 || Kind > 6) { Kind = static_cast<int32_t>(EBotKind::Varied); } // exploiters: RL-4
		const float Skill = Clamp01(Spec.skill);
		FBotProfile P = MakeBotProfile(Kind == 6 ? EBotKind::Varied : static_cast<EBotKind>(Kind), Skill);
		if (Kind == 6)
		{
			P.Kind = EBotKind::Habit;
			std::memcpy(P.Habit, Spec.habit, sizeof(P.Habit));
			P.HabitNoise = Clamp01(Spec.habit_noise);
			P.bAdapts = Spec.adapts != 0;
		}
		auto Override = [](float& Dst, float V) { if (V >= 0.f) { Dst = V; } };
		Override(P.ReactMean, Spec.react_mean);
		Override(P.ReactSigma, Spec.react_sigma);
		Override(P.TimingSigma, Spec.timing_sigma);
		Override(P.ParryAim, Spec.parry_aim);
		Override(P.StepAim, Spec.step_aim);
		Override(P.FeintRead, Spec.feint_read);
		Override(P.KillerRead, Spec.killer_read);
		Override(P.PunishRate, Spec.punish_rate);
		Override(P.AggroRate, Spec.aggro_rate);
		Override(P.HeavyRate, Spec.heavy_rate);
		Override(P.SwitchRate, Spec.switch_rate);
		Override(P.PreferredRange, Spec.preferred_range);
		if (Spec.chain_len > 0) { P.ChainLen = Spec.chain_len; }
		return P;
	}

	// ==============================================================================================
	// One environment
	// ==============================================================================================

	struct FRLEnv::FImpl
	{
		HWRLEnvConfig Cfg{ 180, 66.f, 450.f, 800.f, 0, 0 };
		FEncounter Enc;
		FSimArena Arena;
		FPlayerBot Bot;
		FBotProfile Profile;
		FRLSession Session;
		FRLObserver Obs;
		FTrainingBrain Brain;
		FRandom Rng;
		int32_t FightInSession = 0;
		int32_t FightsInSession = 1;
		int32_t SwitchAfter = -1;
		int64_t Illegal = 0;       // over finished fights (the observer counts the current one)
		int64_t Fights = 0;
		int64_t Sessions = 0;
		int64_t Switches = 0;
	};

	FRLEnv::FRLEnv() : Impl(std::make_unique<FImpl>())
	{
		Impl->Brain.Obs = &Impl->Obs;
		Impl->Enc.bRecordRows = false;
		Next.kind = static_cast<int32_t>(EBotKind::Varied);
		Next.skill = 0.5f;
		Next.fights_in_session = 1;
		Next.habit_switch_after = -1;
		Next.react_mean = Next.react_sigma = Next.timing_sigma = Next.parry_aim = Next.step_aim = Next.feint_read = Next.killer_read = -1.f;
		Next.punish_rate = Next.aggro_rate = Next.heavy_rate = Next.switch_rate = Next.preferred_range = -1.f;
	}

	FRLEnv::~FRLEnv() = default;

	void FRLEnv::Configure(const HWRLEnvConfig& InCfg)
	{
		Impl->Cfg = InCfg;
		if (Impl->Cfg.max_fight_seconds <= 0) { Impl->Cfg.max_fight_seconds = 180; }
		if (Impl->Cfg.start_distance_max < Impl->Cfg.start_distance_min) { Impl->Cfg.start_distance_max = Impl->Cfg.start_distance_min; }
	}

	void FRLEnv::SetNextPlayer(const HWRLPlayerSpec& Spec) { Next = Spec; }

	void FRLEnv::Reset(uint64_t Seed)
	{
		Impl->Rng.Initialize(SeedFrom(Seed));
		StartSession();
	}

	void FRLEnv::StartSession()
	{
		FImpl& I = *Impl;
		Current = Next;
		I.Session.Reset();
		I.Profile = MakeProfileFromSpec(Current);
		I.FightInSession = 0;
		I.FightsInSession = std::clamp(Current.fights_in_session, 1, 4);
		I.SwitchAfter = I.Profile.Kind == EBotKind::Habit ? Current.habit_switch_after : -1;
		++I.Sessions;
		StartFight();
	}

	void FRLEnv::StartFight()
	{
		FImpl& I = *Impl;
		for (int32_t Guard = 0; Guard < 8; ++Guard)
		{
			const int32_t Seed = static_cast<int32_t>(I.Rng.GetUnsignedInt() & 0x7fffffffu);
			I.Illegal += I.Obs.IllegalActions();
			I.Enc.Begin(&I.Brain, Seed, I.Cfg.immortal != 0);
			I.Arena.Reset(I.Rng.FRandRange(I.Cfg.start_distance_min, I.Cfg.start_distance_max));
			I.Bot.Reset(I.Profile, BotSeed(Seed));
			I.Bot.SetHabitPhase(I.SwitchAfter >= 0 && I.Session.BossSwingsSeen >= I.SwitchAfter ? 1 : 0);
			I.Obs.Config.TargetSwingsPerMin = I.Cfg.target_spm;
			I.Obs.BeginEncounter(&I.Session, I.Enc.Duel, I.Arena.Geometry());
			++I.Fights;
			// Run to the first decision (normally frame 0: the keeper starts idle and actionable).
			FRLStepResult Scratch;
			while (!FightOver())
			{
				FrameStart();
				if (I.Obs.IsDecisionPoint(I.Enc.Duel)) { return; }
				FrameRest(Scratch);
			}
			// A fight that ended before the keeper could decide carries no experience: start another.
		}
	}

	void FRLEnv::FrameStart()
	{
		Impl->Enc.ThinkBoss(Impl->Arena.Geometry()); // the training brain never commits; this latches the frame's geometry
	}

	void FRLEnv::FrameRest(FRLStepResult& R)
	{
		FImpl& I = *Impl;
		float Fwd = 0.f;
		float Lat = 0.f;
		I.Bot.Act(I.Enc.Duel, I.Arena, Fwd, Lat);
		I.Arena.LatchCommits(I.Enc.Duel);
		I.Arena.SnapshotPreStep(I.Enc.Duel);
		I.Enc.StepFrame(I.Arena, I.Bot.MovementSym());
		I.Bot.OnEvents(I.Enc.FrameEvents(), I.Enc.Duel);
		I.Arena.Integrate(I.Enc.Duel, Fwd, Lat);
		++R.Frames;
		if (I.SwitchAfter >= 0 && I.Bot.GetHabitPhase() == 0 && I.Session.BossSwingsSeen >= I.SwitchAfter)
		{
			I.Bot.SetHabitPhase(1); // the habit switch (RL.md §6): the keeper's read goes stale
			++I.Switches;
		}
	}

	bool FRLEnv::FightOver() const
	{
		const FImpl& I = *Impl;
		return I.Enc.IsOver() || I.Enc.Duel.Frame >= I.Cfg.max_fight_seconds * FramesPerSecond;
	}

	void FRLEnv::Observe(float* OutObs, int8_t* OutTokens, uint8_t* OutMask) const
	{
		Impl->Obs.BuildObservation(Impl->Enc.Duel, OutObs, OutTokens);
		Impl->Obs.BuildMask(Impl->Enc.Duel, OutMask);
	}

	FRLStepResult FRLEnv::Step(int32_t Action, const FRLRead* Read)
	{
		FImpl& I = *Impl;
		FRLStepResult R;
		R.Tag = Current.tag;
		R.FightIndex = I.Session.FightIndex;
		R.HabitPhase = I.Bot.GetHabitPhase();
		const float PrevDealt = I.Enc.Stats.PlayerDamageTaken;
		const float PrevTaken = I.Enc.Stats.BossDamageTaken;
		const int32_t PrevAttacks = I.Obs.AttacksCommitted();
		const int32_t PrevReads = I.Obs.ReadCountersLanded();

		// Paused at the start of a decision frame (its geometry already latched): act, then finish the frame.
		I.Obs.ApplyAction(I.Enc.Duel, Action, Read);
		FrameRest(R);
		bool bOver = FightOver();
		while (!bOver)
		{
			FrameStart();
			if (I.Obs.IsDecisionPoint(I.Enc.Duel)) { break; }
			FrameRest(R);
			bOver = FightOver();
		}

		const FCombatTuning& K = Tuning();
		R.DmgDealt = I.Enc.Stats.PlayerDamageTaken - PrevDealt;
		R.DmgTaken = I.Enc.Stats.BossDamageTaken - PrevTaken;
		R.Swings = I.Obs.AttacksCommitted() - PrevAttacks;
		const int32_t Label = I.Obs.AnswerToLastDecision();
		R.AuxLabel = static_cast<int8_t>(Label >= 0 && Label < RL::NumAnswerClasses ? Label : -1);
		R.bReadCounter = I.Obs.ReadCountersLanded() > PrevReads;
		R.Reward = R.DmgDealt / K.PlayerHealthMax - R.DmgTaken / K.BossHealthMax;
		// The aggression floor as RL.md section 4.5 writes it: a per-window hinge, max(0, target - actual), so a surplus
		// against one player cannot pay for a deficit against another (a linear swing debt let the keeper stop swinging
		// into parry-happy players). Rate = the last 30 s of this fight; the first 15 s are exempt (every fight opens
		// with an approach). Units: missing swings/min / 60, times the step's seconds.
		if (I.Enc.Duel.Frame >= CostWarmupFrames)
		{
			const float Deficit = I.Cfg.target_spm - I.Obs.SwingsPerMinute(I.Enc.Duel.Frame);
			R.Cost = Deficit > 0.f ? Deficit / 60.f * static_cast<float>(R.Frames) / static_cast<float>(FramesPerSecond) : 0.f;
		}

		if (bOver)
		{
			R.bFightDone = true;
			if (I.Enc.Stats.bBossDied) { R.Result = 2; R.Reward -= 1.f; }       // the keeper's death dominates a double KO
			else if (I.Enc.Stats.bPlayerDied) { R.Result = 1; R.Reward += 1.f; }
			else { R.Result = 3; }                                               // timeout
			++I.FightInSession;
			if (I.FightInSession >= I.FightsInSession)
			{
				R.bSessionDone = true;
				StartSession();
			}
			else
			{
				StartFight();
			}
		}
		return R;
	}

	const FEncounterStats& FRLEnv::FightStats() const { return Impl->Enc.Stats; }
	int64_t FRLEnv::IllegalActions() const { return Impl->Illegal + Impl->Obs.IllegalActions(); }
	int64_t FRLEnv::FightsPlayed() const { return Impl->Fights; }
	int64_t FRLEnv::SessionsPlayed() const { return Impl->Sessions; }
	int64_t FRLEnv::HabitSwitches() const { return Impl->Switches; }

	// ==============================================================================================
	// The batch: a persistent worker pool
	// ==============================================================================================

	/**
	 * A barrier-style pool: every worker takes part in every round (ParallelFor call) and checks in when the items are
	 * exhausted, so no worker can still be inside a round when the next one starts (the job lives on the caller's stack).
	 */
	struct FRLEnvBatch::FPool
	{
		std::vector<std::thread> Workers;
		std::mutex Mutex;
		std::condition_variable Wake;
		std::condition_variable Done;
		const std::function<void(int32_t)>* Job = nullptr;
		int32_t NumItems = 0;
		int32_t ChunkSize = 1;
		std::atomic<int32_t> NextItem{ 0 };
		int32_t CheckedIn = 0;
		uint64_t Generation = 0;
		bool bStop = false;

		static void RunChunks(const std::function<void(int32_t)>& Fn, std::atomic<int32_t>& Next, int32_t N, int32_t Chunk)
		{
			for (;;)
			{
				const int32_t Begin = Next.fetch_add(Chunk);
				if (Begin >= N) { return; }
				const int32_t End = std::min(N, Begin + Chunk);
				for (int32_t I = Begin; I < End; ++I) { Fn(I); }
			}
		}

		void WorkerLoop()
		{
			uint64_t Seen = 0;
			for (;;)
			{
				const std::function<void(int32_t)>* Fn = nullptr;
				int32_t N = 0;
				int32_t Chunk = 1;
				{
					std::unique_lock<std::mutex> Lock(Mutex);
					Wake.wait(Lock, [&] { return bStop || Generation != Seen; });
					if (bStop) { return; }
					Seen = Generation;
					Fn = Job;
					N = NumItems;
					Chunk = ChunkSize;
				}
				RunChunks(*Fn, NextItem, N, Chunk);
				{
					std::lock_guard<std::mutex> Lock(Mutex);
					++CheckedIn;
				}
				Done.notify_all();
			}
		}
	};

	FRLEnvBatch::FRLEnvBatch(int32_t NumEnvs, uint64_t Seed, const HWRLEnvConfig& Cfg)
		: Pool(std::make_unique<FPool>()), BaseSeed(Seed)
	{
		const int32_t N = NumEnvs > 0 ? NumEnvs : 1;
		Envs.reserve(static_cast<size_t>(N));
		for (int32_t I = 0; I < N; ++I)
		{
			Envs.push_back(std::make_unique<FRLEnv>());
			Envs.back()->Configure(Cfg);
		}
		const int32_t Hw = static_cast<int32_t>(std::thread::hardware_concurrency());
		NumThreads = Cfg.num_threads > 0 ? Cfg.num_threads : (Hw > 0 ? Hw : 1);
		NumThreads = std::min(NumThreads, N);
		for (int32_t T = 1; T < NumThreads; ++T) // the calling thread is worker 0
		{
			Pool->Workers.emplace_back([this] { Pool->WorkerLoop(); });
		}
	}

	FRLEnvBatch::~FRLEnvBatch()
	{
		{
			std::lock_guard<std::mutex> Lock(Pool->Mutex);
			Pool->bStop = true;
		}
		Pool->Wake.notify_all();
		for (std::thread& T : Pool->Workers) { T.join(); }
	}

	void FRLEnvBatch::ParallelFor(int32_t N, const std::function<void(int32_t)>& Fn) const
	{
		if (NumThreads <= 1 || N <= 1)
		{
			for (int32_t I = 0; I < N; ++I) { Fn(I); }
			return;
		}
		FPool& P = *Pool;
		int32_t Chunk = 1;
		{
			std::lock_guard<std::mutex> Lock(P.Mutex);
			P.Job = &Fn;
			P.NumItems = N;
			P.ChunkSize = Chunk = std::max(1, N / (NumThreads * 8));
			P.NextItem.store(0);
			P.CheckedIn = 0;
			++P.Generation;
		}
		P.Wake.notify_all();
		FPool::RunChunks(Fn, P.NextItem, N, Chunk);
		std::unique_lock<std::mutex> Lock(P.Mutex);
		const int32_t Workers = static_cast<int32_t>(P.Workers.size());
		P.Done.wait(Lock, [&] { return P.CheckedIn == Workers; });
		P.Job = nullptr;
	}

	void FRLEnvBatch::SetNextPlayer(int32_t Env, const HWRLPlayerSpec& Spec)
	{
		if (Env >= 0 && Env < Size()) { Envs[static_cast<size_t>(Env)]->SetNextPlayer(Spec); }
	}

	void FRLEnvBatch::Reset()
	{
		ParallelFor(Size(), [this](int32_t I) { Envs[static_cast<size_t>(I)]->Reset(SplitMix64(BaseSeed + static_cast<uint64_t>(I))); });
	}

	void FRLEnvBatch::Observe(float* Obs, int8_t* Tokens, uint8_t* Mask) const
	{
		ParallelFor(Size(), [&](int32_t I)
		{
			const size_t K = static_cast<size_t>(I);
			Envs[K]->Observe(Obs + K * RL::ObsDim, Tokens + K * RL::HistoryTokens * RL::TokenFields, Mask + K * RL::NumActions);
		});
	}

	void FRLEnvBatch::Step(const int32_t* Actions, const int8_t* AuxTop, const float* AuxTopP, HWRLStepOut& Out)
	{
		ParallelFor(Size(), [&](int32_t I)
		{
			const size_t K = static_cast<size_t>(I);
			FRLRead Read;
			const FRLRead* ReadPtr = nullptr;
			if (AuxTop != nullptr && AuxTopP != nullptr && AuxTop[K] >= 0 && AuxTop[K] < RL::NumAnswerClasses)
			{
				Read.Predicted = static_cast<ESym>(AuxTop[K]);
				Read.P = AuxTopP[K];
				ReadPtr = &Read;
			}
			const FRLStepResult R = Envs[K]->Step(Actions[K], ReadPtr);
			if (Out.reward) { Out.reward[K] = R.Reward; }
			if (Out.cost) { Out.cost[K] = R.Cost; }
			if (Out.fight_done) { Out.fight_done[K] = R.bFightDone ? 1 : 0; }
			if (Out.session_done) { Out.session_done[K] = R.bSessionDone ? 1 : 0; }
			if (Out.result) { Out.result[K] = R.Result; }
			if (Out.aux_label) { Out.aux_label[K] = R.AuxLabel; }
			if (Out.frames) { Out.frames[K] = R.Frames; }
			if (Out.swings) { Out.swings[K] = R.Swings; }
			if (Out.dmg_dealt) { Out.dmg_dealt[K] = R.DmgDealt; }
			if (Out.dmg_taken) { Out.dmg_taken[K] = R.DmgTaken; }
			if (Out.read_counter) { Out.read_counter[K] = R.bReadCounter ? 1 : 0; }
			if (Out.tag) { Out.tag[K] = R.Tag; }
			if (Out.fight_index) { Out.fight_index[K] = R.FightIndex; }
			if (Out.habit_phase) { Out.habit_phase[K] = R.HabitPhase; }
		});
	}

	// ==============================================================================================
	// Evaluation with the brains in C++ (RL.md §7)
	// ==============================================================================================

	HWRLEvalStats EvalSessions(int32_t Arm, const FRLPolicy* Policy, const HWRLPlayerSpec& Spec, int32_t Sessions, uint64_t Seed,
		bool bImmortal, int32_t FightSeconds, int32_t ScriptIndex)
	{
		HWRLEvalStats S{};
		if (Arm == 2 && (Policy == nullptr || !Policy->IsLoaded())) { return S; }
		FRandom Rng(SeedFrom(Seed));
		const FBotProfile Profile = MakeProfileFromSpec(Spec);
		const int32_t SwitchAfter = Profile.Kind == EBotKind::Habit ? Spec.habit_switch_after : -1;
		const int32_t Fights = std::clamp(Spec.fights_in_session, 1, 4);
		const int32_t MaxFrames = (FightSeconds > 0 ? FightSeconds : 180) * FramesPerSecond;

		FScriptBrain Script;
		Script.SetScriptIndex(ScriptIndex);
		FClassicBrain Classic;
		Classic.SetMode(EBrainMode::Hellwalker);
		{
			const FScriptSlot* Slots = nullptr;
			const int32_t N = BossScript(ScriptIndex, Slots);
			Classic.SetScript(Slots, N);
			Classic.Config().RefSwingsPerMin = 62.f; // the game's reference (the reference project's duel subsystem)
			ApplyScriptTuning(ScriptIndex, Classic.Config());
		}
		std::unique_ptr<FPlaystyleModel> Model;
		FRLBrain RL;
		FRLSession Session;
		IBossBrain* Brain = Arm == 0 ? static_cast<IBossBrain*>(&Script) : (Arm == 1 ? static_cast<IBossBrain*>(&Classic) : static_cast<IBossBrain*>(&RL));

		for (int32_t Sess = 0; Sess < Sessions; ++Sess)
		{
			Model = std::make_unique<FPlaystyleModel>();
			Classic.BindModel(Model.get());
			Session.Reset();
			RL.Bind(Policy, &Session);
			int32_t SessionSwings = 0;
			for (int32_t F = 0; F < Fights; ++F)
			{
				const int32_t FightSeed = static_cast<int32_t>(Rng.GetUnsignedInt() & 0x7fffffffu);
				FEncounter Enc;
				Enc.bRecordRows = false;
				Enc.Begin(Brain, FightSeed, bImmortal);
				FSimArena Arena;
				Arena.Reset(Rng.FRandRange(450.f, 800.f));
				FPlayerBot Bot;
				Bot.Reset(Profile, BotSeed(FightSeed));
				Bot.SetHabitPhase(SwitchAfter >= 0 && SessionSwings >= SwitchAfter ? 1 : 0);
				for (int32_t Frame = 0; Frame < MaxFrames; ++Frame)
				{
					Enc.ThinkBoss(Arena.Geometry());
					float Fwd = 0.f;
					float Lat = 0.f;
					Bot.Act(Enc.Duel, Arena, Fwd, Lat);
					Arena.LatchCommits(Enc.Duel);
					Arena.SnapshotPreStep(Enc.Duel);
					Enc.StepFrame(Arena, Bot.MovementSym());
					Bot.OnEvents(Enc.FrameEvents(), Enc.Duel);
					Arena.Integrate(Enc.Duel, Fwd, Lat);
					for (const FDuelEvent& E : Enc.FrameEvents())
					{
						if (E.Type == EDuelEvent::Commit && E.Side == ESide::Boss && Move(E.Move).IsAttack()) { ++SessionSwings; }
					}
					if (SwitchAfter >= 0 && Bot.GetHabitPhase() == 0 && SessionSwings >= SwitchAfter) { Bot.SetHabitPhase(1); }
					if (Enc.IsOver()) { break; }
				}
				const FEncounterStats& St = Enc.Stats;
				++S.fights;
				S.player_deaths += St.bPlayerDied ? 1 : 0;
				S.boss_deaths += St.bBossDied ? 1 : 0;
				S.timeouts += (!St.bPlayerDied && !St.bBossDied) ? 1 : 0;
				S.seconds += static_cast<float>(St.Frames) / static_cast<float>(FramesPerSecond);
				S.player_dmg_taken += St.PlayerDamageTaken;
				S.boss_dmg_taken += St.BossDamageTaken;
				S.boss_swings += St.BossSwings;
				S.boss_hits += St.BossOutcomes[static_cast<int32_t>(EHitOutcome::Hit)];
				S.boss_whiffs += St.BossOutcomes[static_cast<int32_t>(EHitOutcome::Whiff)];
				S.boss_blocked += St.BossOutcomes[static_cast<int32_t>(EHitOutcome::Blocked)];
				S.boss_parried += St.BossOutcomes[static_cast<int32_t>(EHitOutcome::Parried)];
				S.decisions += St.Decisions;
				S.read_counters += St.CountersLanded;
			}
		}
		return S;
	}
}
