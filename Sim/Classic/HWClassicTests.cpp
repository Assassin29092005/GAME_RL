// HellwalkerRL — CLASSIC (reference) brain, tools only. Its done-tests from the reference project (B1, B2, B3, C3,
// B0 smoke), kept so the benchmark the RL keeper is measured against stays the brain the reference project shipped.

#include "Classic/HWClassicTests.h"

#include <cstdarg>
#include <cstdio>
#include <memory>

namespace HW
{
	FEncounterStats RunClassic(FPlaystyleModel& Model, EBrainMode Mode, const FRunConfig& Cfg, std::vector<FExchangeRow>* OutRows,
		const FBrainConfig* BrainCfg, int32_t Script)
	{
		FClassicBrain Brain;
		if (BrainCfg != nullptr) { Brain.Config() = *BrainCfg; }
		Brain.BindModel(&Model);
		Brain.SetMode(Mode);
		if (Script != 0)
		{
			const FScriptSlot* Slots = nullptr;
			const int32_t N = BossScript(Script, Slots);
			Brain.SetScript(Slots, N);
			ApplyScriptTuning(Script, Brain.Config());
		}
		return RunEncounter(Brain, Cfg, OutRows);
	}

	namespace
	{
		void Logf(std::string& Log, const char* Fmt, ...)
		{
			char Buf[512];
			va_list Args;
			va_start(Args, Fmt);
			std::vsnprintf(Buf, sizeof(Buf), Fmt, Args);
			va_end(Args);
			Log += Buf;
			Log += '\n';
		}

		/** Heap-allocated model (it is ~34 KB). */
		std::unique_ptr<FPlaystyleModel> NewModel() { return std::make_unique<FPlaystyleModel>(); }

		// ------------------------------------------------------------------------------------------
		// B1 — control arm
		// ------------------------------------------------------------------------------------------
		bool TestB1Determinism(std::string& Log)
		{
			auto Model = NewModel();
			for (int32_t I = 0; I < 12; ++I) { Model->Observe(ESym::BFast, -1); Model->Observe(ESym::StepL, 6); }
			Model->Flush();
			FDuel Duel;
			Duel.Reset();
			bool bOk = true;
			for (int32_t M = 0; M < 2; ++M)
			{
				FClassicBrain Brain;
				Brain.BindModel(Model.get());
				Brain.SetMode(M == 0 ? EBrainMode::Pathbreaker : EBrainMode::Hellwalker);
				EMoveId First = EMoveId::None;
				for (int32_t N = 0; N < 25; ++N)
				{
					Brain.Reset(1234);
					const FBrainDecision D = Brain.Decide(Duel, Brain.ScriptAt(1), 1, EHitOutcome::None, false, 150.f);
					if (N == 0) { First = D.Chosen; }
					bOk = bOk && (D.Chosen == First);
				}
				Logf(Log, "%s: 25 decisions, fixed state + seed -> %s every time: %s", BrainModeName(Brain.GetMode()), Move(First).Name, bOk ? "yes" : "NO");
			}
			return bOk;
		}

		bool TestB1ControlIgnoresOutcome(std::string& Log)
		{
			auto Model = NewModel();
			FDuel Duel;
			Duel.Reset();
			FClassicBrain Brain;
			Brain.BindModel(Model.get());
			Brain.SetMode(EBrainMode::Pathbreaker);
			Brain.Reset(7);
			const int32_t Index = 2; // FastSlash chained after FastSlash
			const EHitOutcome Outs[] = { EHitOutcome::Hit, EHitOutcome::Whiff, EHitOutcome::Blocked, EHitOutcome::Parried };
			const EMoveId Scripted = Brain.ScriptAt(Index).Move;
			int32_t Differ = 0;
			for (EHitOutcome O : Outs)
			{
				const FBrainDecision D = Brain.Decide(Duel, Brain.ScriptAt(Index), Index, O, false, 150.f);
				if (D.Chosen != Scripted) { ++Differ; }
				Logf(Log, "Pathbreaker, prev %-8s -> %s", OutcomeName(O), Move(D.Chosen).Name);
			}
			Logf(Log, "differs from script in %d of 4 (must be 0)", Differ);
			return Differ == 0;
		}

		bool TestB1SameOpener(std::string& Log)
		{
			EMoveId First = EMoveId::None;
			int32_t FirstFrame = -1;
			bool bOk = true;
			for (int32_t I = 0; I < 5; ++I)
			{
				auto Model = NewModel();
				FRunConfig Cfg;
				const EBrainMode Mode = EBrainMode::Pathbreaker;
				Cfg.Bot = MakeBotProfile(EBotKind::Varied, 0.5f);
				Cfg.Seed = 1000 + I * 31;
				Cfg.MaxFrames = 600;
				Cfg.StartDistance = 650.f;
				std::vector<FExchangeRow> Rows;
				RunClassic(*Model, Mode, Cfg, &Rows);
				if (Rows.empty()) { bOk = false; continue; }
				if (I == 0) { First = Rows[0].Decision.Chosen; FirstFrame = Rows[0].Decision.Frame; }
				bOk = bOk && (Rows[0].Decision.Chosen == First && Rows[0].Decision.Frame == FirstFrame);
				Logf(Log, "run %d: opener %s at frame %d", I, Move(Rows[0].Decision.Chosen).Name, Rows[0].Decision.Frame);
			}
			return bOk;
		}

