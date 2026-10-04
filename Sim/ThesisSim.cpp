// HellwalkerRL — B0: headless thesis simulator (PLAN §3 B0, RL.md §7). No Unreal.
//
// Compiles the SAME engine-free core the game module compiles (Source/HellwalkerRL/.../HWCore), plus the classic
// reference brain (Sim/Classic — tools only), and asks the question that gates the adaptive boss: against a skilled
// player, is it at least as dangerous as the scripted one (Pathbreaker) — without trading worse, without turtling —
// and are both lethal to a masher?
//
//   ThesisSim                         run the B0 sweep for the adaptive arm and print the verdict (exit 0 = PASS)
//   ThesisSim --brain classic|rl      which adaptive arm (default classic; rl needs --policy)
//   ThesisSim --policy <file.hwrl>    the RL keeper's exported weights
//   ThesisSim --tests                 every core done-test (game core, RL, classic)
//   ThesisSim --csv <file>            also dump one adaptive session's exchange rows (A2 format)
//   ThesisSim --sessions N            sessions per cell (default 4)
//   ThesisSim --script N              the Pathbreaker script (0 the Warden, 1 the Sage)
//   ThesisSim --keeper-damage X       FDuel::KeeperDamageScale in every encounter (default 1 = the rules as trained)
//   ThesisSim --arc --policy <hwrl>   the Adaptive AI learning arc: sessions of 6 lethal fights with the session memory,
//                                     notebook and FRLInsight carried across them as in the game, against habit
//                                     players, switchers and near-random players. --arc-lo / --arc-hi: the difficulty's
//                                     skill range (default the Normal preset 0 / 0.7); --keeper-damage (default 0.75 here);
//                                     --identity; --sessions (default 64); --arc-fixed I holds Insight at I (diagnostic)

#include "HWCore/HWCoreTests.h"
#include "HWCore/HWScriptBrain.h"
#include "HWCore/HWSim.h"
#include "Classic/HWClassicTests.h"
#include "Classic/HWPayoffTable.h"
#include "SimArms.h"

#include "HWCore/HWRLBrain.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace HW;

namespace
{
	int32_t GScript = 0; // --script N: which boss script the thesis is tested on (0 Warden, 1 Sage)
	float GKeeperDamage = 1.f; // --keeper-damage X: FDuel::KeeperDamageScale in every B0 encounter (1 = the rules as trained)

	struct FCell
	{
		float DmgPerMin[3] = {};   // per encounter index (session-persistent memory)
		float SwingsPerMin[3] = {};
		float Counters[3] = {};
		float SubRate[3] = {};
		float BossDmgPerMin[3] = {};
	};

	constexpr int32_t EncountersPerSession = 3;
	constexpr int32_t EncounterSeconds = 90;

	FCell RunCell(FSimArm& Arm, EBotKind Kind, float Skill, int32_t Sessions)
	{
		FCell C;
		for (int32_t S = 0; S < Sessions; ++S)
		{
			Arm.NewSession();
			for (int32_t E = 0; E < EncountersPerSession; ++E)
			{
				FRunConfig Cfg;
				Cfg.Bot = MakeBotProfile(Kind, Skill);
				Cfg.Seed = 10007 * (S + 1) + 131 * E + static_cast<int32_t>(Kind) * 17 + static_cast<int32_t>(Skill * 100.f);
				Cfg.bImmortal = true;
				Cfg.MaxFrames = EncounterSeconds * FramesPerSecond;
				Cfg.KeeperDamageScale = GKeeperDamage;
				const FEncounterStats St = Arm.Run(Cfg);
				C.DmgPerMin[E] += St.DamagePerMin() / static_cast<float>(Sessions);
				C.SwingsPerMin[E] += St.SwingsPerMin() / static_cast<float>(Sessions);
				C.Counters[E] += static_cast<float>(St.CountersLanded) / static_cast<float>(Sessions);
				C.SubRate[E] += (St.Decisions > 0 ? static_cast<float>(St.DecisionKinds[static_cast<int32_t>(EDecisionKind::Substitution)]) / static_cast<float>(St.Decisions) : 0.f) / static_cast<float>(Sessions);
				C.BossDmgPerMin[E] += (St.Frames > 0 ? St.BossDamageTaken * 3600.f / static_cast<float>(St.Frames) : 0.f) / static_cast<float>(Sessions);
			}
		}
		return C;
	}

	float Mean3(const float* V) { return (V[0] + V[1] + V[2]) / 3.f; }

	int RunTestList(const FCoreTest* Tests, int32_t N, int32_t& Failed)
	{
		for (int32_t I = 0; I < N; ++I)
		{
			std::string Log;
			ResetMoveTable();
			const bool bOk = Tests[I].Fn(Log);
			std::printf("[%s] %-32s (%s)\n", bOk ? "PASS" : "FAIL", Tests[I].Name, Tests[I].Milestone);
			std::string Indented;
			for (char Ch : Log) { Indented += Ch; if (Ch == '\n') { Indented += "      "; } }
			std::printf("      %s\n", Indented.c_str());
			if (!bOk) { ++Failed; }
		}
		return N;
	}

	int RunTests()
	{
		int32_t Failed = 0;
		int32_t Total = 0;
		const FCoreTest* Tests = nullptr;
		Total += RunTestList(Tests, GetCoreTests(Tests), Failed);
		Total += RunTestList(Tests, GetClassicTests(Tests), Failed);
		Total += RunExtraTests(Failed);
		std::printf("%d/%d core tests passed\n", Total - Failed, Total);
		return Failed == 0 ? 0 : 1;
	}

