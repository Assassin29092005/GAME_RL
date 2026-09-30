// Hellwalker — automation tests (run headless: see README "Tests").
//
//   Project.HellwalkerRL.Core.<name>     every engine-free done-test (A1, A3, B1, B2, B3, C3, B0 smoke) —
//                                      the same functions the B0 simulator runs, now inside Unreal
//   Project.HellwalkerRL.RngParity       HW::FRandom is bit-exact with FRandomStream (B1: one seeded stream)
//   Project.HellwalkerRL.ConfigRoundTrip UCombatAnimConfig captures and re-applies the core table losslessly
//   Project.HellwalkerRL.PayoffMatrix    the derived payoff matrix agrees with PLAN §2.4's worked examples
//
// Flags are EAutomationTestFlags_ApplicationContextMask — underscore, not '::' (PLAN B1).

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HWCombatAnimConfig.h"
#include "HWCore/HWCoreTests.h"
#include "HWCore/HWMoves.h"
#include "HWCore/HWPayoffTable.h"
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
	HW::RebuildDerivedPayoffs();
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWPayoffMatrixTest, "Project.HellwalkerRL.PayoffMatrix", HWTestFlags)

bool FHWPayoffMatrixTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	HW::ResetMoveTable();
	HW::RebuildDerivedPayoffs();
	const HW::FPayoffTable& T = HW::DerivedPayoffs();
	// PLAN §2.4's worked examples, now measured through FDuel rather than asserted.
	TestTrue(TEXT("Fast attack vs Parry is negative"), T.GetTypical(HW::EMoveId::BFastSlash, HW::ESym::Parry) < 0.f);
	TestTrue(TEXT("Feint vs Parry is positive (they press on the fake, eat the real one)"), T.GetTypical(HW::EMoveId::BFeintMid, HW::ESym::Parry) > 0.f);
	TestTrue(TEXT("Killer move vs Block is positive"), T.GetTypical(HW::EMoveId::BKillerThrust, HW::ESym::Block) > 0.f);
	TestTrue(TEXT("Tracking sweep vs the dodge it tracks beats the straight slash"),
		T.GetTypical(HW::EMoveId::BSweepLeft, HW::ESym::StepL) > T.GetTypical(HW::EMoveId::BFastSlash, HW::ESym::StepL));
	// Authoring rule: defensive actions cap at +1.
	bool bCap = true;
	for (int32 I = 1; I < HW::NumMoves; ++I)
	{
		const HW::FMoveData& M = HW::Move(static_cast<HW::EMoveId>(I));
		if (M.Owner != HW::ESide::Boss || M.IsAttack()) { continue; }
		for (int32 S = 0; S < HW::NumPlayerSymbols; ++S)
		{
			for (int32 L = HW::FPayoffTable::MinLead; L <= HW::FPayoffTable::MaxLead; ++L)
			{
				bCap = bCap && T.Get(M.Id, static_cast<HW::ESym>(S), L) <= 1.f;
			}
		}
	}
	TestTrue(TEXT("defensive actions cap at +1 (PLAN §2.4 authoring rule)"), bCap);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