		// ------------------------------------------------------------------------------------------
		// B2 — the playstyle model, with a negative control
		// ------------------------------------------------------------------------------------------
		bool TestB2ReadRisesThenFalls(std::string& Log)
		{
			auto Model = NewModel();
			FDuel Duel;
			Duel.Reset();
			FClassicBrain Brain;
			Brain.BindModel(Model.get());
			Brain.SetMode(EBrainMode::Hellwalker);
			Brain.Reset(99);
			const int32_t Index = 1; // ATTACK FastSlash, string start
			const FScriptSlot Slot = Brain.ScriptAt(Index);

			// "In the same context": every response follows (Neutral, B_Fast), and the query ends on Neutral so
			// the brain predicts in exactly that context.
			const float Read0 = Model->PredictAfter(ESym::BFast).ReadBits;
			for (int32_t I = 0; I < 10; ++I) { Model->Observe(ESym::Neutral, -1); Model->Observe(ESym::BFast, -1); Model->Observe(ESym::StepL, 6); }
			Model->Observe(ESym::Neutral, -1);
			Model->Flush();
			const FPrediction P1 = Model->PredictAfter(ESym::BFast);
			const FBrainDecision D1 = Brain.Decide(Duel, Slot, Index, EHitOutcome::None, false, 150.f);
			Logf(Log, "after 10x StepL: read %.3f -> %.3f bits, top %s p=%.2f; decision %s (%s)", Read0, P1.ReadBits,
				SymName(P1.Top), P1.TopP, Move(D1.Chosen).Name, D1.Reason);
			bool bOk = P1.ReadBits > Read0 && P1.Top == ESym::StepL;

			// Negative control: a balanced distribution for 30 player symbols.
			const ESym Balanced[] = { ESym::Parry, ESym::StepR, ESym::Block, ESym::StepB, ESym::Light, ESym::StepL,
				ESym::Heavy, ESym::Retreat, ESym::Neutral, ESym::Advance, ESym::StepF, ESym::Switch };
			for (int32_t I = 0; I < 30; ++I) { Model->Observe(ESym::BFast, -1); Model->Observe(Balanced[I % 12], 8); Model->Observe(ESym::Neutral, -1); }
			Model->Flush();
			const FPrediction P2 = Model->PredictAfter(ESym::BFast);
			Brain.Reset(99);
			const FBrainDecision D2 = Brain.Decide(Duel, Slot, Index, EHitOutcome::None, false, 150.f);
			Logf(Log, "after 30 balanced: read %.3f bits, top %s p=%.2f, margin %.2f -> %s (%s)", P2.ReadBits,
				SymName(P2.Top), P2.TopP, D2.Margin, Move(D2.Chosen).Name, DecisionKindName(D2.Kind));
			bOk = bOk && (P2.ReadBits < P1.ReadBits);
			bOk &= D2.Chosen == Slot.Move; // back to the script on its own
			return bOk;
		}

		bool TestB2ThinContextFloor(std::string& Log)
		{
			auto Model = NewModel();
			Model->Observe(ESym::BFast, -1);
			Model->Observe(ESym::StepL, 6);
			Model->Flush();
			const FPrediction P = Model->PredictAfter(ESym::BFast);
			FDuel Duel;
			Duel.Reset();
			FClassicBrain Brain;
			Brain.BindModel(Model.get());
			Brain.SetMode(EBrainMode::Hellwalker);
			Brain.Reset(5);
			const FBrainDecision D = Brain.Decide(Duel, Brain.ScriptAt(1), 1, EHitOutcome::None, false, 150.f);
			Logf(Log, "context visited once: deepest order blended %d, read %.3f bits, top %s p=%.2f, margin %.2f -> %s",
				P.OrderUsed, P.ReadBits, SymName(P.Top), P.TopP, D.Margin, Move(D.Chosen).Name);
			return P.OrderUsed < 1 && D.Chosen == Brain.ScriptAt(1).Move;
		}