	int RunDiag(FSimArm& Script, FSimArm& Adaptive, EBotKind Kind, float Skill, bool bLethal)
	{
		FSimArm* Arms[2] = { &Script, &Adaptive };
		Adaptive.SetReferenceSwingsPerMin(64.f);
		for (int32_t M = 0; M < 2; ++M)
		{
			FSimArm& Arm = *Arms[M];
			Arm.NewSession();
			FEncounterStats Sum;
			float Secs = 0.f;
			int32_t Deaths = 0;
			int32_t Kills = 0;
			for (int32_t E = 0; E < 3; ++E)
			{
				FRunConfig Cfg;
				Cfg.Bot = MakeBotProfile(Kind, Skill);
				Cfg.Seed = 91 + E;
				Cfg.bImmortal = !bLethal;
				Cfg.MaxFrames = (bLethal ? 240 : EncounterSeconds) * FramesPerSecond;
				Cfg.KeeperDamageScale = GKeeperDamage;
				const FEncounterStats S = Arm.Run(Cfg);
				Secs += S.Frames / 60.f;
				Deaths += S.bPlayerDied ? 1 : 0;
				Kills += S.bBossDied ? 1 : 0;
				Sum.PlayerDamageTaken += S.PlayerDamageTaken;
				Sum.BossDamageTaken += S.BossDamageTaken;
				Sum.BossSwings += S.BossSwings;
				Sum.PlayerSwings += S.PlayerSwings;
				Sum.Decisions += S.Decisions;
				Sum.PlayerGuardBreaks += S.PlayerGuardBreaks;
				Sum.BossExposed += S.BossExposed;
				Sum.Symbols += S.Symbols;
				for (int32_t I = 0; I < 5; ++I) { Sum.BossOutcomes[I] += S.BossOutcomes[I]; Sum.PlayerOutcomes[I] += S.PlayerOutcomes[I]; }
				for (int32_t I = 0; I < NumDecisionKinds; ++I) { Sum.DecisionKinds[I] += S.DecisionKinds[I]; }
				for (int32_t I = 0; I < NumMoves; ++I)
				{
					Sum.DamageByMove[I] += S.DamageByMove[I];
					Sum.HitsByMove[I] += S.HitsByMove[I];
					Sum.MoveSelect[I] += S.MoveSelect[I];
					Sum.BossDamageAfterMove[I] += S.BossDamageAfterMove[I];
				}
				for (int32_t I = 0; I < static_cast<int32_t>(EDefenderPhase::Count); ++I) { Sum.BossDamageByPhase[I] += S.BossDamageByPhase[I]; }
			}
			const float Min = Secs / 60.f;
			std::printf("== %s vs %s %.1f — %.0f s total, player died %d/3, boss died %d/3\n", Arm.Label(), BotKindName(Kind), Skill, Secs, Deaths, Kills);
			std::printf("   player dmg taken/min %.1f | boss dmg taken/min %.1f | boss swings/min %.1f | player swings/min %.1f | symbols/s %.2f\n",
				Sum.PlayerDamageTaken / Min, Sum.BossDamageTaken / Min, Sum.BossSwings / Min, Sum.PlayerSwings / Min, Sum.Symbols / Secs);
			std::printf("   boss swings:   hit %d  whiff %d  blocked %d  parried %d\n", Sum.BossOutcomes[1], Sum.BossOutcomes[2], Sum.BossOutcomes[3], Sum.BossOutcomes[4]);
			std::printf("   player swings: hit %d  whiff %d  blocked %d  parried %d | guard breaks %d, boss exposed %d\n",
				Sum.PlayerOutcomes[1], Sum.PlayerOutcomes[2], Sum.PlayerOutcomes[3], Sum.PlayerOutcomes[4], Sum.PlayerGuardBreaks, Sum.BossExposed);
			std::printf("   decisions %d:", Sum.Decisions);
			for (int32_t I = 0; I < NumDecisionKinds; ++I) { std::printf(" %s %d", DecisionKindName(static_cast<EDecisionKind>(I)), Sum.DecisionKinds[I]); }
			std::printf("\n   boss hurt while:");
			for (int32_t I = 0; I < static_cast<int32_t>(EDefenderPhase::Count); ++I)
			{
				if (Sum.BossDamageByPhase[I] > 0.f) { std::printf(" %s %.0f", DefenderPhaseName(static_cast<EDefenderPhase>(I)), Sum.BossDamageByPhase[I]); }
			}
			std::printf("\n   by move (selected / hits / damage dealt / boss damage taken after it):\n");
			for (int32_t I = 1; I < NumMoves; ++I)
			{
				if (Sum.MoveSelect[I] == 0 && Sum.HitsByMove[I] == 0) { continue; }
				std::printf("     %-15s sel %4d  hits %4d  dmg %7.1f  taken-after %7.1f\n", Move(static_cast<EMoveId>(I)).Name,
					Sum.MoveSelect[I], Sum.HitsByMove[I], Sum.DamageByMove[I], Sum.BossDamageAfterMove[I]);
			}
		}
		return 0;
	}

	/** Print the derived payoff matrix at typical leads, next to the hand-authored one (classic brain). */
	int DumpPayoffs()
	{
		const FPayoffTable& T = DerivedPayoffs();
		std::printf("derived payoff (typical lead) / authored, per player symbol\n%-15s", "boss move");
		for (int32_t S = 0; S < NumPlayerSymbols; ++S) { std::printf(" %9s", SymName(static_cast<ESym>(S))); }
		std::printf("\n");
		for (int32_t M = 1; M < NumMoves; ++M)
		{
			const FMoveData& Mv = Move(static_cast<EMoveId>(M));
			if (Mv.Owner != ESide::Boss) { continue; }
			std::printf("%-15s", Mv.Name);
			for (int32_t S = 0; S < NumPlayerSymbols; ++S)
			{
				const ESym Sy = static_cast<ESym>(S);
				std::printf(" %+5.1f/%+2.0f", T.GetTypical(Mv.Id, Sy), FClassicBrain::BasePayoff(Mv.Id, Sy));
			}
			std::printf("\n");
		}
		return 0;
	}

