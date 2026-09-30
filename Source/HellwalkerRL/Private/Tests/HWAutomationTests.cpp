// HellwalkerRL — automation tests (run headless: Tools\Test.bat).
//
//   Project.HellwalkerRL.Core.<name>     the game core's done-tests (A1, A3, B1, B0) — the same functions ThesisSim runs
//   Project.HellwalkerRL.RL.<name>       the RL keeper's core tests (actions, masks, perception delay, tokens, labels,
//                                      the forward pass, brain determinism, session memory) — also run by ThesisSim
//   Project.HellwalkerRL.RLModel         the shipped model loads in the game and plays a deterministic fight
//   Project.HellwalkerRL.RngParity       HW::FRandom is bit-exact with FRandomStream (B1: one seeded stream)
//   Project.HellwalkerRL.ConfigRoundTrip UCombatAnimConfig captures and re-applies the core table losslessly
//
// Flags are EAutomationTestFlags_ApplicationContextMask — underscore, not '::' (PLAN B1).

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HWCombatAnimConfig.h"
#include "HWCore/HWCoreTests.h"
#include "HWCore/HWMoves.h"
#include "HWCore/HWRLBrain.h"
#include "HWCore/HWSim.h"
#include "HWSessionSubsystem.h"
#include "Misc/FileHelper.h"
#include "HWCore/HWRandom.h"
#include "Math/RandomStream.h"

namespace
{
	constexpr EAutomationTestFlags HWTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FHWCoreDoneTests, "Project.HellwalkerRL.Core", HWTestFlags)

void FHWCoreDoneTests::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	const HW::FCoreTest* Tests = nullptr;
	const int32 N = HW::GetCoreTests(Tests);
	for (int32 I = 0; I < N; ++I)
	{
		OutBeautifiedNames.Add(UTF8_TO_TCHAR(Tests[I].Name));
		OutTestCommands.Add(UTF8_TO_TCHAR(Tests[I].Name));
	}
}

bool FHWCoreDoneTests::RunTest(const FString& Parameters)
{
	const HW::FCoreTest* Tests = nullptr;
	const int32 N = HW::GetCoreTests(Tests);
	for (int32 I = 0; I < N; ++I)
	{
		if (Parameters != UTF8_TO_TCHAR(Tests[I].Name)) { continue; }
		std::string Log;
		HW::ResetMoveTable();
		const bool bOk = Tests[I].Fn(Log);
		TArray<FString> Lines;
		FString(UTF8_TO_TCHAR(Log.c_str())).ParseIntoArrayLines(Lines);
		for (const FString& L : Lines) { AddInfo(L); }
		if (!bOk) { AddError(FString::Printf(TEXT("%s (%s) failed — see the log lines above."), *Parameters, UTF8_TO_TCHAR(Tests[I].Milestone))); }
		return bOk;
	}
	AddError(FString::Printf(TEXT("No core test named %s"), *Parameters));
	return false;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FHWRLCoreTests, "Project.HellwalkerRL.RL", HWTestFlags)

void FHWRLCoreTests::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	const HW::FCoreTest* Tests = nullptr;
	const int32 N = HW::GetRLTests(Tests);
	for (int32 I = 0; I < N; ++I)
	{
		OutBeautifiedNames.Add(UTF8_TO_TCHAR(Tests[I].Name));
		OutTestCommands.Add(UTF8_TO_TCHAR(Tests[I].Name));
	}
}

bool FHWRLCoreTests::RunTest(const FString& Parameters)
{
	const HW::FCoreTest* Tests = nullptr;
	const int32 N = HW::GetRLTests(Tests);
	for (int32 I = 0; I < N; ++I)
	{
		if (Parameters != UTF8_TO_TCHAR(Tests[I].Name)) { continue; }
		std::string Log;
		HW::ResetMoveTable();
		const bool bOk = Tests[I].Fn(Log);
		TArray<FString> Lines;
		FString(UTF8_TO_TCHAR(Log.c_str())).ParseIntoArrayLines(Lines);
		for (const FString& L : Lines) { AddInfo(L); }
		if (!bOk) { AddError(FString::Printf(TEXT("%s (%s) failed — see the log lines above."), *Parameters, UTF8_TO_TCHAR(Tests[I].Milestone))); }
		return bOk;
	}
	AddError(FString::Printf(TEXT("No RL test named %s"), *Parameters));
	return false;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWRLModelTest, "Project.HellwalkerRL.RLModel", HWTestFlags)

