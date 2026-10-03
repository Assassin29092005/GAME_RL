// HellwalkerRL — tools only. The RL training environment (see HWRLEnv.h and RL/DESIGN.md §7).

#include "HWRLEnv.h"

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

		// Style rewards (DESIGN.md §7): small, per identity, on top of the shared damage / win reward.
		constexpr float WardenGuardBreak = 0.03f;    // a guard broken by the keeper
		constexpr float WardenPressure = 0.006f;     // a draining swing the player had to block
		constexpr float SageBite = 0.02f;            // the player bit on a bait (pressed at the fake impact)
		constexpr float SageEvade = 0.012f;          // a player swing whiffed into the keeper's evade
		constexpr float ReturnedRead = 0.04f;        // a READ counter: a confident, correct read that a hit landed on

		float StyleReward(int32_t Identity, const uint8_t* Ev, bool bRead, float Scale)
		{
			float R = 0.f;
			switch (Identity)
			{
			case 0: R = WardenGuardBreak * Ev[2] + WardenPressure * Ev[3]; break;
			case 1: R = SageBite * Ev[0] + SageEvade * Ev[1]; break;
			case 2: R = bRead ? ReturnedRead : 0.f; break;
			default: break;
			}
			return R * Scale;
		}

		/** The keeper's health for a session's identity (mortal fights only: immortal pools stay 1e9). */
		void ApplyKeeperHealth(FEncounter& Enc, int32_t Identity, bool bImmortal)
		{
			if (bImmortal) { return; }
			FFighter& B = Enc.Duel.Get(ESide::Boss);
			B.HealthMax *= RL::KeeperHealthScale(Identity);
			B.Health = B.HealthMax;
		}
	}

	// ==============================================================================================
	// The worker pool
	// ==============================================================================================

	/**
	 * Every worker takes part in every round and checks in when the items are exhausted, so no worker can still be
	 * inside a round when the next one starts (the job lives on the caller's stack).
	 */
	struct FWorkerPool::FImpl
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

	FWorkerPool::FWorkerPool(int32_t Threads) : Impl(std::make_unique<FImpl>())
	{
		const int32_t Hw = static_cast<int32_t>(std::thread::hardware_concurrency());
		NumThreads = Threads > 0 ? Threads : (Hw > 0 ? Hw : 1);
		for (int32_t T = 1; T < NumThreads; ++T) // the calling thread is worker 0
		{
			Impl->Workers.emplace_back([this] { Impl->WorkerLoop(); });
		}
	}

	FWorkerPool::~FWorkerPool()
	{
		{
			std::lock_guard<std::mutex> Lock(Impl->Mutex);
			Impl->bStop = true;
		}
		Impl->Wake.notify_all();
		for (std::thread& T : Impl->Workers) { T.join(); }
	}

	void FWorkerPool::ParallelFor(int32_t N, const std::function<void(int32_t)>& Fn)
	{
		if (NumThreads <= 1 || N <= 1)
		{
			for (int32_t I = 0; I < N; ++I) { Fn(I); }
			return;
		}
		FImpl& P = *Impl;
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
		FImpl::RunChunks(Fn, P.NextItem, N, Chunk);
		std::unique_lock<std::mutex> Lock(P.Mutex);
		const int32_t Workers = static_cast<int32_t>(P.Workers.size());
		P.Done.wait(Lock, [&] { return P.CheckedIn == Workers; });
		P.Job = nullptr;
	}

	// ==============================================================================================
	// Specs
	// ==============================================================================================

	FBotProfile MakeProfileFromSpec(const HWRLPlayerSpec& Spec)
	{
		int32_t Kind = Spec.kind;
		if (Kind == 7 || Kind < 0 || Kind > 6) { Kind = static_cast<int32_t>(EBotKind::Varied); } // exploiters: a fallback profile
		const float Skill = Clamp01(Spec.skill);
		FBotProfile P = MakeBotProfile(Kind == 6 ? EBotKind::Varied : static_cast<EBotKind>(Kind), Skill);
		if (Kind == 6)
		{
			P.Kind = EBotKind::Habit;
			std::memcpy(P.Habit, Spec.habit, sizeof(P.Habit));
			P.HabitNoise = Clamp01(Spec.habit_noise);
			P.bAdapts = Spec.adapts != 0;
			P.LearnRate = Spec.learn_rate > 0.f ? (Spec.learn_rate < 1.f ? Spec.learn_rate : 1.f) : 0.f;
			if (Spec.learn_temp > 0.f) { P.LearnTemp = Spec.learn_temp; }
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

	float SpecKeeperSkill(const HWRLPlayerSpec& Spec) { return Spec.keeper_skill < 0.f ? 1.f : Clamp01(Spec.keeper_skill); }
	int32_t SpecKeeperIdentity(const HWRLPlayerSpec& Spec)
	{
		return Spec.keeper_identity >= 0 && Spec.keeper_identity < RL::NumKeepers ? Spec.keeper_identity : 0;
	}

	void CountStyleEvents(const std::vector<FDuelEvent>& Events, const FDuel& Duel, int32_t& BiteSwing, uint8_t* Out)
	{
		const FFighter& B = Duel.Get(ESide::Boss);
		auto Bump = [](uint8_t& V) { if (V < 255) { ++V; } };
		for (const FDuelEvent& E : Events)
		{
			if (E.Type == EDuelEvent::GuardBreak && E.Side == ESide::Player) { Bump(Out[2]); }
			else if (E.Type == EDuelEvent::Outcome && E.Side == ESide::Boss && E.Outcome == EHitOutcome::Blocked && Move(E.Move).ShaChiDrainOnBlock > 0.f)
			{
				Bump(Out[3]);
			}
			else if (E.Type == EDuelEvent::Outcome && E.Side == ESide::Player && E.Outcome == EHitOutcome::Whiff
				&& B.State == EFighterState::Acting && B.CurrentMove().Kind == EMoveKind::Step)
			{
				Bump(Out[1]); // the player's swing ran out into the keeper's evade
			}
			else if (E.Type == EDuelEvent::Commit && E.Side == ESide::Player
				&& (E.Sym == ESym::Parry || E.Sym == ESym::StepF || E.Sym == ESym::StepB || E.Sym == ESym::StepL || E.Sym == ESym::StepR)
				&& B.State == EFighterState::Acting && B.CommitFrame != BiteSwing)
			{
				// A bite: the answer was pressed nearer the bait's fake impact than the real one (the token's Bite rule).
				const FMoveData& M = B.CurrentMove();
				const int32_t Press = E.Frame - B.CommitFrame;
				if (M.IsAttack() && M.FakeImpactFrame >= 0 && Press >= M.FakeImpactFrame - 12 && Press * 2 < M.FakeImpactFrame + M.Startup)
				{
					Bump(Out[0]);
					BiteSwing = B.CommitFrame;
				}
			}
		}
	}

	// ==============================================================================================
	// One environment
	// ==============================================================================================

	struct FRLEnv::FImpl
	{
		HWRLEnvConfig Cfg{ 180, 66.f, 450.f, 800.f, 0, 0, 1.f, 0 };
		FEncounter Enc;
		FSimArena Arena;
		FPlayerBot Bot;
		FBotMemory BotMem;
		FExploiterDriver Exploiter;
		bool bExploiter = false;
		FBotProfile Profile;
		FRLSession Session;
		FRLObserver Obs;
		FTrainingBrain Brain;
		FRandom Rng;
		float Skill = 1.f;
		int32_t Identity = 0;
		int32_t FightInSession = 0;
		int32_t FightsInSession = 1;
		int32_t SwitchAfter = -1;
		int32_t BiteSwing = -1;
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
		Next.policy_id = -1;
		Next.keeper_skill = -1.f;
		Next.keeper_identity = -1;
	}

	FRLEnv::~FRLEnv() = default;

	void FRLEnv::Configure(const HWRLEnvConfig& InCfg)
	{
		Impl->Cfg = InCfg;
		if (Impl->Cfg.max_fight_seconds <= 0) { Impl->Cfg.max_fight_seconds = 180; }
		if (Impl->Cfg.start_distance_max < Impl->Cfg.start_distance_min) { Impl->Cfg.start_distance_max = Impl->Cfg.start_distance_min; }
		if (Impl->Cfg.style_scale < 0.f) { Impl->Cfg.style_scale = 0.f; }
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
		I.BotMem.Reset();
		I.Profile = MakeProfileFromSpec(Current);
		I.Skill = SpecKeeperSkill(Current);
		I.Identity = SpecKeeperIdentity(Current);
		I.FightInSession = 0;
		I.FightsInSession = std::clamp(Current.fights_in_session, 1, 4);
		I.SwitchAfter = I.Profile.Kind == EBotKind::Habit ? Current.habit_switch_after : -1;
		// Kind 7: a registered exploiter policy plays the player; an unknown id falls back to the profile's bot.
		I.bExploiter = false;
		if (Current.kind == 7 && PlayerPolicies != nullptr && Current.policy_id >= 0
			&& Current.policy_id < static_cast<int32_t>(PlayerPolicies->size()))
		{
			I.bExploiter = I.Exploiter.Bind((*PlayerPolicies)[static_cast<size_t>(Current.policy_id)]);
			if (I.bExploiter) { I.Exploiter.BeginSession(static_cast<int32_t>(I.Rng.GetUnsignedInt() & 0x7fffffffu)); }
		}
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
			ApplyKeeperHealth(I.Enc, I.Identity, I.Cfg.immortal != 0);
			I.Arena.Reset(I.Rng.FRandRange(I.Cfg.start_distance_min, I.Cfg.start_distance_max));
			I.Bot.Reset(I.Profile, BotSeed(Seed));
			I.Bot.SetMemory(&I.BotMem);
			I.Bot.SetHabitPhase(I.SwitchAfter >= 0 && I.Session.BossSwingsSeen >= I.SwitchAfter ? 1 : 0);
			I.Obs.Config.TargetSwingsPerMin = I.Cfg.target_spm;
			I.Obs.Config.Skill = I.Skill;
			I.Obs.Config.Identity = I.Identity;
			I.Obs.BeginEncounter(&I.Session, I.Enc.Duel, I.Arena.Geometry());
			if (I.bExploiter) { I.Exploiter.BeginFight(I.Enc.Duel, I.Arena.Geometry(), I.Skill, I.Identity, I.Session.FightIndex); }
			I.BiteSwing = -1;
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
		const FDuelGeometry Geo = I.Arena.Geometry();
		if (I.bExploiter) { I.Exploiter.Act(I.Enc.Duel, I.Arena, Fwd, Lat); }
		else { I.Bot.Act(I.Enc.Duel, I.Arena, Fwd, Lat); }
		I.Arena.LatchCommits(I.Enc.Duel);
		I.Arena.SnapshotPreStep(I.Enc.Duel);
		I.Enc.StepFrame(I.Arena, I.bExploiter ? I.Exploiter.MovementSym() : I.Bot.MovementSym());
		if (I.bExploiter) { I.Exploiter.RecordFrame(I.Enc.FrameEvents(), I.Enc.Duel, Geo); }
		else { I.Bot.OnEvents(I.Enc.FrameEvents(), I.Enc.Duel); }
		CountStyleEvents(I.Enc.FrameEvents(), I.Enc.Duel, I.BiteSwing, R.StyleEvents);
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
		R.Identity = I.Identity;
		R.Skill = I.Skill;
		const float PrevDealt = I.Enc.Stats.PlayerDamageTaken;
		const float PrevTaken = I.Enc.Stats.BossDamageTaken;
		const int32_t PrevHits = I.Enc.Stats.BossOutcomes[static_cast<int32_t>(EHitOutcome::Hit)];
		const int32_t PrevAttacks = I.Obs.AttacksCommitted();
		const int32_t PrevReads = I.Obs.ReadCountersLanded();

		// Paused at the start of a decision frame (its geometry already latched): act, then finish the frame.
		I.Obs.ApplyAction(I.Enc.Duel, Action, Read);
		FrameRest(R);
		// The habit table the player answers this step's swing with: a switch fires on the commit frame of swing
		// #switch_after (BossSwingsSeen counts it at the commit) and the player plans its answer no earlier than 4 frames
		// later — so the phase after the first frame is the table that answers (review finding: read before, it lagged).
		R.HabitPhase = I.Bot.GetHabitPhase();
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
		R.Hits = I.Enc.Stats.BossOutcomes[static_cast<int32_t>(EHitOutcome::Hit)] - PrevHits;
		R.Swings = I.Obs.AttacksCommitted() - PrevAttacks;
		const int32_t Label = I.Obs.AnswerToLastDecision();
		R.AuxLabel = static_cast<int8_t>(Label >= 0 && Label < RL::NumAnswerClasses ? Label : -1);
		R.bReadCounter = I.Obs.ReadCountersLanded() > PrevReads;
		// Normalised by the TUNED maxima (the keeper's identity scales its pool; a point of damage is worth the same).
		R.Reward = R.DmgDealt / K.PlayerHealthMax - R.DmgTaken / K.BossHealthMax;
		R.Style = StyleReward(I.Identity, R.StyleEvents, R.bReadCounter, I.Cfg.style_scale);
		R.Reward += R.Style;
		// The aggression floor as RL.md section 4.5 writes it: a per-window hinge, max(0, target - actual), so a surplus
		// against one player cannot pay for a deficit against another (a linear swing debt let the keeper stop swinging
		// into parry-happy players). Rate = the last 30 s of this fight; the first 15 s are exempt (every fight opens
		// with an approach). Target = the fight's skill's (RL::TargetSwingsPerMin). Units: missing swings/min / 60,
		// times the step's seconds.
		if (I.Enc.Duel.Frame >= CostWarmupFrames)
		{
			const float Deficit = I.Obs.TargetSwingsPerMin() - I.Obs.SwingsPerMinute(I.Enc.Duel.Frame);
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
	// The batch
	// ==============================================================================================

	namespace
	{
		int32_t PoolThreads(int32_t Requested, int32_t NumEnvs)
		{
			const int32_t Hw = static_cast<int32_t>(std::thread::hardware_concurrency());
			const int32_t T = Requested > 0 ? Requested : (Hw > 0 ? Hw : 1);
			return std::max(1, std::min(T, NumEnvs));
		}

		void WriteStep(const FRLStepResult& R, size_t K, HWRLStepOut& Out)
		{
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
			if (Out.identity) { Out.identity[K] = R.Identity; }
			if (Out.skill) { Out.skill[K] = R.Skill; }
			if (Out.style) { Out.style[K] = R.Style; }
			if (Out.style_events) { std::memcpy(Out.style_events + K * 4, R.StyleEvents, 4); }
			if (Out.hits) { Out.hits[K] = R.Hits; }
		}
	}

	FRLEnvBatch::FRLEnvBatch(int32_t NumEnvs, uint64_t Seed, const HWRLEnvConfig& Cfg)
		: Pool(PoolThreads(Cfg.num_threads, NumEnvs > 0 ? NumEnvs : 1)), BaseSeed(Seed)
	{
		const int32_t N = NumEnvs > 0 ? NumEnvs : 1;
		Envs.reserve(static_cast<size_t>(N));
		PlayerPolicies.reserve(64); // the envs keep a pointer to this vector: never reallocate under them
		for (int32_t I = 0; I < N; ++I)
		{
			Envs.push_back(std::make_unique<FRLEnv>());
			Envs.back()->Configure(Cfg);
			Envs.back()->SetPlayerPolicies(&PlayerPolicies);
		}
	}

	FRLEnvBatch::~FRLEnvBatch() = default;

	int32_t FRLEnvBatch::AddPlayerPolicy(const FRLPolicy* Policy)
	{
		FExploiterDriver Probe;
		if (!Probe.Bind(Policy) || PlayerPolicies.size() >= PlayerPolicies.capacity()) { return -1; }
		PlayerPolicies.push_back(Policy);
		return static_cast<int32_t>(PlayerPolicies.size()) - 1;
	}

	void FRLEnvBatch::SetNextPlayer(int32_t Env, const HWRLPlayerSpec& Spec)
	{
		if (Env >= 0 && Env < Size()) { Envs[static_cast<size_t>(Env)]->SetNextPlayer(Spec); }
	}

	void FRLEnvBatch::Reset()
	{
		Pool.ParallelFor(Size(), [this](int32_t I) { Envs[static_cast<size_t>(I)]->Reset(SplitMix64(BaseSeed + static_cast<uint64_t>(I))); });
	}

	void FRLEnvBatch::Observe(float* Obs, int8_t* Tokens, uint8_t* Mask)
	{
		Pool.ParallelFor(Size(), [&](int32_t I)
		{
			const size_t K = static_cast<size_t>(I);
			Envs[K]->Observe(Obs + K * RL::ObsDim, Tokens + K * RL::HistoryTokens * RL::TokenFields, Mask + K * RL::NumActions);
		});
	}

	void FRLEnvBatch::Step(const int32_t* Actions, const int8_t* AuxTop, const float* AuxTopP, HWRLStepOut& Out)
	{
		Pool.ParallelFor(Size(), [&](int32_t I)
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
			WriteStep(Envs[K]->Step(Actions[K], ReadPtr), K, Out);
		});
	}

	// ==============================================================================================
	// Evaluation with the brains in C++ (RL.md §7)
	// ==============================================================================================

	namespace
	{
		/** One session of a brain against a spec's player, the env's frame loop and habit switches. Calls OnFrame(Enc,
		 *  Arena, Fight, SessionSwings, HabitPhase) after every frame. */
		template <typename FOnFrame>
		void RunEvalSession(IBossBrain* Brain, const FBotProfile& Profile, int32_t SwitchAfter, int32_t Fights, int32_t MaxFrames,
		bool bImmortal, int32_t Identity, FRandom& Rng, FBotMemory& Mem, HWRLEvalStats& S, FBotDiag* OutBotDiag, FOnFrame&& OnFrame)
		{
			int32_t SessionSwings = 0;
			Mem.Reset();
			for (int32_t F = 0; F < Fights; ++F)
			{
				const int32_t FightSeed = static_cast<int32_t>(Rng.GetUnsignedInt() & 0x7fffffffu);
				FEncounter Enc;
				Enc.bRecordRows = false;
				Enc.Begin(Brain, FightSeed, bImmortal);
				ApplyKeeperHealth(Enc, Identity, bImmortal);
				FSimArena Arena;
				Arena.Reset(Rng.FRandRange(450.f, 800.f));
				FPlayerBot Bot;
				Bot.Reset(Profile, BotSeed(FightSeed));
				Bot.SetMemory(&Mem);
				Bot.SetHabitPhase(SwitchAfter >= 0 && SessionSwings >= SwitchAfter ? 1 : 0);
				int32_t BiteSwing = -1;
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
					uint8_t Ev[4] = {};
					CountStyleEvents(Enc.FrameEvents(), Enc.Duel, BiteSwing, Ev);
					for (int32_t K = 0; K < 4; ++K) { S.style_events[K] += Ev[K]; }
					for (const FDuelEvent& E : Enc.FrameEvents())
					{
						if (E.Type == EDuelEvent::Commit && E.Side == ESide::Boss)
						{
							const int32_t A = RL::MoveAction(E.Move);
							if (A >= 0) { ++S.boss_moves[A]; }
							S.distance_sum += Arena.Distance();
							++S.distance_samples;
							if (Move(E.Move).IsAttack()) { ++SessionSwings; }
						}
					}
					Arena.Integrate(Enc.Duel, Fwd, Lat);
					OnFrame(Enc, F, SessionSwings, Bot);
					// The same rule and timing as the training env: table B answers from swing #SwitchAfter on.
					if (SwitchAfter >= 0 && Bot.GetHabitPhase() == 0 && SessionSwings >= SwitchAfter) { Bot.SetHabitPhase(1); }
					if (Enc.IsOver()) { break; }
				}
				if (OutBotDiag != nullptr) { OutBotDiag->Add(Bot.Diag()); }
				const FEncounterStats& St = Enc.Stats;
				++S.fights;
				S.player_deaths += St.bPlayerDied ? 1 : 0;
				S.boss_deaths += St.bBossDied ? 1 : 0;
				S.timeouts += (!St.bPlayerDied && !St.bBossDied) ? 1 : 0;
				S.seconds += static_cast<float>(St.Frames) / static_cast<float>(FramesPerSecond);
				S.player_dmg_taken += St.PlayerDamageTaken;
				S.boss_dmg_taken += St.BossDamageTaken;
				S.boss_swings += St.BossSwings;
				S.player_swings += St.PlayerSwings;
				S.boss_hits += St.BossOutcomes[static_cast<int32_t>(EHitOutcome::Hit)];
				S.boss_whiffs += St.BossOutcomes[static_cast<int32_t>(EHitOutcome::Whiff)];
				S.boss_blocked += St.BossOutcomes[static_cast<int32_t>(EHitOutcome::Blocked)];
				S.boss_parried += St.BossOutcomes[static_cast<int32_t>(EHitOutcome::Parried)];
				S.decisions += St.Decisions;
				S.read_counters += St.CountersLanded;
			}
		}

		void AddStats(HWRLEvalStats& Dst, const HWRLEvalStats& Src)
		{
			Dst.fights += Src.fights; Dst.player_deaths += Src.player_deaths; Dst.boss_deaths += Src.boss_deaths;
			Dst.timeouts += Src.timeouts; Dst.seconds += Src.seconds; Dst.player_dmg_taken += Src.player_dmg_taken;
			Dst.boss_dmg_taken += Src.boss_dmg_taken; Dst.boss_swings += Src.boss_swings; Dst.boss_hits += Src.boss_hits;
			Dst.boss_whiffs += Src.boss_whiffs; Dst.boss_blocked += Src.boss_blocked; Dst.boss_parried += Src.boss_parried;
			Dst.decisions += Src.decisions; Dst.read_counters += Src.read_counters; Dst.player_swings += Src.player_swings;
			for (int32_t K = 0; K < 22; ++K) { Dst.boss_moves[K] += Src.boss_moves[K]; }
			for (int32_t K = 0; K < 4; ++K) { Dst.style_events[K] += Src.style_events[K]; }
			Dst.distance_sum += Src.distance_sum; Dst.distance_samples += Src.distance_samples;
		}
	}

	HWRLEvalStats EvalSessions(int32_t Arm, const FRLPolicy* Policy, const HWRLPlayerSpec& Spec, int32_t Sessions, uint64_t Seed,
		bool bImmortal, int32_t FightSeconds, int32_t ScriptIndex, float Temperature, int32_t MinSwingGap, FBotDiag* OutBotDiag)
	{
		HWRLEvalStats S{};
		if (Arm == 2 && (Policy == nullptr || !Policy->IsBossPlayable())) { return S; }
		FRandom Rng(SeedFrom(Seed));
		const FBotProfile Profile = MakeProfileFromSpec(Spec);
		const int32_t SwitchAfter = Profile.Kind == EBotKind::Habit ? Spec.habit_switch_after : -1;
		const int32_t Fights = std::clamp(Spec.fights_in_session, 1, 4);
		const int32_t MaxFrames = (FightSeconds > 0 ? FightSeconds : 180) * FramesPerSecond;
		const int32_t Identity = Arm == 2 ? SpecKeeperIdentity(Spec) : 0;

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
		RL.Configure(SpecKeeperSkill(Spec), Identity, MinSwingGap);
		RL.SetTemperature(Temperature);
		FRLSession Session;
		FBotMemory Mem;
		IBossBrain* Brain = Arm == 0 ? static_cast<IBossBrain*>(&Script) : (Arm == 1 ? static_cast<IBossBrain*>(&Classic) : static_cast<IBossBrain*>(&RL));

		for (int32_t Sess = 0; Sess < Sessions; ++Sess)
		{
			Model = std::make_unique<FPlaystyleModel>();
			Classic.BindModel(Model.get());
			Session.Reset();
			RL.Bind(Policy, &Session);
			RunEvalSession(Brain, Profile, SwitchAfter, Fights, MaxFrames, bImmortal, Identity, Rng, Mem, S, OutBotDiag,
				[](const FEncounter&, int32_t, int32_t, const FPlayerBot&) {});
		}
		return S;
	}

	int32_t EvalAttackLog(const FRLPolicy& Policy, const HWRLPlayerSpec* Specs, int32_t NumSpecs, int32_t Sessions, uint64_t Seed,
		bool bImmortal, int32_t FightSeconds, int32_t Threads, HWRLAttackRecord* Out, int32_t MaxRecords, HWRLEvalStats* OutStats)
	{
		if (Specs == nullptr || NumSpecs <= 0 || Sessions <= 0 || !Policy.IsBossPlayable()) { return -1; }
		std::vector<std::vector<HWRLAttackRecord>> Logs(static_cast<size_t>(Sessions));
		std::vector<HWRLEvalStats> Stats(static_cast<size_t>(Sessions));
		const int32_t MaxFrames = (FightSeconds > 0 ? FightSeconds : 180) * FramesPerSecond;
		FWorkerPool Pool(PoolThreads(Threads, Sessions));
		Pool.ParallelFor(Sessions, [&](int32_t Sess)
		{
			// Everything a session touches is its own (the policy is shared const; the brain has its own scratch).
			const HWRLPlayerSpec& Spec = Specs[Sess % NumSpecs];
			const FBotProfile Profile = MakeProfileFromSpec(Spec);
			const int32_t SwitchAfter = Profile.Kind == EBotKind::Habit ? Spec.habit_switch_after : -1;
			const int32_t Identity = SpecKeeperIdentity(Spec);
			FRandom Rng(SeedFrom(Seed + static_cast<uint64_t>(Sess) * 0x9E37ull));
			FRLSession Session;
			FRLBrain Brain;
			Brain.Bind(&Policy, &Session);
			Brain.Configure(SpecKeeperSkill(Spec), Identity);
			FBotMemory Mem;
			std::vector<HWRLAttackRecord>& Log = Logs[static_cast<size_t>(Sess)];
			int32_t Pending = -1; // the record of the keeper's swing in flight
			int32_t PendingCommit = -1;
			RunEvalSession(&Brain, Profile, SwitchAfter, std::clamp(Spec.fights_in_session, 1, 4), MaxFrames, bImmortal, Identity, Rng, Mem,
				Stats[static_cast<size_t>(Sess)], nullptr,
				[&](const FEncounter& Enc, int32_t Fight, int32_t SessionSwings, const FPlayerBot& Bot)
				{
					for (const FDuelEvent& E : Enc.FrameEvents())
					{
						if (E.Type == EDuelEvent::Commit && E.Side == ESide::Boss && Move(E.Move).IsAttack())
						{
							HWRLAttackRecord R{};
							R.session = Sess;
							R.k = SessionSwings;
							R.action = RL::MoveAction(E.Move);
							R.habit_phase = -1; // filled in when the player plans its answer to this swing
							R.fight = Fight;
							Log.push_back(R);
							Pending = static_cast<int32_t>(Log.size()) - 1;
							PendingCommit = E.Frame;
						}
						else if (E.Type == EDuelEvent::Outcome && E.Side == ESide::Boss && Pending >= 0)
						{
							HWRLAttackRecord& R = Log[static_cast<size_t>(Pending)];
							if (R.outcome == 0 && RL::MoveAction(E.Move) == R.action)
							{
								R.outcome = static_cast<int32_t>(E.Outcome);
								R.dealt = E.Damage > 0.f ? 1 : 0;
							}
						}
					}
					// The table the player ACTUALLY answered this swing with: the phase it planned with (review finding: inferring
					// it from the swing counter made the reading test's switch check true by construction).
					if (Pending >= 0 && Bot.PlannedSwingFrame() == PendingCommit && Bot.PlannedHabitPhase() >= 0)
					{
						Log[static_cast<size_t>(Pending)].habit_phase = Bot.PlannedHabitPhase();
					}
					if (Enc.IsOver()) { Pending = -1; }
				});
		});
		int32_t N = 0;
		HWRLEvalStats Total{};
		for (int32_t Sess = 0; Sess < Sessions; ++Sess)
		{
			AddStats(Total, Stats[static_cast<size_t>(Sess)]);
			for (const HWRLAttackRecord& R : Logs[static_cast<size_t>(Sess)])
			{
				if (Out != nullptr && N < MaxRecords) { Out[N] = R; }
				++N;
			}
		}
		if (OutStats != nullptr) { *OutStats = Total; }
		return Out != nullptr && N > MaxRecords ? MaxRecords : N;
	}

	// ==============================================================================================
	// RL-4: the exploiter's training env
	// ==============================================================================================

	struct FRLPlayerEnv::FImpl
	{
		HWRLEnvConfig Cfg{ 180, 66.f, 450.f, 800.f, 0, 0, 0.f, 0 };
		const FRLPolicy* Boss = nullptr;
		FEncounter Enc;
		FSimArena Arena;
		FPlayerAgent Agent;
		FRLSession Session;
		FRLBrain Keeper;
		FRandom Rng;
		int32_t FightInSession = 0;
		int32_t FightsInSession = 1;
		float Fwd = 0.f;
		float Lat = 0.f;
	};

	FRLPlayerEnv::FRLPlayerEnv() : Impl(std::make_unique<FImpl>()) { Impl->Enc.bRecordRows = false; }
	FRLPlayerEnv::~FRLPlayerEnv() = default;

	void FRLPlayerEnv::Configure(const HWRLEnvConfig& InCfg, const FRLPolicy* InBoss)
	{
		Impl->Cfg = InCfg;
		if (Impl->Cfg.max_fight_seconds <= 0) { Impl->Cfg.max_fight_seconds = 180; }
		if (Impl->Cfg.start_distance_max < Impl->Cfg.start_distance_min) { Impl->Cfg.start_distance_max = Impl->Cfg.start_distance_min; }
		Impl->Boss = InBoss;
	}

	void FRLPlayerEnv::Reset(uint64_t Seed)
	{
		Impl->Rng.Initialize(SeedFrom(Seed));
		StartSession();
	}

	void FRLPlayerEnv::StartSession()
	{
		FImpl& I = *Impl;
		Current = Next;
		Current.Skill = Clamp01(Current.Skill);
		if (Current.Identity < 0 || Current.Identity >= RL::NumKeepers) { Current.Identity = 0; }
		I.Session.Reset();
		I.Keeper.Bind(I.Boss, &I.Session);
		I.Keeper.Configure(Current.Skill, Current.Identity);
		I.FightInSession = 0;
		I.FightsInSession = std::clamp(Current.Fights, 1, 4);
		StartFight();
	}

	void FRLPlayerEnv::StartFight()
	{
		FImpl& I = *Impl;
		for (int32_t Guard = 0; Guard < 8; ++Guard)
		{
			const int32_t Seed = static_cast<int32_t>(I.Rng.GetUnsignedInt() & 0x7fffffffu);
			I.Enc.Begin(&I.Keeper, Seed, I.Cfg.immortal != 0);
			ApplyKeeperHealth(I.Enc, Current.Identity, I.Cfg.immortal != 0);
			I.Arena.Reset(I.Rng.FRandRange(I.Cfg.start_distance_min, I.Cfg.start_distance_max));
			I.Agent.BeginFight(I.Enc.Duel, I.Arena.Geometry(), Current.Skill, Current.Identity, I.Session.FightsBegun);
			I.Fwd = I.Lat = 0.f;
			FRLStepResult Scratch;
			while (!FightOver())
			{
				FrameStart();
				if (I.Agent.IsDecisionPoint(I.Enc.Duel)) { return; }
				FrameRest(Scratch);
			}
		}
	}

	void FRLPlayerEnv::FrameStart()
	{
		Impl->Enc.ThinkBoss(Impl->Arena.Geometry()); // the frozen keeper decides first (it never sees this frame's input)
	}

	void FRLPlayerEnv::FrameRest(FRLStepResult& R)
	{
		FImpl& I = *Impl;
		const FDuelGeometry Geo = I.Arena.Geometry();
		I.Agent.HoldState(I.Enc.Duel);
		I.Arena.LatchCommits(I.Enc.Duel);
		I.Arena.SnapshotPreStep(I.Enc.Duel);
		I.Enc.StepFrame(I.Arena, I.Agent.MovementSym());
		I.Agent.RecordFrame(I.Enc.FrameEvents(), I.Enc.Duel, Geo);
		I.Arena.Integrate(I.Enc.Duel, I.Agent.WalkForward(), I.Agent.WalkLateral());
		++R.Frames;
	}

	bool FRLPlayerEnv::FightOver() const
	{
		const FImpl& I = *Impl;
		return I.Enc.IsOver() || I.Enc.Duel.Frame >= I.Cfg.max_fight_seconds * FramesPerSecond;
	}

	void FRLPlayerEnv::Observe(float* OutObs, uint8_t* OutMask) const
	{
		Impl->Agent.BuildObservation(Impl->Enc.Duel, Impl->Arena.Geometry(), OutObs);
		Impl->Agent.BuildMask(Impl->Enc.Duel, OutMask);
	}

	FRLStepResult FRLPlayerEnv::Step(int32_t Action)
	{
		FImpl& I = *Impl;
		FRLStepResult R;
		R.Tag = Current.Tag;
		R.FightIndex = I.Session.FightIndex;
		R.Identity = Current.Identity;
		R.Skill = Current.Skill;
		const float PrevDealt = I.Enc.Stats.BossDamageTaken;
		const float PrevTaken = I.Enc.Stats.PlayerDamageTaken;
		const int32_t PrevSwings = I.Enc.Stats.PlayerSwings;
		const int32_t PrevHits = I.Enc.Stats.PlayerOutcomes[static_cast<int32_t>(EHitOutcome::Hit)];

		I.Agent.ApplyAction(I.Enc.Duel, I.Arena, Action);
		FrameRest(R);
		bool bOver = FightOver();
		while (!bOver)
		{
			FrameStart();
			if (I.Agent.IsDecisionPoint(I.Enc.Duel)) { break; }
			FrameRest(R);
			bOver = FightOver();
		}
		const FCombatTuning& K = Tuning();
		R.DmgDealt = I.Enc.Stats.BossDamageTaken - PrevDealt;
		R.DmgTaken = I.Enc.Stats.PlayerDamageTaken - PrevTaken;
		R.Swings = I.Enc.Stats.PlayerSwings - PrevSwings;
		R.Hits = I.Enc.Stats.PlayerOutcomes[static_cast<int32_t>(EHitOutcome::Hit)] - PrevHits;
		// The exploiter's job is to find holes, not to survive: the keeper's health counts double, and staying out of
		// reach costs a little every decision (without both it learned to run circles and stall — its "best" answer to a
		// keeper it could not yet beat).
		R.Reward = 2.f * R.DmgDealt / (K.BossHealthMax * RL::KeeperHealthScale(Current.Identity)) - R.DmgTaken / K.PlayerHealthMax;
		if (I.Arena.Distance() > 400.f) { R.Reward -= 0.01f; }
		if (bOver)
		{
			R.bFightDone = true;
			// A timeout is half a loss for the exploiter: surviving is the keeper's job, not the challenger's (without this
			// it learned to back away and stall until the clock ran out).
			if (I.Enc.Stats.bBossDied) { R.Result = 2; R.Reward += 1.f; }
			else if (I.Enc.Stats.bPlayerDied) { R.Result = 1; R.Reward -= 1.f; }
			else { R.Result = 3; R.Reward -= 0.5f; }
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

	FRLPlayerEnvBatch::FRLPlayerEnvBatch(int32_t NumEnvs, uint64_t Seed, const HWRLEnvConfig& Cfg, const FRLPolicy* Boss)
		: Pool(PoolThreads(Cfg.num_threads, NumEnvs > 0 ? NumEnvs : 1)), BaseSeed(Seed)
	{
		const int32_t N = NumEnvs > 0 ? NumEnvs : 1;
		Envs.reserve(static_cast<size_t>(N));
		for (int32_t I = 0; I < N; ++I)
		{
			Envs.push_back(std::make_unique<FRLPlayerEnv>());
			Envs.back()->Configure(Cfg, Boss);
		}
	}

	void FRLPlayerEnvBatch::SetNextKeeper(int32_t Env, const FRLKeeperSpec& Spec)
	{
		if (Env >= 0 && Env < Size()) { Envs[static_cast<size_t>(Env)]->SetNextKeeper(Spec); }
	}

	void FRLPlayerEnvBatch::Reset()
	{
		Pool.ParallelFor(Size(), [this](int32_t I) { Envs[static_cast<size_t>(I)]->Reset(SplitMix64(BaseSeed ^ 0xC0FFEEull) + static_cast<uint64_t>(I)); });
	}

	void FRLPlayerEnvBatch::Observe(float* Obs, uint8_t* Mask)
	{
		Pool.ParallelFor(Size(), [&](int32_t I)
		{
			const size_t K = static_cast<size_t>(I);
			Envs[K]->Observe(Obs + K * RLPlayer::ObsDim, Mask + K * RLPlayer::NumActions);
		});
	}

	void FRLPlayerEnvBatch::Step(const int32_t* Actions, HWRLStepOut& Out)
	{
		Pool.ParallelFor(Size(), [&](int32_t I)
		{
			const size_t K = static_cast<size_t>(I);
			WriteStep(Envs[K]->Step(Actions[K]), K, Out);
		});
	}
}