	void DumpCsv(const char* Path, FSimArm& Arm)
	{
		FILE* F = nullptr;
		if (fopen_s(&F, Path, "w") != 0 || F == nullptr) { std::printf("cannot write %s\n", Path); return; }
		std::fprintf(F, "%s\n", ExchangeCsvHeader());
		Arm.NewSession();
		for (int32_t E = 0; E < EncountersPerSession; ++E)
		{
			FRunConfig Cfg;
			Cfg.Bot = MakeBotProfile(EBotKind::Habitual, 0.8f);
			Cfg.Seed = 77 + E;
			Cfg.bImmortal = true;
			Cfg.MaxFrames = EncounterSeconds * FramesPerSecond;
			Cfg.KeeperDamageScale = GKeeperDamage;
			std::vector<FExchangeRow> Rows;
			Arm.Run(Cfg, &Rows);
			char RunId[32];
			std::snprintf(RunId, sizeof(RunId), "sim-e%d", E + 1);
			char Line[1024];
			for (const FExchangeRow& R : Rows)
			{
				FormatExchangeRow(R, RunId, EBrainMode::Hellwalker, 1000.f / 60.f, Line, sizeof(Line));
				std::fprintf(F, "%s\n", Line);
			}
		}
		std::fclose(F);
		std::printf("wrote %s\n", Path);
	}

	// ---- --arc: the learning arc of the Adaptive AI mode ----------------------------------------------------------------
	// Sessions of lethal fights against the shipped RL keeper, carrying everything across the fights exactly as the game
	// does: one FRLBrain bound to the session's memory (FRLSession) and notebook, configured before each fight from
	// FRLInsight::KeeperConfig (skill, temperature, breather — fixed for the fight), and the insight updated after each
	// fight from the notebook's read-head calls for that fight. The question: does a trick win the first fights and stop
	// working by the 4th-5th, while a player without a trick keeps the keeper guessing?

	constexpr int32_t ArcFights = 6;
	constexpr int32_t ArcFightSeconds = 180;
	constexpr float ArcHabitNoise = 0.05f; // a consistent habit, not a perfect machine

	/** A habit player's answer to every boss swing class (the FBotProfile::Habit columns). */
	enum class EArcHabit : int32_t { Parry = 0, Block = 1, StepL = 2, StepR = 3, StepB = 4, StepF = 5, Attack = 6, None = 7, Random = -1 };

	enum class EArcKind : int32_t { Habit, Switcher, Random };

	struct FArcPlayer
	{
		std::string Name;
		EArcKind Kind = EArcKind::Habit;
		FBotProfile First;      // the fights before SwitchAt
		FBotProfile Then;       // the fights from SwitchAt on (the same profile for a fixed habit)
		EArcHabit Habit = EArcHabit::Random; // a fixed-habit player's habit (graded cells)
		int32_t SwitchAt = -1;  // fight index (0-based) of the habit switch; -1 = never
	};

	// --arc-tune k=v,...: try FRLInsight's tunables without a rebuild (how they were calibrated; see RL/DESIGN.md).
	struct FArcTune
	{
		bool bOn = false;
		float Floor = FRLInsight::AccuracyFloor, Full = FRLInsight::AccuracyFull, Max = FRLInsight::MaxInsight;
		float Rise = FRLInsight::RiseRate, Fall = FRLInsight::FallRate, Half = FRLInsight::HalfWeightPredictions;
		float Temp0 = FRLInsight::MaxTemperature, TempGone = FRLInsight::TemperatureGoneAt;
		float Gap0 = static_cast<float>(FRLInsight::MaxSwingGap), GapGone = FRLInsight::SwingGapGoneAt;
		float SkillPow = 1.f;
		float Plateau = 0.f;
	};
	FArcTune GTune;
	void TuneAfterFight(FRLInsight& In, int32_t Predictions, int32_t Correct)
	{
		const FArcTune& T = GTune;
		++In.Fights;
		if (Predictions <= 0) { return; }
		const float N = static_cast<float>(Predictions);
		float Acc = static_cast<float>(Correct) / N;
		float Norm = (Acc - T.Floor) / (T.Full - T.Floor);
		Norm = Norm < 0.f ? 0.f : (Norm > 1.f ? 1.f : Norm);
		const float Target = T.Max * Norm;
		const float Rate = (Target >= In.Insight ? T.Rise : T.Fall) * N / (N + T.Half);
		In.Insight += Rate * (Target - In.Insight);
	}
	void TuneKeeperConfig(const FRLInsight& In, float Lo, float Hi, float& OutSkill, float& OutTemp, int32_t& OutGap)
	{
		const FArcTune& T = GTune;
		const float I = In.Insight;
		OutSkill = Lo + (Hi - Lo) * std::pow(I, T.SkillPow);
		const float Loose = I <= T.Plateau ? 1.f : 1.f - (I - T.Plateau) / (T.TempGone - T.Plateau);
		OutTemp = Loose > 0.f ? T.Temp0 * Loose : 0.f;
		const float Breathe = I <= T.Plateau ? 1.f : 1.f - (I - T.Plateau) / (T.GapGone - T.Plateau);
		OutGap = Breathe > 0.f ? static_cast<int32_t>(T.Gap0 * Breathe + 0.5f) : 0;
	}
	bool ParseTune(const char* Arg)
	{
		std::string A(Arg);
		size_t Pos = 0;
		while (Pos < A.size())
		{
			size_t End = A.find(',', Pos);
			if (End == std::string::npos) { End = A.size(); }
			const std::string KV = A.substr(Pos, End - Pos);
			const size_t Eq = KV.find('=');
			if (Eq == std::string::npos) { return false; }
			const std::string K = KV.substr(0, Eq);
			const float V = static_cast<float>(std::atof(KV.c_str() + Eq + 1));
			if (K == "floor") { GTune.Floor = V; } else if (K == "full") { GTune.Full = V; } else if (K == "max") { GTune.Max = V; }
			else if (K == "rise") { GTune.Rise = V; } else if (K == "fall") { GTune.Fall = V; } else if (K == "half") { GTune.Half = V; }
			else if (K == "temp0") { GTune.Temp0 = V; } else if (K == "tempgone") { GTune.TempGone = V; }
			else if (K == "gap0") { GTune.Gap0 = V; } else if (K == "gapgone") { GTune.GapGone = V; } else if (K == "skillpow") { GTune.SkillPow = V; } else if (K == "plateau") { GTune.Plateau = V; }
			else { return false; }
			Pos = End + 1;
		}
		GTune.bOn = true;
		return true;
	}

