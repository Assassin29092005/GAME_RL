// HellwalkerRL — engine-free core. Done-tests of the game core: combat (A1, A3) and the control arm (B1).
// The RL keeper's tests live in HWRLTests.cpp; the classic reference brain's in Sim/Classic/HWClassicTests.cpp.

#include "HWCore/HWCoreTests.h"

#include "HWCore/HWScriptBrain.h"
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
		// B1 — control arm (Pathbreaker: the script, verbatim)
		// ------------------------------------------------------------------------------------------
		bool TestB1Determinism(std::string& Log)
		{
			bool bOk = true;
			EMoveId First = EMoveId::None;
			for (int32_t N = 0; N < 25; ++N)
			{
				FDuel Duel;
				Duel.Reset();
				FScriptBrain Brain;
				Brain.BeginEncounter(1234);
				FDuelGeometry Geo;
				Geo.BossX = 150.f;
				FBrainDecision D;
				// Slot 0 is an approach (Reposition); the first decision is its commitment.
				bOk = bOk && Brain.Think(Duel, Geo, &D);
				if (N == 0) { First = D.Chosen; }
				bOk = bOk && (D.Chosen == First);
			}
			Logf(Log, "Pathbreaker: 25 decisions, fixed state + seed -> %s every time: %s", Move(First).Name, bOk ? "yes" : "NO");
			return bOk;
		}

		bool TestB1ControlIgnoresOutcome(std::string& Log)
		{
			// Very different players produce very different outcome streams; the script must not care.
			const EBotKind Kinds[] = { EBotKind::Masher, EBotKind::Turtle, EBotKind::RhythmParrier, EBotKind::DodgerLeft };
			int32_t Decisions = 0;
			int32_t Differ = 0;
			for (EBotKind K : Kinds)
			{
				FScriptBrain Brain;
				FRunConfig Cfg;
				Cfg.Bot = MakeBotProfile(K, 0.7f);
				Cfg.Seed = 7;
				Cfg.bImmortal = true;
				Cfg.MaxFrames = 60 * FramesPerSecond;
				std::vector<FExchangeRow> Rows;
				RunEncounter(Brain, Cfg, &Rows);
				for (size_t I = 0; I < Rows.size(); ++I)
				{
					++Decisions;
					if (Rows[I].Decision.Chosen != Brain.ScriptAt(static_cast<int32_t>(I)).Move) { ++Differ; }
				}
				Logf(Log, "vs %-13s: %d decisions", BotKindName(K), static_cast<int32_t>(Rows.size()));
			}
			Logf(Log, "%d decisions, %d differ from the script's order (must be 0)", Decisions, Differ);
			return Differ == 0 && Decisions > 50;
		}

		bool TestB1SameOpener(std::string& Log)
		{
			EMoveId First = EMoveId::None;
			int32_t FirstFrame = -1;
			bool bOk = true;
			for (int32_t I = 0; I < 5; ++I)
			{
				FScriptBrain Brain;
				FRunConfig Cfg;
				Cfg.Bot = MakeBotProfile(EBotKind::Varied, 0.5f);
				Cfg.Seed = 1000 + I * 31;
				Cfg.MaxFrames = 600;
				Cfg.StartDistance = 650.f;
				std::vector<FExchangeRow> Rows;
				RunEncounter(Brain, Cfg, &Rows);
				if (Rows.empty()) { bOk = false; continue; }
				if (I == 0) { First = Rows[0].Decision.Chosen; FirstFrame = Rows[0].Decision.Frame; }
				bOk = bOk && (Rows[0].Decision.Chosen == First && Rows[0].Decision.Frame == FirstFrame);
				Logf(Log, "run %d: opener %s at frame %d", I, Move(Rows[0].Decision.Chosen).Name, Rows[0].Decision.Frame);
			}
			return bOk;
		}

		// ------------------------------------------------------------------------------------------
		// Whole-encounter determinism (B1's premise, end to end)
		// ------------------------------------------------------------------------------------------
		bool TestSimDeterminism(std::string& Log)
		{
			FEncounterStats S[2];
			for (int32_t I = 0; I < 2; ++I)
			{
				FScriptBrain Brain;
				FRunConfig Cfg;
				Cfg.Bot = MakeBotProfile(EBotKind::Varied, 0.6f);
				Cfg.Seed = 4242;
				S[I] = RunEncounter(Brain, Cfg);
			}
			const bool bSame = S[0].Frames == S[1].Frames && S[0].PlayerDamageTaken == S[1].PlayerDamageTaken
				&& S[0].BossDamageTaken == S[1].BossDamageTaken && S[0].BossSwings == S[1].BossSwings
				&& S[0].Decisions == S[1].Decisions;
			Logf(Log, "two runs, same seed: frames %d/%d, dmg taken %.1f/%.1f, swings %d/%d -> %s",
				S[0].Frames, S[1].Frames, S[0].PlayerDamageTaken, S[1].PlayerDamageTaken, S[0].BossSwings, S[1].BossSwings,
				bSame ? "identical" : "DIFFERENT");
			return bSame;
		}

		// B0 smoke (control half): the script is lethal to a masher.
		bool TestB0MasherLethal(std::string& Log)
		{
			FScriptBrain Brain;
			FRunConfig Cfg;
			Cfg.Bot = MakeBotProfile(EBotKind::Masher, 0.1f);
			Cfg.Seed = 900;
			Cfg.MaxFrames = 180 * FramesPerSecond;
			const FEncounterStats S = RunEncounter(Brain, Cfg);
			Logf(Log, "masher vs Pathbreaker: player %s at %.1f s (boss took %.0f)", S.bPlayerDied ? "died" : "survived", S.Frames / 60.f, S.BossDamageTaken);
			return S.bPlayerDied;
		}

		const FCoreTest Tests[] = {
			{ "A1.FourOutcomes",              "A1", &TestA1FourOutcomes },
			{ "A1.InterruptedSwingWhiffsOnce","A1", &TestA1InterruptedSwingWhiffsOnce },
			{ "A3.ParryWindow",               "A3", &TestA3ParryWindow },
			{ "A3.GuardBreak",                "A3", &TestA3GuardBreak },
			{ "B1.Determinism",               "B1", &TestB1Determinism },
			{ "B1.ControlIgnoresOutcome",     "B1", &TestB1ControlIgnoresOutcome },
			{ "B1.SameOpener",                "B1", &TestB1SameOpener },
			{ "Sim.Determinism",              "B1", &TestSimDeterminism },
			{ "B0.MasherLethal",              "B0", &TestB0MasherLethal },
		};
	}

	int32_t GetCoreTests(const FCoreTest*& OutTests)
	{
		OutTests = Tests;
		return static_cast<int32_t>(sizeof(Tests) / sizeof(Tests[0]));
	}
}
