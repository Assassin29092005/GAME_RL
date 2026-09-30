// Hellwalker — engine-free core. Done-tests from PLAN §3 (A1, A3, B1, B2, B3, C3) + B0 smoke.

#include "HWCore/HWCoreTests.h"

#include "HWCore/HWSim.h"

#include <cstdarg>
#include <cstdio>
#include <memory>

namespace HW
{
	namespace
	{
		class FFixedOracle : public IContactOracle
		{
		public:
			bool bPlayerHits = true;
			bool bBossHits = true;
			bool Contacts(const FDuel& Duel, ESide Attacker) override
			{
				(void)Duel;
				return Attacker == ESide::Player ? bPlayerHits : bBossHits;
			}
		};

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

		/** Step until the boss raises an outcome; returns it (None on timeout). Collects all events. */
		EHitOutcome StepUntilBossOutcome(FDuel& Duel, IContactOracle& Oracle, int32_t MaxFrames, std::vector<FDuelEvent>* OutAll = nullptr)
		{
			std::vector<FDuelEvent> Ev;
			for (int32_t I = 0; I < MaxFrames; ++I)
			{
				Duel.Step(Oracle);
				Duel.TakeEvents(Ev);
				EHitOutcome Found = EHitOutcome::None;
				for (const FDuelEvent& E : Ev)
				{
					if (OutAll != nullptr) { OutAll->push_back(E); }
					if (E.Type == EDuelEvent::Outcome && E.Side == ESide::Boss && Found == EHitOutcome::None) { Found = E.Outcome; }
				}
				if (Found != EHitOutcome::None) { return Found; }
			}
			return EHitOutcome::None;
		}

		void StepFrames(FDuel& Duel, IContactOracle& Oracle, int32_t N)
		{
			std::vector<FDuelEvent> Ev;
			for (int32_t I = 0; I < N; ++I) { Duel.Step(Oracle); Duel.TakeEvents(Ev); }
		}

		/** Heap-allocated model (it is ~34 KB) with an optional training stream. */
		std::unique_ptr<FPlaystyleModel> NewModel() { return std::make_unique<FPlaystyleModel>(); }

		// ------------------------------------------------------------------------------------------
		// A1 — all four outcomes provoked deliberately and logged correctly
		// ------------------------------------------------------------------------------------------
		bool TestA1FourOutcomes(std::string& Log)
		{
			bool bOk = true;
			FFixedOracle Hit;
			FFixedOracle Never;
			Never.bBossHits = Never.bPlayerHits = false;
			FDuel Duel;

			// Hit
			Duel.Reset();
			Duel.Commit(ESide::Boss, EMoveId::BFastSlash);
			EHitOutcome O = StepUntilBossOutcome(Duel, Hit, 60);
			Logf(Log, "undefended FastSlash -> %s", OutcomeName(O));
			bOk = bOk && (O == EHitOutcome::Hit);
			bOk = bOk && (Duel.Get(ESide::Player).State == EFighterState::Hitstun);

			// Whiff — and exactly one outcome for the swing
			Duel.Reset();
			Duel.Commit(ESide::Boss, EMoveId::BFastSlash);
			std::vector<FDuelEvent> All;
			O = StepUntilBossOutcome(Duel, Never, 60, &All);
			StepFrames(Duel, Never, 40);
			int32_t Outcomes = 0;
			for (const FDuelEvent& E : All) { if (E.Type == EDuelEvent::Outcome) { ++Outcomes; } }
			Logf(Log, "no contact -> %s (%d outcome events)", OutcomeName(O), Outcomes);
			bOk = bOk && (O == EHitOutcome::Whiff && Outcomes == 1);

			// Blocked
			Duel.Reset();
			Duel.SetGuardHeld(ESide::Player, true);
			StepFrames(Duel, Hit, 3);
			Duel.Commit(ESide::Boss, EMoveId::BFastSlash);
			O = StepUntilBossOutcome(Duel, Hit, 60);
			Logf(Log, "guard held -> %s (player sha-chi %.1f)", OutcomeName(O), Duel.Get(ESide::Player).ShaChi);
			bOk = bOk && (O == EHitOutcome::Blocked);

			// Parried — and the parry reward floor is paid
			Duel.Reset();
			Duel.Commit(ESide::Boss, EMoveId::BFastSlash);
			StepFrames(Duel, Hit, Move(EMoveId::BFastSlash).Startup - 4);
			Duel.Commit(ESide::Player, EMoveId::PParry);
			O = StepUntilBossOutcome(Duel, Hit, 60);
			const FFighter& B = Duel.Get(ESide::Boss);
			Logf(Log, "parry 4f before impact -> %s (boss %s, sha-chi %.1f)", OutcomeName(O), FighterStateName(B.State), B.ShaChi);
			bOk = bOk && (O == EHitOutcome::Parried && B.State == EFighterState::Stagger);
			bOk = bOk && (B.ShaChi <= B.ShaChiMax - Tuning().ParryRewardShaChi + 0.01f);
			return bOk;
		}