	struct FArcOptions
	{
		const FRLPolicy* Policy = nullptr;
		float Lo = 0.f;               // the difficulty's skill range (default: the Normal preset)
		float Hi = 0.7f;
		float Damage = 0.75f;         // FDuel::KeeperDamageScale (default: the Normal preset)
		int32_t Identity = 0;
		int32_t Sessions = 64;
		float FixedInsight = -1.f;    // >= 0: hold Insight there for every fight (diagnostic: the strength curve)
		float HoldSkill = -1.f;       // >= 0: hold this keeper config instead of KeeperConfig's (diagnostic)
		float HoldTemperature = 0.f;
		int32_t HoldGap = 0;
		std::vector<float> PlayerSkills = { 0.6f, 0.85f }; // the simulated players' skills (--arc-skills a,b,...)
	};

	/** One fight of one session. */
	struct FArcResult
	{
		bool    bWin = false;          // the keeper fell
		bool    bLoss = false;         // the player fell
		float   Insight = 0.f;         // before the fight
		float   Skill = 0.f;
		float   Temperature = 0.f;
		int32_t Gap = 0;
		int32_t Predictions = 0;
		int32_t Correct = 0;
		int32_t SwingPredictions = 0;  // the calls on its attacks (FRLInsight's input)
		int32_t SwingCorrect = 0;
		float   Seconds = 0.f;
	};

	FBotProfile ArcHabitProfile(EArcHabit Habit, float Skill)
	{
		FBotProfile P = MakeBotProfile(EBotKind::Habit, Skill);
		const int32_t Col = static_cast<int32_t>(Habit);
		for (int32_t T = 0; T < 2; ++T)
		{
			for (int32_t C = 0; C < FBotProfile::HabitClasses; ++C)
			{
				for (int32_t R = 0; R < FBotProfile::HabitResponses; ++R)
				{
					P.Habit[T][C][R] = Habit == EArcHabit::Random ? 1.f : (R == Col ? 1.f : 0.f);
				}
			}
		}
		P.HabitNoise = Habit == EArcHabit::Random ? 0.f : ArcHabitNoise;
		return P;
	}

	const char* ArcHabitName(EArcHabit H)
	{
		switch (H)
		{
		case EArcHabit::Parry:  return "always-parry";
		case EArcHabit::Block:  return "always-block";
		case EArcHabit::StepL:  return "always-step-left";
		case EArcHabit::StepR:  return "always-step-right";
		case EArcHabit::StepB:  return "always-step-back";
		case EArcHabit::StepF:  return "always-step-in";
		case EArcHabit::Attack: return "always-attack";
		case EArcHabit::None:   return "never-answers";
		default:                return "near-random";
		}
	}

	void RunArcSession(const FArcOptions& O, const FArcPlayer& Pl, int32_t Cell, int32_t S, FArcResult* Out)
	{
		// Heap: the brain, its observer and the session memory are large for a worker thread's stack.
		std::unique_ptr<FRLSession> Session = std::make_unique<FRLSession>();
		std::unique_ptr<FRLNotebook> Notebook = std::make_unique<FRLNotebook>();
		std::unique_ptr<FRLBrain> Brain = std::make_unique<FRLBrain>();
		FRLInsight Insight;
		int32_t SwingSeen = 0, SwingCorrectSeen = 0; // the notebook's totals at the last fight's end (as the game takes them)
		Session->Reset();
		Brain->Bind(O.Policy, Session.get(), Notebook.get());
		for (int32_t F = 0; F < ArcFights; ++F)
		{
			if (O.FixedInsight >= 0.f) { Insight.Insight = O.FixedInsight; }
			FArcResult& R = Out[F];
			R.Insight = Insight.Insight;
			if (GTune.bOn) { TuneKeeperConfig(Insight, O.Lo, O.Hi, R.Skill, R.Temperature, R.Gap); } else
			Insight.KeeperConfig(O.Lo, O.Hi, R.Skill, R.Temperature, R.Gap);
			if (O.HoldSkill >= 0.f)
			{
				R.Skill = O.HoldSkill;
				R.Temperature = O.HoldTemperature;
				R.Gap = O.HoldGap;
			}
			Brain->Configure(R.Skill, O.Identity, R.Gap);
			Brain->SetTemperature(R.Temperature);

			FRunConfig Cfg;
			Cfg.Bot = (Pl.SwitchAt >= 0 && F >= Pl.SwitchAt) ? Pl.Then : Pl.First;
			Cfg.Seed = 1 + Cell + 37 * (S * ArcFights + F); // small: RunEncounter multiplies it for the bot's stream
			Cfg.MaxFrames = ArcFightSeconds * FramesPerSecond;
			Cfg.BossHealthScale = RL::KeeperHealthScale(O.Identity);
			Cfg.KeeperDamageScale = O.Damage;
			const FEncounterStats St = RunEncounter(*Brain, Cfg);

			// As the game's EndEncounter: the notebook gets the fight's last answer, then the insight moves.
			Brain->FlushNotebook();
			const int32_t Fi = Session->FightIndex < FRLNotebook::MaxFights ? Session->FightIndex : FRLNotebook::MaxFights - 1;
			R.Predictions = Notebook->FightPredictions[Fi];
			R.Correct = Notebook->FightCorrect[Fi];
			R.SwingPredictions = Notebook->SwingPredictions - SwingSeen;
			R.SwingCorrect = Notebook->SwingCorrect - SwingCorrectSeen;
			SwingSeen = Notebook->SwingPredictions;
			SwingCorrectSeen = Notebook->SwingCorrect;
			R.bWin = St.bBossDied && !St.bPlayerDied;
			R.bLoss = St.bPlayerDied;
			R.Seconds = static_cast<float>(St.Frames) / static_cast<float>(FramesPerSecond);
			if (GTune.bOn) { TuneAfterFight(Insight, R.SwingPredictions, R.SwingCorrect); } else
			Insight.AfterFight(R.SwingPredictions, R.SwingCorrect);
		}
	}

