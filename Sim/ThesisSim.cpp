// Hellwalker — B0: headless thesis simulator (PLAN §3 B0). No Unreal.
//
// Compiles the SAME engine-free core the game module compiles (Source/HellwalkerRL/.../HWCore), and asks
// the one question that gates Phase B: against a skilled player, is the adaptive boss (Hellwalker) at
// least as dangerous as the scripted one (Pathbreaker) — and are both lethal to a masher?
//
//   ThesisSim                 run the B0 sweep and print the verdict (exit code 0 = thesis holds)
//   ThesisSim --tests         run every core done-test (A1, A3, B1, B2, B3, C3, B0 smoke)
//   ThesisSim --csv <file>    also dump one Hellwalker session's exchange rows (A2 format)
//   ThesisSim --sessions N    sessions per cell (default 4)

#include "HWCore/HWCoreTests.h"
#include "HWCore/HWPayoffTable.h"
#include "HWCore/HWSim.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace HW;

namespace
{
	int32_t GScript = 0; // --script N: which boss script the thesis is tested on (0 Warden, 1 Sage)

	struct FCell
	{
		float DmgPerMin[3] = {};   // per encounter index (session-persistent model)
		float SwingsPerMin[3] = {};
		float Counters[3] = {};
		float SubRate[3] = {};
		float BossDmgPerMin[3] = {};
	};

	constexpr int32_t EncountersPerSession = 3;
	constexpr int32_t EncounterSeconds = 90;