		// ------------------------------------------------------------------------------------------
		// B3 — adapt and counterattack
		// ------------------------------------------------------------------------------------------
		bool TestB3OutcomesDiffer(std::string& Log)
		{
			auto Model = NewModel();
			FDuel Duel;
			Duel.Reset();
			const int32_t Index = 2;
			const EHitOutcome Outs[] = { EHitOutcome::Hit, EHitOutcome::Whiff, EHitOutcome::Blocked, EHitOutcome::Parried };
			int32_t Differ[2] = { 0, 0 };
			for (int32_t M = 0; M < 2; ++M)
			{
				FClassicBrain Brain;
				Brain.BindModel(Model.get());
				Brain.SetMode(M == 0 ? EBrainMode::Pathbreaker : EBrainMode::Hellwalker);
				Brain.Reset(11);
				const EMoveId Scripted = Brain.ScriptAt(Index).Move;
				for (EHitOutcome O : Outs)
				{
					const FBrainDecision D = Brain.Decide(Duel, Brain.ScriptAt(Index), Index, O, false, 150.f);
					if (D.Chosen != Scripted) { ++Differ[M]; }
					Logf(Log, "%-11s prev %-8s -> %-14s %s", BrainModeName(Brain.GetMode()), OutcomeName(O), Move(D.Chosen).Name, D.Reason);
				}
			}
			Logf(Log, "differs from script: control %d/4 (must be 0), adaptive %d/4 (must be >= 3)", Differ[0], Differ[1]);
			return Differ[0] == 0 && Differ[1] >= 3;
		}

		bool TestB3LegibilityAndAborts(std::string& Log)
		{
			auto Model = NewModel();
			bool bOk = true;
			int32_t Checked = 0;
			int32_t Aborts = 0;
			for (int32_t E = 0; E < 3; ++E)
			{
				FRunConfig Cfg;
				const EBrainMode Mode = EBrainMode::Hellwalker;
				Cfg.Bot = MakeBotProfile(EBotKind::Habitual, 0.8f);
				Cfg.Seed = 300 + E;
				Cfg.bImmortal = true;
				Cfg.MaxFrames = 90 * FramesPerSecond;
				std::vector<FExchangeRow> Rows;
				RunClassic(*Model, Mode, Cfg, &Rows);
				for (const FExchangeRow& R : Rows)
				{
					const FBrainDecision& D = R.Decision;
					const FMoveData& Ch = Move(D.Chosen);
					const FMoveData& Sc = Move(D.Scripted);
					if (Ch.IsAttack() && Sc.IsAttack() && Ch.Startup < Sc.Startup && D.Kind != EDecisionKind::PerfectPunish)
					{
						bOk = false;
						Logf(Log, "LEGIBILITY VIOLATION: %s (%d) replaced %s (%d): %s", Ch.Name, Ch.Startup, Sc.Name, Sc.Startup, D.Reason);
					}
					if (D.Kind == EDecisionKind::LuckyDrawAbort)
					{
						++Aborts;
						const bool bDefensive = Ch.Kind == EMoveKind::Guard || Ch.Kind == EMoveKind::Counter || Ch.Kind == EMoveKind::Step
							|| (Ch.FakeImpactFrame >= 0 && D.PrevOutcome == EHitOutcome::Parried);
						if (!bDefensive)
						{
							bOk = false;
							Logf(Log, "ABORT TO NEUTRAL: %s after %s", Ch.Name, OutcomeName(D.PrevOutcome));
						}
					}
					++Checked;
				}
			}
			Logf(Log, "%d decisions checked, %d aborts, every abort defensive/evasive, every attack as legible as its script: %s",
				Checked, Aborts, bOk ? "yes" : "NO");
			return bOk && Checked > 50;
		}

		// ------------------------------------------------------------------------------------------
		// C3 — chosen == argmax on every decision
		// ------------------------------------------------------------------------------------------
		bool TestC3ArgmaxEveryDecision(std::string& Log)
		{
			auto Model = NewModel();
			int32_t Violations = 0;
			int32_t Decisions = 0;
			const EBotKind Kinds[] = { EBotKind::Habitual, EBotKind::Varied, EBotKind::Turtle };
			for (int32_t I = 0; I < 3; ++I)
			{
				FRunConfig Cfg;
				const EBrainMode Mode = EBrainMode::Hellwalker;
				Cfg.Bot = MakeBotProfile(Kinds[I], 0.7f);
				Cfg.Seed = 77 + I;
				Cfg.bImmortal = true;
				const FEncounterStats S = RunClassic(*Model, Mode, Cfg);
				Violations += S.ArgmaxViolations;
				Decisions += S.Decisions;
			}
			Logf(Log, "%d decisions, %d argmax violations", Decisions, Violations);
			return Violations == 0 && Decisions > 50;
		}