	struct FArcRow
	{
		float Win = 0.f, Loss = 0.f, Insight = 0.f, InsightMax = 0.f, Skill = 0.f, Temperature = 0.f, Gap = 0.f, Read = 0.f, SwingRead = 0.f, Seconds = 0.f;
		float Calls = 0.f, SwingCalls = 0.f;
	};

	/** Per fight index: means over the sessions (read = pooled top-1 accuracy of the fight's calls). */
	void ArcSummarise(const std::vector<FArcResult>& All, int32_t Sessions, FArcRow* Rows)
	{
		for (int32_t F = 0; F < ArcFights; ++F)
		{
			FArcRow& Row = Rows[F];
			double W = 0, L = 0, I = 0, Sk = 0, T = 0, G = 0, Sec = 0;
			long long Pr = 0, Co = 0, SPr = 0, SCo = 0;
			for (int32_t S = 0; S < Sessions; ++S)
			{
				const FArcResult& R = All[static_cast<size_t>(S) * ArcFights + static_cast<size_t>(F)];
				W += R.bWin ? 1 : 0;
				L += R.bLoss ? 1 : 0;
				I += R.Insight;
				if (R.Insight > Row.InsightMax) { Row.InsightMax = R.Insight; }
				Sk += R.Skill;
				T += R.Temperature;
				G += R.Gap;
				Sec += R.Seconds;
				Pr += R.Predictions;
				Co += R.Correct;
				SPr += R.SwingPredictions;
				SCo += R.SwingCorrect;
			}
			const double N = Sessions > 0 ? static_cast<double>(Sessions) : 1.0;
			Row.Win = static_cast<float>(100.0 * W / N);
			Row.Loss = static_cast<float>(100.0 * L / N);
			Row.Insight = static_cast<float>(I / N);
			Row.Skill = static_cast<float>(Sk / N);
			Row.Temperature = static_cast<float>(T / N);
			Row.Gap = static_cast<float>(G / N);
			Row.Seconds = static_cast<float>(Sec / N);
			Row.Read = Pr > 0 ? static_cast<float>(100.0 * static_cast<double>(Co) / static_cast<double>(Pr)) : 0.f;
			Row.SwingRead = SPr > 0 ? static_cast<float>(100.0 * static_cast<double>(SCo) / static_cast<double>(SPr)) : 0.f;
			Row.Calls = static_cast<float>(static_cast<double>(Pr) / N);
			Row.SwingCalls = static_cast<float>(static_cast<double>(SPr) / N);
		}
	}