	FCell RunCell(EBrainMode Mode, EBotKind Kind, float Skill, int32_t Sessions, const FBrainConfig& BrainCfg)
	{
		FCell C;
		for (int32_t S = 0; S < Sessions; ++S)
		{
			auto Model = std::make_unique<FPlaystyleModel>();
			for (int32_t E = 0; E < EncountersPerSession; ++E)
			{
				FRunConfig Cfg;
				Cfg.Script = GScript;
				Cfg.Mode = Mode;
				Cfg.Bot = MakeBotProfile(Kind, Skill);
				Cfg.Seed = 10007 * (S + 1) + 131 * E + static_cast<int32_t>(Kind) * 17 + static_cast<int32_t>(Skill * 100.f);
				Cfg.bImmortal = true;
				Cfg.MaxFrames = EncounterSeconds * FramesPerSecond;
				Cfg.BrainConfig = &BrainCfg;
				const FEncounterStats St = RunEncounter(*Model, Cfg);
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

	int RunTests()
	{
		const FCoreTest* Tests = nullptr;
		const int32_t N = GetCoreTests(Tests);
		int32_t Failed = 0;
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
		std::printf("%d/%d core tests passed\n", N - Failed, N);
		return Failed == 0 ? 0 : 1;
	}

	int RunDiag(EBotKind Kind, float Skill, bool bLethal)
	{
		FBrainConfig BrainCfg;
		BrainCfg.RefSwingsPerMin = 64.f;
		for (int32_t M = 0; M < 2; ++M)
		{
			const EBrainMode Mode = M == 0 ? EBrainMode::Pathbreaker : EBrainMode::Hellwalker;
			auto Model = std::make_unique<FPlaystyleModel>();
			FEncounterStats Sum;
			float Secs = 0.f;
			int32_t Deaths = 0;
			int32_t Kills = 0;
			for (int32_t E = 0; E < 3; ++E)
			{
				FRunConfig Cfg;
				Cfg.Script = GScript;
				Cfg.Mode = Mode;
				Cfg.Bot = MakeBotProfile(Kind, Skill);
				Cfg.Seed = 91 + E;
				Cfg.bImmortal = !bLethal;
				Cfg.MaxFrames = (bLethal ? 240 : EncounterSeconds) * FramesPerSecond;
				Cfg.BrainConfig = &BrainCfg;
				const FEncounterStats S = RunEncounter(*Model, Cfg);
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
				for (int32_t I = 0; I < 8; ++I) { Sum.DecisionKinds[I] += S.DecisionKinds[I]; }
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
			std::printf("== %s vs %s %.1f — %.0f s total, player died %d/3, boss died %d/3\n", BrainModeName(Mode), BotKindName(Kind), Skill, Secs, Deaths, Kills);
			std::printf("   player dmg taken/min %.1f | boss dmg taken/min %.1f | boss swings/min %.1f | player swings/min %.1f | symbols/s %.2f\n",
				Sum.PlayerDamageTaken / Min, Sum.BossDamageTaken / Min, Sum.BossSwings / Min, Sum.PlayerSwings / Min, Sum.Symbols / Secs);
			std::printf("   boss swings:   hit %d  whiff %d  blocked %d  parried %d\n", Sum.BossOutcomes[1], Sum.BossOutcomes[2], Sum.BossOutcomes[3], Sum.BossOutcomes[4]);
			std::printf("   player swings: hit %d  whiff %d  blocked %d  parried %d | guard breaks %d, boss exposed %d\n",
				Sum.PlayerOutcomes[1], Sum.PlayerOutcomes[2], Sum.PlayerOutcomes[3], Sum.PlayerOutcomes[4], Sum.PlayerGuardBreaks, Sum.BossExposed);
			std::printf("   decisions %d:", Sum.Decisions);
			for (int32_t I = 0; I < 7; ++I) { std::printf(" %s %d", DecisionKindName(static_cast<EDecisionKind>(I)), Sum.DecisionKinds[I]); }
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

	/** Trace: every player hit that lands on the boss in its startup, with the decision that chose the move. */
	int RunTrace(EBotKind Kind, float Skill, EBrainMode Mode, int32_t MaxLines)
	{
		auto Model = std::make_unique<FPlaystyleModel>();
		FBrainConfig BrainCfg;
		BrainCfg.RefSwingsPerMin = 62.f;
		FEncounter Enc;
		Enc.Brain.Config() = BrainCfg;
		Enc.Begin(Model.get(), Mode, 91, true);
		FSimArena Arena;
		Arena.Reset(650.f);
		FPlayerBot Bot;
		Bot.Reset(MakeBotProfile(Kind, Skill), 91 * 7919 + 17);
		int32_t Lines = 0;
		EMoveId LastPlayerMove = EMoveId::None;
		int32_t LastPlayerCommit = -1;
		for (int32_t F = 0; F < 90 * FramesPerSecond && Lines < MaxLines; ++F)
		{
			Enc.ThinkBoss(Arena.Distance());
			float PF = 0.f, PL = 0.f;
			Bot.Act(Enc.Duel, Arena, PF, PL);
			Arena.LatchCommits(Enc.Duel);
			Arena.SnapshotPreStep(Enc.Duel);
			const FFighter BossBefore = Enc.Duel.Get(ESide::Boss);
			Enc.StepFrame(Arena, Bot.MovementSym());
			Bot.OnEvents(Enc.FrameEvents(), Enc.Duel);
			Arena.Integrate(Enc.Duel, PF, PL);
			for (const FDuelEvent& E : Enc.FrameEvents())
			{
				if (E.Type == EDuelEvent::Commit && E.Side == ESide::Player) { LastPlayerMove = E.Move; LastPlayerCommit = E.Frame; }
				if (E.Type == EDuelEvent::Outcome && E.Side == ESide::Player && E.Outcome == EHitOutcome::Hit
					&& E.DefenderPhase == static_cast<uint8_t>(EDefenderPhase::Startup))
				{
					const FBrainDecision& D = Enc.Brain.LastDecision();
					std::printf("f%5d boss %-13s T=%2d (commit f%d) hit by %-10s (player committed f%d, %d before boss) | decision f%d %s: %s\n",
						E.Frame, Move(BossBefore.Move).Name, BossBefore.T, BossBefore.CommitFrame, Move(E.Move).Name, LastPlayerCommit,
						BossBefore.CommitFrame - LastPlayerCommit, D.Frame, DecisionKindName(D.Kind), D.Reason);
					++Lines;
				}
			}
		}
		return 0;
	}

	/** Print the derived payoff matrix at typical leads, next to the hand-authored one. */
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
				std::printf(" %+5.1f/%+2.0f", T.GetTypical(Mv.Id, Sy), FBossBrain::BasePayoff(Mv.Id, Sy));
			}
			std::printf("\n");
		}
		return 0;
	}

	void DumpCsv(const char* Path, const FBrainConfig& BrainCfg)
	{
		FILE* F = nullptr;
		if (fopen_s(&F, Path, "w") != 0 || F == nullptr) { std::printf("cannot write %s\n", Path); return; }
		std::fprintf(F, "%s\n", ExchangeCsvHeader());
		auto Model = std::make_unique<FPlaystyleModel>();
		for (int32_t E = 0; E < EncountersPerSession; ++E)
		{
			FRunConfig Cfg;
			Cfg.Script = GScript;
			Cfg.Mode = EBrainMode::Hellwalker;
			Cfg.Bot = MakeBotProfile(EBotKind::Habitual, 0.8f);
			Cfg.Seed = 77 + E;
			Cfg.bImmortal = true;
			Cfg.MaxFrames = EncounterSeconds * FramesPerSecond;
			Cfg.BrainConfig = &BrainCfg;
			std::vector<FExchangeRow> Rows;
			RunEncounter(*Model, Cfg, &Rows);
			char RunId[32];
			std::snprintf(RunId, sizeof(RunId), "sim-e%d", E + 1);
			char Line[1024];
			for (const FExchangeRow& R : Rows)
			{
				FormatExchangeRow(R, RunId, Cfg.Mode, 1000.f / 60.f, Line, sizeof(Line));
				std::fprintf(F, "%s\n", Line);
			}
		}
		std::fclose(F);
		std::printf("wrote %s\n", Path);
	}
}

int main(int Argc, char** Argv)
{
	int32_t Sessions = 4;
	const char* CsvPath = nullptr;
	bool bTests = false;
	bool bAuthored = false;
	for (int I = 1; I < Argc; ++I)
	{
		if (std::strcmp(Argv[I], "--tests") == 0) { bTests = true; }
		else if (std::strcmp(Argv[I], "--csv") == 0 && I + 1 < Argc) { CsvPath = Argv[++I]; }
		else if (std::strcmp(Argv[I], "--sessions") == 0 && I + 1 < Argc) { Sessions = std::atoi(Argv[++I]); }
		else if (std::strcmp(Argv[I], "--payoffs") == 0) { return DumpPayoffs(); }
		else if (std::strcmp(Argv[I], "--authored") == 0) { bAuthored = true; }
		else if (std::strcmp(Argv[I], "--script") == 0 && I + 1 < Argc) { GScript = std::atoi(Argv[++I]); }
		else if (std::strcmp(Argv[I], "--trace") == 0 && I + 3 < Argc)
		{
			return RunTrace(static_cast<EBotKind>(std::atoi(Argv[I + 1])), static_cast<float>(std::atof(Argv[I + 2])),
				std::strcmp(Argv[I + 3], "H") == 0 ? EBrainMode::Hellwalker : EBrainMode::Pathbreaker, 40);
		}
		else if (std::strcmp(Argv[I], "--diag") == 0 && I + 2 < Argc)
		{
			const int32_t K = std::atoi(Argv[I + 1]);
			const float Sk = static_cast<float>(std::atof(Argv[I + 2]));
			const bool bLethal = I + 3 < Argc && std::strcmp(Argv[I + 3], "lethal") == 0;
			return RunDiag(static_cast<EBotKind>(K), Sk, bLethal);
		}
	}
	if (bTests) { return RunTests(); }

	FBrainConfig BrainCfg;
	BrainCfg.bDerivedPayoffs = !bAuthored;
	std::printf("payoffs: %s\n", bAuthored ? "hand-authored matrix (ablation)" : "derived from the combat rules");

	// ---- 1. Reference curve: the scripted boss's swings/min (PLAN §1.2 floor controller input) ----
	const EBotKind Kinds[] = { EBotKind::Habitual, EBotKind::RhythmParrier, EBotKind::DodgerLeft, EBotKind::Varied, EBotKind::Turtle };
	const float Skills[] = { 0.3f, 0.5f, 0.7f, 0.9f };
	float RefSum = 0.f;
	int32_t RefN = 0;
	FCell Scripted[5][4];
	for (int32_t K = 0; K < 5; ++K)
	{
		for (int32_t S = 0; S < 4; ++S)
		{
			Scripted[K][S] = RunCell(EBrainMode::Pathbreaker, Kinds[K], Skills[S], Sessions, BrainCfg);
			RefSum += Mean3(Scripted[K][S].SwingsPerMin);
			++RefN;
		}
	}
	BrainCfg.RefSwingsPerMin = RefSum / static_cast<float>(RefN);
	std::printf("B0 thesis simulator — %d sessions x %d encounters x %d s per cell\n", Sessions, EncountersPerSession, EncounterSeconds);
	std::printf("reference (Pathbreaker) swings/min = %.2f -> floor controller target\n\n", BrainCfg.RefSwingsPerMin);

	// ---- 2. Sweep: damage taken per minute, per encounter, both modes -----------------------------
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
			const FCell H = RunCell(EBrainMode::Hellwalker, Kinds[K], Skills[S], Sessions, BrainCfg);
			const float MP = Mean3(P.DmgPerMin);
			const float MH = Mean3(H.DmgPerMin);
			const bool bSkilled = Skills[S] >= 0.5f;
			const bool bHarder = MH >= MP;
			const bool bSwingsOk = Mean3(H.SwingsPerMin) >= Mean3(P.SwingsPerMin);
			// Net exchange: the player's health-loss rate over the boss's. "Harder" must survive the boss taking
			// MORE damage too — dealing more while opening more gaps is §1.2's failure in disguise.
			const float XP = (MP / HpP) / ((Mean3(P.BossDmgPerMin) + 1e-3f) / HpB);
			const float XH = (MH / HpP) / ((Mean3(H.BossDmgPerMin) + 1e-3f) / HpB);
			// Sampling tolerance: at 6 vs 16 sessions single cells flip by up to ~8% (measured: DodgerLeft 0.5
			// fails at n=6, passes by 17% at n=16; Turtle 0.5 the reverse, by 1%). The plan's own criteria stay strict.
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

	// ---- 3. Lethality: the masher dies to both --------------------------------------------------------
	std::printf("\nlethal runs (180 s cap):\n");
	bool bMasherLethal = true;
	const EBotKind LethalKinds[] = { EBotKind::Masher, EBotKind::Habitual, EBotKind::Varied, EBotKind::RhythmParrier, EBotKind::DodgerLeft, EBotKind::Turtle };
	const float LethalSkill[] = { 0.1f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f };
	for (int32_t L = 0; L < 6; ++L)
	{
		for (int32_t M = 0; M < 2; ++M)
		{
			const EBrainMode Mode = M == 0 ? EBrainMode::Pathbreaker : EBrainMode::Hellwalker;
			int32_t Deaths = 0;
			int32_t BossKills = 0;
			float DeathTime = 0.f;
			for (int32_t S = 0; S < Sessions; ++S)
			{
				auto Model = std::make_unique<FPlaystyleModel>();
				FRunConfig Cfg;
				Cfg.Script = GScript;
				Cfg.Mode = Mode;
				Cfg.Bot = MakeBotProfile(LethalKinds[L], LethalSkill[L]);
				Cfg.Seed = 555 + S * 13;
				Cfg.MaxFrames = 180 * FramesPerSecond;
				Cfg.BrainConfig = &BrainCfg;
				const FEncounterStats St = RunEncounter(*Model, Cfg);
				if (St.bPlayerDied) { ++Deaths; DeathTime += St.Frames / 60.f; }
				if (St.bBossDied) { ++BossKills; }
			}
			std::printf("  %-10s skill %.1f vs %-11s: player died %d/%d (mean %.1f s), boss died %d/%d\n",
				BotKindName(LethalKinds[L]), LethalSkill[L], BrainModeName(Mode), Deaths, Sessions,
				Deaths > 0 ? DeathTime / static_cast<float>(Deaths) : 0.f, BossKills, Sessions);
			if (L == 0 && Deaths < Sessions) { bMasherLethal = false; }
		}
	}

	if (CsvPath != nullptr) { DumpCsv(CsvPath, BrainCfg); }

	std::printf("\nVERDICT\n");
	std::printf("  skilled players: Hellwalker at least as dangerous as Pathbreaker ... %s\n", bThesis ? "PASS" : "FAIL");
	std::printf("  aggression floor: Hellwalker swings/min >= Pathbreaker's ........... %s\n", bFloor ? "PASS" : "FAIL");
	std::printf("  masher: lethal in both modes ....................................... %s\n", bMasherLethal ? "PASS" : "FAIL");
	std::printf("  net exchange (stricter than the plan; 5%% sampling tolerance) ...... %s\n", bExchange ? "PASS" : "FAIL");
	const bool bAll = bThesis && bFloor && bMasherLethal && bExchange;
	std::printf("  B0 %s\n", bAll ? "PASSES — the design is sound enough to build." : "FAILS — the design is wrong; no engine work repairs it.");
	return bAll ? 0 : 2;
}