		// A1 — a swing interrupted mid-active with nothing connected raises exactly one Whiff (§5.4)
		bool TestA1InterruptedSwingWhiffsOnce(std::string& Log)
		{
			FFixedOracle Oracle;
			Oracle.bBossHits = false;   // the boss's hitbox never touches
			Oracle.bPlayerHits = true;  // the player's does
			FDuel Duel;
			Duel.Reset();
			Duel.Commit(ESide::Boss, EMoveId::BFastSlash); // no hyper-armor, so the player's hit interrupts it
			StepFrames(Duel, Oracle, Move(EMoveId::BFastSlash).Startup + 1); // now mid-active
			Duel.CommitPlayerAttack(false);
			std::vector<FDuelEvent> All;
			std::vector<FDuelEvent> Ev;
			for (int32_t I = 0; I < 40; ++I)
			{
				Duel.Step(Oracle);
				Duel.TakeEvents(Ev);
				All.insert(All.end(), Ev.begin(), Ev.end());
			}
			int32_t BossOutcomes = 0;
			EHitOutcome Seen = EHitOutcome::None;
			for (const FDuelEvent& E : All)
			{
				if (E.Type == EDuelEvent::Outcome && E.Side == ESide::Boss) { ++BossOutcomes; Seen = E.Outcome; }
			}
			Logf(Log, "boss swing interrupted mid-active -> %d outcome(s), last %s", BossOutcomes, OutcomeName(Seen));
			return BossOutcomes == 1 && Seen == EHitOutcome::Whiff;
		}

		// ------------------------------------------------------------------------------------------
		// A3 — console-injected press at an exact frame offset: 9 before impact fails, 7 succeeds
		// ------------------------------------------------------------------------------------------
		bool TestA3ParryWindow(std::string& Log)
		{
			bool bOk = true;
			FFixedOracle Oracle;
			struct FCase { int32_t Offset; EHitOutcome Expect; };
			const FCase Cases[] = {
				{ 10, EHitOutcome::Hit }, { 9, EHitOutcome::Hit }, { 8, EHitOutcome::Parried }, { 7, EHitOutcome::Parried },
				{ 4, EHitOutcome::Parried }, { 1, EHitOutcome::Parried }, { 0, EHitOutcome::Hit },
			};
			for (const FCase& C : Cases)
			{
				FDuel Duel;
				Duel.Reset();
				Duel.Commit(ESide::Boss, EMoveId::BFastSlash);
				const int32_t Impact = Move(EMoveId::BFastSlash).Startup; // duel frame of first contact
				StepFrames(Duel, Oracle, Impact - C.Offset);
				Duel.Commit(ESide::Player, EMoveId::PParry);
				const EHitOutcome O = StepUntilBossOutcome(Duel, Oracle, 60);
				const bool bPass = O == C.Expect;
				Logf(Log, "parry pressed %2d frames before impact -> %-8s %s", C.Offset, OutcomeName(O), bPass ? "ok" : "WRONG");
				bOk = bOk && (bPass);
			}
			return bOk;
		}