	int RunArc(const FArcOptions& O)
	{
		if (O.Policy == nullptr) { return 3; }
		std::vector<FArcPlayer> Players;
		const EArcHabit Habits[] = { EArcHabit::Parry, EArcHabit::Block, EArcHabit::StepL, EArcHabit::Attack };
		const std::vector<float>& PlayerSkills = O.PlayerSkills;
		for (float Sk : PlayerSkills)
		{
			for (EArcHabit H : Habits)
			{
				FArcPlayer P;
				P.Name = std::string(ArcHabitName(H));
				P.First = P.Then = ArcHabitProfile(H, Sk);
				P.Habit = H;
				Players.push_back(P);
			}
		}
		for (float Sk : PlayerSkills)
		{
			// Switchers: one trick for fights 1-3, another from fight 4.
			const EArcHabit Pairs[][2] = { { EArcHabit::Block, EArcHabit::Parry }, { EArcHabit::StepL, EArcHabit::Parry },
				{ EArcHabit::Parry, EArcHabit::StepL }, { EArcHabit::StepL, EArcHabit::Block } };
			for (const EArcHabit* Pair : Pairs)
			{
				FArcPlayer P;
				P.Name = std::string("switch ") + ArcHabitName(Pair[0]) + " > " + ArcHabitName(Pair[1]);
				P.First = ArcHabitProfile(Pair[0], Sk);
				P.Then = ArcHabitProfile(Pair[1], Sk);
				P.Kind = EArcKind::Switcher;
				P.SwitchAt = 3;
				Players.push_back(P);
			}
		}
		for (float Sk : PlayerSkills)
		{
			FArcPlayer P;
			P.Name = ArcHabitName(EArcHabit::Random);
			P.Kind = EArcKind::Random;
			P.First = P.Then = ArcHabitProfile(EArcHabit::Random, Sk);
			Players.push_back(P);
		}

		std::printf("arc: %d sessions x %d lethal fights (%d s cap) per player vs the RL keeper (%s), skill %.2f-%.2f, keeper damage x%.2f%s\n",
			O.Sessions, ArcFights, ArcFightSeconds, RL::KeeperName(O.Identity), O.Lo, O.Hi, O.Damage,
			O.HoldSkill >= 0.f ? " — keeper config HELD (diagnostic)" : (O.FixedInsight >= 0.f ? " — Insight HELD (diagnostic)" : ""));
		const FArcTune& Tn = GTune; // the compiled FRLInsight tunables unless --arc-tune overrides some
		std::printf("insight: floor %.2f full %.2f max %.2f rise %.2f fall %.2f half-weight %.0f | temperature %.2f gone at %.2f | gap %.0f gone at %.2f%s\n",
			Tn.Floor, Tn.Full, Tn.Max, Tn.Rise, Tn.Fall, Tn.Half, Tn.Temp0, Tn.TempGone, Tn.Gap0, Tn.GapGone, Tn.bOn ? "  (--arc-tune)" : "");

		bool bTrickOk = true, bRandomOk = true;
		const int32_t Workers = static_cast<int32_t>(std::thread::hardware_concurrency() > 0 ? std::thread::hardware_concurrency() : 4);
		for (int32_t Cell = 0; Cell < static_cast<int32_t>(Players.size()); ++Cell)
		{
			const FArcPlayer& Pl = Players[static_cast<size_t>(Cell)];
			std::vector<FArcResult> All(static_cast<size_t>(O.Sessions) * ArcFights);
			std::atomic<int32_t> Next{ 0 };
			std::vector<std::thread> Pool;
			for (int32_t W = 0; W < Workers; ++W)
			{
				Pool.emplace_back([&]()
				{
					for (int32_t S = Next++; S < O.Sessions; S = Next++)
					{
						RunArcSession(O, Pl, Cell, S, All.data() + static_cast<size_t>(S) * ArcFights);
					}
				});
			}
			for (std::thread& T : Pool) { T.join(); }

			FArcRow Rows[ArcFights];
			ArcSummarise(All, O.Sessions, Rows);
			std::printf("\n== %s, player skill %.2f%s\n", Pl.Name.c_str(), Pl.First.Skill, Pl.SwitchAt >= 0 ? " (switches before fight 4)" : "");
			std::printf("  fight  win%%  lost%%  insight (max)  skill  temp  gap  swing-read%% (calls)  all-read%% (calls)  secs\n");
			for (int32_t F = 0; F < ArcFights; ++F)
			{
				const FArcRow& R = Rows[F];
				std::printf("  %5d %5.1f %6.1f   %5.2f (%4.2f)  %5.2f %5.2f %4.0f       %5.1f (%4.0f)     %5.1f (%4.0f)  %5.1f\n", F + 1, R.Win, R.Loss,
					R.Insight, R.InsightMax, R.Skill, R.Temperature, R.Gap, R.SwingRead, R.SwingCalls, R.Read, R.Calls, R.Seconds);
			}
			// The calibration targets (Normal preset; informational elsewhere).
			if (Pl.Kind == EArcKind::Switcher)
			{
				// Informational: a new trick wins again only where it beats the keeper's counters; its read of you resets.
				std::printf("  switcher: fight 3 win %.0f%% -> fight 4 %.0f%%, swing-read %.0f%% -> %.0f%%, insight %.2f -> %.2f\n", Rows[2].Win, Rows[3].Win,
					Rows[2].SwingRead, Rows[3].SwingRead, Rows[3].Insight, Rows[4].Insight);
			}
			else if (Pl.Kind == EArcKind::Random)
			{
				float MaxI = 0.f, MinWin = 100.f;
				for (int32_t F = 0; F < ArcFights; ++F)
				{
					MaxI = Rows[F].Insight > MaxI ? Rows[F].Insight : MaxI;
					MinWin = Rows[F].Win < MinWin ? Rows[F].Win : MinWin;
				}
				const bool bLow = MaxI < 0.35f;
				std::printf("  near-random: mean insight peaks at %.2f (%s), lowest win rate %.0f%%\n", MaxI, bLow ? "stays low" : "TOO HIGH", MinWin);
				bRandomOk = bRandomOk && bLow;
			}
			else
			{
				bool bRising = true;
				for (int32_t F = 1; F < 5; ++F) { bRising = bRising && Rows[F].Win <= Rows[F - 1].Win + 5.f; } // 5 points of noise
				// The demo's trick players: a clean block or step habit, executed well (an assisted human). The parry habit is
				// shown but not graded: a near-perfect parry bot reads half the feints and is not countered (the parry-and-punish
				// weakness, README); attack-spam never wins in the first place.
				const bool bGraded = (Pl.Habit == EArcHabit::Block || Pl.Habit == EArcHabit::StepL) && Pl.First.Skill >= 0.8f;
				const bool bEarly = Rows[0].Win >= 70.f && Rows[1].Win >= 70.f;
				const bool bFifth = Rows[4].Win <= 30.f;
				std::printf("  habit: fights 1-2 win %.0f%% / %.0f%%, fight 5 win %.0f%%, keeper rising over fights 1-5: %s%s\n", Rows[0].Win, Rows[1].Win,
					Rows[4].Win, bRising ? "yes" : "NO", bGraded ? (bEarly && bFifth && bRising ? "  [graded: PASS]" : "  [graded: FAIL]") : "");
				if (bGraded) { bTrickOk = bTrickOk && bEarly && bFifth && bRising; }
			}
			std::fflush(stdout);
		}
		std::printf("\nARC\n  one trick (block / step, skill >= 0.8): wins fights 1-2, countered by fight 5 ... %s\n", bTrickOk ? "PASS" : "FAIL");
		std::printf("  near-random players: insight stays low .................................... %s\n", bRandomOk ? "PASS" : "FAIL");
		return (bTrickOk && bRandomOk) ? 0 : 2;
	}
}