		// ------------------------------------------------------------------------------------------
		// Whole-encounter determinism (B1's premise, end to end)
		// ------------------------------------------------------------------------------------------
		bool TestSimDeterminism(std::string& Log)
		{
			FEncounterStats S[2];
			for (int32_t I = 0; I < 2; ++I)
			{
				auto Model = NewModel();
				FRunConfig Cfg;
				const EBrainMode Mode = EBrainMode::Hellwalker;
				Cfg.Bot = MakeBotProfile(EBotKind::Varied, 0.6f);
				Cfg.Seed = 4242;
				S[I] = RunClassic(*Model, Mode, Cfg);
			}
			const bool bSame = S[0].Frames == S[1].Frames && S[0].PlayerDamageTaken == S[1].PlayerDamageTaken
				&& S[0].BossDamageTaken == S[1].BossDamageTaken && S[0].BossSwings == S[1].BossSwings
				&& S[0].Symbols == S[1].Symbols && S[0].Decisions == S[1].Decisions;
			Logf(Log, "two runs, same seed: frames %d/%d, dmg taken %.1f/%.1f, swings %d/%d, symbols %d/%d -> %s",
				S[0].Frames, S[1].Frames, S[0].PlayerDamageTaken, S[1].PlayerDamageTaken, S[0].BossSwings, S[1].BossSwings,
				S[0].Symbols, S[1].Symbols, bSame ? "identical" : "DIFFERENT");
			return bSame;
		}

		// ------------------------------------------------------------------------------------------
		// B0 smoke — the thesis, in miniature (the full sweep is Sim/ThesisSim)
		// ------------------------------------------------------------------------------------------
		bool TestB0ThesisSmoke(std::string& Log)
		{
			bool bOk = true;
			// Skilled, habitual: adaptive must deal at least as much damage per minute as scripted.
			float Dpm[2] = { 0.f, 0.f };
			float Spm[2] = { 0.f, 0.f };
			for (int32_t M = 0; M < 2; ++M)
			{
				auto Model = NewModel();
				for (int32_t E = 0; E < 3; ++E)
				{
					FRunConfig Cfg;
					const EBrainMode Mode = M == 0 ? EBrainMode::Pathbreaker : EBrainMode::Hellwalker;
					Cfg.Bot = MakeBotProfile(EBotKind::Habitual, 0.8f);
					Cfg.Seed = 500 + E;
					Cfg.bImmortal = true;
					const FEncounterStats S = RunClassic(*Model, Mode, Cfg);
					Dpm[M] += S.DamagePerMin() / 3.f;
					Spm[M] += S.SwingsPerMin() / 3.f;
				}
			}
			Logf(Log, "habitual 0.8: damage/min Pathbreaker %.1f vs Hellwalker %.1f; swings/min %.1f vs %.1f", Dpm[0], Dpm[1], Spm[0], Spm[1]);
			bOk = bOk && (Dpm[1] >= Dpm[0]);

			// Masher: both lethal.
			for (int32_t M = 0; M < 2; ++M)
			{
				auto Model = NewModel();
				FRunConfig Cfg;
				const EBrainMode Mode = M == 0 ? EBrainMode::Pathbreaker : EBrainMode::Hellwalker;
				Cfg.Bot = MakeBotProfile(EBotKind::Masher, 0.1f);
				Cfg.Seed = 900;
				Cfg.MaxFrames = 180 * FramesPerSecond;
				const FEncounterStats S = RunClassic(*Model, Mode, Cfg);
				Logf(Log, "masher vs %s: player %s at %.1f s (boss took %.0f)", BrainModeName(Mode),
					S.bPlayerDied ? "died" : "survived", S.Frames / 60.f, S.BossDamageTaken);
				bOk = bOk && (S.bPlayerDied);
			}
			return bOk;
		}

		const FCoreTest Tests[] = {
			{ "Classic.B1.Determinism",           "B1", &TestB1Determinism },
			{ "Classic.B1.ControlIgnoresOutcome", "B1", &TestB1ControlIgnoresOutcome },
			{ "Classic.B1.SameOpener",            "B1", &TestB1SameOpener },
			{ "Classic.B2.ReadRisesThenFalls",    "B2", &TestB2ReadRisesThenFalls },
			{ "Classic.B2.ThinContextFloor",      "B2", &TestB2ThinContextFloor },
			{ "Classic.B3.OutcomesDiffer",        "B3", &TestB3OutcomesDiffer },
			{ "Classic.B3.LegibilityAndAborts",   "B3", &TestB3LegibilityAndAborts },
			{ "Classic.C3.ArgmaxEveryDecision",   "C3", &TestC3ArgmaxEveryDecision },
			{ "Classic.Sim.Determinism",          "B1", &TestSimDeterminism },
			{ "Classic.B0.ThesisSmoke",           "B0", &TestB0ThesisSmoke },
		};
	}

	int32_t GetClassicTests(const FCoreTest*& OutTests)
	{
		OutTests = Tests;
		return static_cast<int32_t>(sizeof(Tests) / sizeof(Tests[0]));
	}
}