		// A3 — guard breaks after 3 heavy blocks from full
		bool TestA3GuardBreak(std::string& Log)
		{
			FFixedOracle Oracle;
			FDuel Duel;
			Duel.Reset();
			Duel.SetGuardHeld(ESide::Player, true);
			StepFrames(Duel, Oracle, 3);
			bool bBroke = false;
			int32_t BrokeOn = -1;
			for (int32_t N = 1; N <= 3; ++N)
			{
				int32_t Guard = 0;
				while (!Duel.CanCommit(ESide::Boss, EMoveId::BHeavyCleave) && Guard++ < 200) { StepFrames(Duel, Oracle, 1); }
				Duel.Commit(ESide::Boss, EMoveId::BHeavyCleave);
				std::vector<FDuelEvent> All;
				const EHitOutcome O = StepUntilBossOutcome(Duel, Oracle, 80, &All);
				for (const FDuelEvent& E : All)
				{
					if (E.Type == EDuelEvent::GuardBreak && E.Side == ESide::Player && !bBroke) { bBroke = true; BrokeOn = N; }
				}
				Logf(Log, "heavy block %d -> %s, player sha-chi %.1f, state %s", N, OutcomeName(O),
					Duel.Get(ESide::Player).ShaChi, FighterStateName(Duel.Get(ESide::Player).State));
			}
			Logf(Log, "guard broke on block %d", BrokeOn);
			return bBroke && BrokeOn == 3;
		}

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
				FBossBrain Brain;
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
			FBossBrain Brain;
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
				Cfg.Mode = EBrainMode::Pathbreaker;
				Cfg.Bot = MakeBotProfile(EBotKind::Varied, 0.5f);
				Cfg.Seed = 1000 + I * 31;
				Cfg.MaxFrames = 600;
				Cfg.StartDistance = 650.f;
				std::vector<FExchangeRow> Rows;
				RunEncounter(*Model, Cfg, &Rows);
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
			FBossBrain Brain;
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
			FBossBrain Brain;
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
				FBossBrain Brain;
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
				Cfg.Mode = EBrainMode::Hellwalker;
				Cfg.Bot = MakeBotProfile(EBotKind::Habitual, 0.8f);
				Cfg.Seed = 300 + E;
				Cfg.bImmortal = true;
				Cfg.MaxFrames = 90 * FramesPerSecond;
				std::vector<FExchangeRow> Rows;
				RunEncounter(*Model, Cfg, &Rows);
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
				Cfg.Mode = EBrainMode::Hellwalker;
				Cfg.Bot = MakeBotProfile(Kinds[I], 0.7f);
				Cfg.Seed = 77 + I;
				Cfg.bImmortal = true;
				const FEncounterStats S = RunEncounter(*Model, Cfg);
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
				Cfg.Mode = EBrainMode::Hellwalker;
				Cfg.Bot = MakeBotProfile(EBotKind::Varied, 0.6f);
				Cfg.Seed = 4242;
				S[I] = RunEncounter(*Model, Cfg);
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
					Cfg.Mode = M == 0 ? EBrainMode::Pathbreaker : EBrainMode::Hellwalker;
					Cfg.Bot = MakeBotProfile(EBotKind::Habitual, 0.8f);
					Cfg.Seed = 500 + E;
					Cfg.bImmortal = true;
					const FEncounterStats S = RunEncounter(*Model, Cfg);
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
				Cfg.Mode = M == 0 ? EBrainMode::Pathbreaker : EBrainMode::Hellwalker;
				Cfg.Bot = MakeBotProfile(EBotKind::Masher, 0.1f);
				Cfg.Seed = 900;
				Cfg.MaxFrames = 180 * FramesPerSecond;
				const FEncounterStats S = RunEncounter(*Model, Cfg);
				Logf(Log, "masher vs %s: player %s at %.1f s (boss took %.0f)", BrainModeName(Cfg.Mode),
					S.bPlayerDied ? "died" : "survived", S.Frames / 60.f, S.BossDamageTaken);
				bOk = bOk && (S.bPlayerDied);
			}
			return bOk;
		}

		const FCoreTest Tests[] = {
			{ "A1.FourOutcomes",              "A1", &TestA1FourOutcomes },
			{ "A1.InterruptedSwingWhiffsOnce","A1", &TestA1InterruptedSwingWhiffsOnce },
			{ "A3.ParryWindow",               "A3", &TestA3ParryWindow },
			{ "A3.GuardBreak",                "A3", &TestA3GuardBreak },
			{ "B1.Determinism",               "B1", &TestB1Determinism },
			{ "B1.ControlIgnoresOutcome",     "B1", &TestB1ControlIgnoresOutcome },
			{ "B1.SameOpener",                "B1", &TestB1SameOpener },
			{ "B2.ReadRisesThenFalls",        "B2", &TestB2ReadRisesThenFalls },
			{ "B2.ThinContextFloor",          "B2", &TestB2ThinContextFloor },
			{ "B3.OutcomesDiffer",            "B3", &TestB3OutcomesDiffer },
			{ "B3.LegibilityAndAborts",       "B3", &TestB3LegibilityAndAborts },
			{ "C3.ArgmaxEveryDecision",       "C3", &TestC3ArgmaxEveryDecision },
			{ "Sim.Determinism",              "B1", &TestSimDeterminism },
			{ "B0.ThesisSmoke",               "B0", &TestB0ThesisSmoke },
		};
	}

	int32_t GetCoreTests(const FCoreTest*& OutTests)
	{
		OutTests = Tests;
		return static_cast<int32_t>(sizeof(Tests) / sizeof(Tests[0]));
	}
}