int main(int Argc, char** Argv)
{
	int32_t Sessions = 4;
	const char* CsvPath = nullptr;
	const char* BrainName = "classic";
	const char* PolicyPath = nullptr;
	float KeeperSkill = 1.f;
	int32_t KeeperIdentity = -1; // -1: follows the script (0 Warden, 1 Sage)
	bool bTests = false;
	bool bAuthored = false;
	int32_t DiagKind = -1;
	float DiagSkill = 0.f;
	bool bDiagLethal = false;
	bool bArc = false;
	bool bSessionsGiven = false;
	bool bDamageGiven = false;
	FArcOptions Arc;
	for (int I = 1; I < Argc; ++I)
	{
		if (std::strcmp(Argv[I], "--tests") == 0) { bTests = true; }
		else if (std::strcmp(Argv[I], "--csv") == 0 && I + 1 < Argc) { CsvPath = Argv[++I]; }
		else if (std::strcmp(Argv[I], "--sessions") == 0 && I + 1 < Argc) { Sessions = std::atoi(Argv[++I]); bSessionsGiven = true; }
		else if (std::strcmp(Argv[I], "--keeper-damage") == 0 && I + 1 < Argc) { GKeeperDamage = static_cast<float>(std::atof(Argv[++I])); bDamageGiven = true; }
		else if (std::strcmp(Argv[I], "--arc") == 0) { bArc = true; }
		else if (std::strcmp(Argv[I], "--arc-lo") == 0 && I + 1 < Argc) { Arc.Lo = static_cast<float>(std::atof(Argv[++I])); }
		else if (std::strcmp(Argv[I], "--arc-hi") == 0 && I + 1 < Argc) { Arc.Hi = static_cast<float>(std::atof(Argv[++I])); }
		else if (std::strcmp(Argv[I], "--arc-fixed") == 0 && I + 1 < Argc) { Arc.FixedInsight = static_cast<float>(std::atof(Argv[++I])); }
		else if (std::strcmp(Argv[I], "--arc-tune") == 0 && I + 1 < Argc) { if (!ParseTune(Argv[++I])) { std::printf("bad --arc-tune\n"); return 3; } }
		else if (std::strcmp(Argv[I], "--arc-skills") == 0 && I + 1 < Argc)
		{
			Arc.PlayerSkills.clear();
			for (const char* C = Argv[++I]; *C != 0;)
			{
				Arc.PlayerSkills.push_back(static_cast<float>(std::atof(C)));
				while (*C != 0 && *C != ',') { ++C; }
				if (*C == ',') { ++C; }
			}
			if (Arc.PlayerSkills.empty()) { Arc.PlayerSkills.push_back(0.85f); }
		}
		else if (std::strcmp(Argv[I], "--arc-hold") == 0 && I + 3 < Argc)
		{
			Arc.HoldSkill = static_cast<float>(std::atof(Argv[++I]));
			Arc.HoldTemperature = static_cast<float>(std::atof(Argv[++I]));
			Arc.HoldGap = std::atoi(Argv[++I]);
		}
		else if (std::strcmp(Argv[I], "--payoffs") == 0) { return DumpPayoffs(); }
		else if (std::strcmp(Argv[I], "--authored") == 0) { bAuthored = true; }
		else if (std::strcmp(Argv[I], "--script") == 0 && I + 1 < Argc) { GScript = std::atoi(Argv[++I]); }
		else if (std::strcmp(Argv[I], "--brain") == 0 && I + 1 < Argc) { BrainName = Argv[++I]; }
		else if (std::strcmp(Argv[I], "--policy") == 0 && I + 1 < Argc) { PolicyPath = Argv[++I]; }
		else if (std::strcmp(Argv[I], "--skill") == 0 && I + 1 < Argc) { KeeperSkill = static_cast<float>(std::atof(Argv[++I])); }
		else if (std::strcmp(Argv[I], "--identity") == 0 && I + 1 < Argc) { KeeperIdentity = std::atoi(Argv[++I]); }
		else if (std::strcmp(Argv[I], "--diag") == 0 && I + 2 < Argc)
		{
			DiagKind = std::atoi(Argv[I + 1]);
			DiagSkill = static_cast<float>(std::atof(Argv[I + 2]));
			bDiagLethal = I + 3 < Argc && std::strcmp(Argv[I + 3], "lethal") == 0;
			I += bDiagLethal ? 3 : 2;
		}
	}
	if (bTests) { return RunTests(); }
	if (GKeeperDamage <= 0.f || GKeeperDamage > 2.f) { std::printf("--keeper-damage must be in (0, 2]\n"); return 3; }
	if (bArc)
	{
		// The arc always plays the RL keeper (the policy Thesis.bat passes, or the shipped one under the working directory).
		const char* Path = PolicyPath != nullptr ? PolicyPath : "Content/HellwalkerRL/RL/hellwalker_rl.hwrl";
		std::unique_ptr<FRLPolicy> Policy = std::make_unique<FRLPolicy>();
		if (!Policy->LoadFromFile(Path)) { std::printf("cannot load %s: %s\n", Path, Policy->GetError().c_str()); return 3; }
		if (!Policy->IsBossPlayable()) { std::printf("%s is not a playable boss policy\n", Path); return 3; }
		std::printf("policy: %s\n", Path);
		Arc.Policy = Policy.get();
		Arc.Damage = bDamageGiven ? GKeeperDamage : Arc.Damage;
		Arc.Identity = KeeperIdentity >= 0 && KeeperIdentity < RL::NumKeepers ? KeeperIdentity : 0;
		Arc.Sessions = bSessionsGiven && Sessions > 0 ? Sessions : Arc.Sessions;
		return RunArc(Arc);
	}

	FScriptArm Script(GScript);
	std::unique_ptr<FSimArm> Adaptive = MakeAdaptiveArm(BrainName, PolicyPath, GScript, !bAuthored, KeeperSkill, KeeperIdentity);
	if (!Adaptive) { return 3; }
	if (DiagKind >= 0) { return RunDiag(Script, *Adaptive, static_cast<EBotKind>(DiagKind), DiagSkill, bDiagLethal); }

	std::printf("adaptive arm: %s\n", Adaptive->Label());
	if (GKeeperDamage != 1.f) { std::printf("keeper damage x%.2f (both arms)\n", GKeeperDamage); }

	// ---- 1. Reference curve: the scripted boss's swings/min (the classic floor controller's input) ----
	const EBotKind Kinds[] = { EBotKind::Habitual, EBotKind::RhythmParrier, EBotKind::DodgerLeft, EBotKind::Varied, EBotKind::Turtle };
	const float Skills[] = { 0.3f, 0.5f, 0.7f, 0.9f };
	float RefSum = 0.f;
	int32_t RefN = 0;
	FCell Scripted[5][4];
	for (int32_t K = 0; K < 5; ++K)
	{
		for (int32_t S = 0; S < 4; ++S)
		{
			Scripted[K][S] = RunCell(Script, Kinds[K], Skills[S], Sessions);
			RefSum += Mean3(Scripted[K][S].SwingsPerMin);
			++RefN;
		}
	}
	const float RefSpm = RefSum / static_cast<float>(RefN);
	Adaptive->SetReferenceSwingsPerMin(RefSpm);
	std::printf("B0 thesis simulator — %d sessions x %d encounters x %d s per cell\n", Sessions, EncountersPerSession, EncounterSeconds);
	std::printf("reference (Pathbreaker) swings/min = %.2f -> floor controller target\n\n", RefSpm);

	// ---- 2. Sweep: damage taken per minute, per encounter, both arms --------------------------------
	std::printf("%-13s %5s | %-26s | %-26s | %6s %6s | %5s %5s | %5s %5s | %s\n", "profile", "skill",
		"Pathbreaker dmg/min e1 e2 e3", "Hellwalker dmg/min e1 e2 e3", "spm P", "spm H", "subs", "ctrs", "xch P", "xch H", "verdict");
	bool bThesis = true;
	bool bFloor = true;
	bool bExchange = true;
	const float HpP = Tuning().PlayerHealthMax;
	const float HpB = Tuning().BossHealthMax;
	for (int32_t K = 0; K < 5; ++K)
	{
		for (int32_t S = 0; S < 4; ++S)
		{
			const FCell& P = Scripted[K][S];
			const FCell H = RunCell(*Adaptive, Kinds[K], Skills[S], Sessions);
			const float MP = Mean3(P.DmgPerMin);
			const float MH = Mean3(H.DmgPerMin);
			const bool bSkilled = Skills[S] >= 0.5f;
			const bool bHarder = MH >= MP;
			const bool bSwingsOk = Mean3(H.SwingsPerMin) >= Mean3(P.SwingsPerMin);
			// Net exchange: the player's health-loss rate over the boss's. "Harder" must survive the boss taking
			// MORE damage too — dealing more while opening more gaps is §1.2's failure in disguise.
			const float XP = (MP / HpP) / ((Mean3(P.BossDmgPerMin) + 1e-3f) / HpB);
			const float XH = (MH / HpP) / ((Mean3(H.BossDmgPerMin) + 1e-3f) / HpB);
			// Sampling tolerance: single cells flip by up to ~8% between 6 and 16 sessions. The plan's criteria stay strict.
			const bool bXOk = XH >= XP * 0.95f;
			if (bSkilled && !bHarder) { bThesis = false; }
			if (bSkilled && !bSwingsOk) { bFloor = false; }
			if (bSkilled && !bXOk) { bExchange = false; }
			std::printf("%-13s %5.1f | %6.1f %6.1f %6.1f        | %6.1f %6.1f %6.1f        | %6.1f %6.1f | %4.0f%% %5.1f | %5.2f %5.2f | %s%s%s%s\n",
				BotKindName(Kinds[K]), Skills[S], P.DmgPerMin[0], P.DmgPerMin[1], P.DmgPerMin[2],
				H.DmgPerMin[0], H.DmgPerMin[1], H.DmgPerMin[2], Mean3(P.SwingsPerMin), Mean3(H.SwingsPerMin),
				Mean3(H.SubRate) * 100.f, Mean3(H.Counters), XP, XH,
				bHarder ? "harder" : "EASIER", bXOk ? "" : " / WORSE-TRADE", bSwingsOk ? "" : " / FLOOR",
				(bSkilled ? "" : " (unskilled: informational)"));
		}
	}

	// ---- 3. Lethality: the masher dies to both ------------------------------------------------------
	std::printf("\nlethal runs (180 s cap):\n");
	bool bMasherLethal = true;
	const EBotKind LethalKinds[] = { EBotKind::Masher, EBotKind::Habitual, EBotKind::Varied, EBotKind::RhythmParrier, EBotKind::DodgerLeft, EBotKind::Turtle };
	const float LethalSkill[] = { 0.1f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f };
	FSimArm* Arms[2] = { &Script, Adaptive.get() };
	for (int32_t L = 0; L < 6; ++L)
	{
		for (int32_t M = 0; M < 2; ++M)
		{
			int32_t Deaths = 0;
			int32_t BossKills = 0;
			float DeathTime = 0.f;
			for (int32_t S = 0; S < Sessions; ++S)
			{
				Arms[M]->NewSession();
				FRunConfig Cfg;
				Cfg.Bot = MakeBotProfile(LethalKinds[L], LethalSkill[L]);
				Cfg.Seed = 555 + S * 13;
				Cfg.MaxFrames = 180 * FramesPerSecond;
				Cfg.KeeperDamageScale = GKeeperDamage;
				const FEncounterStats St = Arms[M]->Run(Cfg);
				if (St.bPlayerDied) { ++Deaths; DeathTime += St.Frames / 60.f; }
				if (St.bBossDied) { ++BossKills; }
			}
			std::printf("  %-10s skill %.1f vs %-11s: player died %d/%d (mean %.1f s), boss died %d/%d\n",
				BotKindName(LethalKinds[L]), LethalSkill[L], M == 0 ? "Pathbreaker" : "Hellwalker", Deaths, Sessions,
				Deaths > 0 ? DeathTime / static_cast<float>(Deaths) : 0.f, BossKills, Sessions);
			if (L == 0 && Deaths < Sessions) { bMasherLethal = false; }
		}
	}

	if (CsvPath != nullptr) { DumpCsv(CsvPath, *Adaptive); }

	std::printf("\nVERDICT\n");
	std::printf("  skilled players: Hellwalker at least as dangerous as Pathbreaker ... %s\n", bThesis ? "PASS" : "FAIL");
	std::printf("  aggression floor: Hellwalker swings/min >= Pathbreaker's ........... %s\n", bFloor ? "PASS" : "FAIL");
	std::printf("  masher: lethal in both modes ....................................... %s\n", bMasherLethal ? "PASS" : "FAIL");
	std::printf("  net exchange (stricter than the plan; 5%% sampling tolerance) ...... %s\n", bExchange ? "PASS" : "FAIL");
	const bool bAll = bThesis && bFloor && bMasherLethal && bExchange;
	std::printf("  B0 %s\n", bAll ? "PASSES — the design is sound enough to build." : "FAILS — the design is wrong; no engine work repairs it.");
	return bAll ? 0 : 2;
}