bool FHWRLModelTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	HW::ResetMoveTable();
	// The file the game ships (Content/HellwalkerRL/RL/hellwalker_rl.hwrl, or -HWPolicy=).
	const FString Path = UHWSessionSubsystem::PolicyPath();
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path, FILEREAD_Silent))
	{
		AddError(FString::Printf(TEXT("no RL model at %s — train and export one (RL/README.md), or the Hellwalker tier falls back to the script"), *Path));
		return false;
	}
	HW::FRLPolicy Policy;
	if (!Policy.LoadFromMemory(Bytes.GetData(), static_cast<size_t>(Bytes.Num())))
	{
		AddError(FString::Printf(TEXT("%s does not load: %s"), *Path, UTF8_TO_TCHAR(Policy.GetError().c_str())));
		return false;
	}
	AddInfo(FString::Printf(TEXT("%s: %s, hidden %d, %d actions"), *Path, Policy.IsRecurrent() ? TEXT("recurrent") : TEXT("feed-forward"),
		Policy.HiddenSize(), Policy.NumActions()));

	// Two identical sessions against the same simulated player must produce the identical fight (B1, RL.md section 8).
	HW::FEncounterStats Stats[2];
	for (int32 Run = 0; Run < 2; ++Run)
	{
		HW::FRLSession Session;
		Session.Reset();
		HW::FRLBrain Brain;
		Brain.Bind(&Policy, &Session);
		HW::FRunConfig Cfg;
		Cfg.Bot = HW::MakeBotProfile(HW::EBotKind::Habitual, 0.8f);
		Cfg.Seed = 4242;
		Cfg.MaxFrames = 60 * HW::FramesPerSecond;
		Stats[Run] = HW::RunEncounter(Brain, Cfg);
	}
	TestTrue(TEXT("the RL keeper made decisions"), Stats[0].Decisions > 20);
	TestTrue(TEXT("the RL keeper attacked"), Stats[0].BossSwings > 0);
	TestTrue(TEXT("same seed, same fight"), Stats[0].Frames == Stats[1].Frames && Stats[0].Decisions == Stats[1].Decisions
		&& Stats[0].PlayerDamageTaken == Stats[1].PlayerDamageTaken && Stats[0].BossDamageTaken == Stats[1].BossDamageTaken);
	AddInfo(FString::Printf(TEXT("60 s vs Habitual 0.8: %d decisions, %d swings, dealt %.0f, took %.0f"), Stats[0].Decisions,
		Stats[0].BossSwings, Stats[0].PlayerDamageTaken, Stats[0].BossDamageTaken));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWRngParityTest, "Project.HellwalkerRL.RngParity", HWTestFlags)

bool FHWRngParityTest::RunTest(const FString& Parameters)
{
	for (int32 Seed : { 0, 1, 1234, -77, 2147483647 })
	{
		HW::FRandom Ours(Seed);
		FRandomStream Theirs(Seed);
		for (int32 I = 0; I < 2000; ++I)
		{
			switch (I % 3)
			{
			case 0:
				if (Ours.GetFraction() != Theirs.GetFraction()) { AddError(FString::Printf(TEXT("GetFraction diverged: seed %d draw %d"), Seed, I)); return false; }
				break;
			case 1:
				if (Ours.RandRange(-5, 37) != Theirs.RandRange(-5, 37)) { AddError(FString::Printf(TEXT("RandRange diverged: seed %d draw %d"), Seed, I)); return false; }
				break;
			default:
				if (Ours.GetUnsignedInt() != Theirs.GetUnsignedInt()) { AddError(FString::Printf(TEXT("GetUnsignedInt diverged: seed %d draw %d"), Seed, I)); return false; }
				break;
			}
		}
	}
	AddInfo(TEXT("HW::FRandom matches FRandomStream bit for bit over 10,000 draws."));
	(void)Parameters;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWConfigRoundTripTest, "Project.HellwalkerRL.ConfigRoundTrip", HWTestFlags)

bool FHWConfigRoundTripTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	HW::ResetMoveTable();
	UHWCombatAnimConfig* Config = NewObject<UHWCombatAnimConfig>();
	Config->CaptureFromCore();
	const int32 Applied = Config->ApplyToCore();
	TestEqual(TEXT("every move applied"), Applied, HW::NumMoves - 1);
	bool bOk = true;
	for (const FHWMoveFrameData& D : Config->Moves)
	{
		for (int32 I = 1; I < HW::NumMoves; ++I)
		{
			const HW::FMoveData& M = HW::Move(static_cast<HW::EMoveId>(I));
			if (D.Move != FName(UTF8_TO_TCHAR(M.Name))) { continue; }
			bOk = bOk && D.Startup == M.Startup && D.Active == M.Active && D.Recovery == M.Recovery && D.Damage == M.Damage;
		}
	}
	TestTrue(TEXT("frame data survives capture -> apply"), bOk);
	HW::ResetMoveTable();
	return bOk;
}

#endif // WITH_DEV_AUTOMATION_TESTS
