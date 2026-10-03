// HellwalkerRL — the keepers' difficulty and identity wiring (pure functions: no world).
//
//   Project.HellwalkerRL.Keepers.Identity    which keeper the RL network plays: command line > explicit > look > script
//   Project.HellwalkerRL.Keepers.Adaptive    the Adaptive controller keeps fights close, stays in [0, 1], samples when easy
//   Project.HellwalkerRL.Keepers.MusicTension the duel music tightens as the keeper pulls ahead, calms when it is losing

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HWDuelSubsystem.h"
#include "HWSessionSubsystem.h"
#include "HWSettings.h"
#include "HWAudio.h"
#include "HWCore/HWRLTypes.h"

namespace
{
	constexpr EAutomationTestFlags HWKeepersTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWKeepersIdentityTest, "Project.HellwalkerRL.Keepers.Identity", HWKeepersTestFlags)

bool FHWKeepersIdentityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using HW::RL::EKeeper;
	auto Id = [](FName Cast, int32 Script, int32 Explicit, int32 Cmd) { return UHWDuelSubsystem::KeeperIdentityFor(Cast, Script, Explicit, Cmd); };
	TestEqual(TEXT("Sevarog is the Warden"), Id(TEXT("Sevarog"), 0, -1, -1), static_cast<int32>(EKeeper::Warden));
	TestEqual(TEXT("Wukong is the Sage"), Id(TEXT("Wukong"), 0, -1, -1), static_cast<int32>(EKeeper::Sage));
	TestEqual(TEXT("the Golem is the Returned"), Id(TEXT("Golem"), 0, -1, -1), static_cast<int32>(EKeeper::Returned));
	TestEqual(TEXT("no look: script 1 is the Sage's"), Id(NAME_None, 1, -1, -1), static_cast<int32>(EKeeper::Sage));
	TestEqual(TEXT("no look: script 0 is the Warden's"), Id(NAME_None, 0, -1, -1), static_cast<int32>(EKeeper::Warden));
	TestEqual(TEXT("an explicit choice (the shrine) beats the look"), Id(TEXT("Sevarog"), 0, 2, -1), static_cast<int32>(EKeeper::Returned));
	TestEqual(TEXT("-HWKeeper beats everything"), Id(TEXT("Golem"), 1, 2, 1), static_cast<int32>(EKeeper::Sage));
	TestEqual(TEXT("out-of-range choices are ignored"), Id(TEXT("Wukong"), 0, 7, 9), static_cast<int32>(EKeeper::Sage));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWKeepersAdaptiveTest, "Project.HellwalkerRL.Keepers.Adaptive", HWKeepersTestFlags)

bool FHWKeepersAdaptiveTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using S = UHWSessionSubsystem;
	TestTrue(TEXT("the keeper crushed you: it eases off a lot"), FMath::IsNearlyEqual(S::NextAdaptiveSkill(0.6f, false, 0.f, 0.8f), 0.5f, 1.0e-4f));
	TestTrue(TEXT("it won narrowly: it eases off a little"), FMath::IsNearlyEqual(S::NextAdaptiveSkill(0.6f, false, 0.f, 0.2f), 0.57f, 1.0e-4f));
	TestTrue(TEXT("you won narrowly: it sharpens a little"), FMath::IsNearlyEqual(S::NextAdaptiveSkill(0.6f, true, 0.1f, 0.f), 0.64f, 1.0e-4f));
	TestTrue(TEXT("you crushed it: it sharpens a lot"), FMath::IsNearlyEqual(S::NextAdaptiveSkill(0.6f, true, 0.7f, 0.f), 0.7f, 1.0e-4f));
	TestTrue(TEXT("never below 0"), S::NextAdaptiveSkill(0.02f, false, 0.f, 1.f) == 0.f);
	TestTrue(TEXT("never above 1"), S::NextAdaptiveSkill(0.97f, true, 1.f, 0.f) == 1.f);
	// Ten losses in a row walk it down to the floor; then it samples (easiest play), and wins bring it back up.
	float Skill = S::AdaptiveStart;
	for (int32 I = 0; I < 10; ++I) { Skill = S::NextAdaptiveSkill(Skill, false, 0.f, 0.9f); }
	TestTrue(TEXT("a losing streak reaches the floor and samples"), Skill == 0.f && S::AdaptiveTemperature(Skill) > 0.f);
	for (int32 I = 0; I < 5; ++I) { Skill = S::NextAdaptiveSkill(Skill, true, 0.6f, 0.f); }
	TestTrue(TEXT("wins bring it back to greedy play"), FMath::IsNearlyEqual(Skill, 0.5f, 1.0e-4f) && S::AdaptiveTemperature(Skill) == 0.f);
	// The breather: Easy's at the floor, none from skill 0.15 up.
	TestEqual(TEXT("at the floor it breathes like Easy"), S::AdaptiveSwingGap(0.f), HW::RL::EasySwingGap);
	TestEqual(TEXT("no breather from 0.15"), S::AdaptiveSwingGap(0.15f), 0);
	TestTrue(TEXT("the breather shrinks as it sharpens"), S::AdaptiveSwingGap(0.05f) > S::AdaptiveSwingGap(0.1f) && S::AdaptiveSwingGap(0.1f) > 0);
	TestEqual(TEXT("Easy breathes"), UHWSettingsSubsystem::KeeperSwingGapFor(EHWDifficulty::Easy), HW::RL::EasySwingGap);
	TestEqual(TEXT("Hellwalker does not"), UHWSettingsSubsystem::KeeperSwingGapFor(EHWDifficulty::Hellwalker), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWKeepersMusicTensionTest, "Project.HellwalkerRL.Keepers.MusicTension", HWKeepersTestFlags)

bool FHWKeepersMusicTensionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using A = UHWAudioSubsystem;
	TestTrue(TEXT("an even fight: half tension"), FMath::IsNearlyEqual(A::TensionFromState(1.f, 1.f), 0.5f) && FMath::IsNearlyEqual(A::TensionFromState(0.4f, 0.4f), 0.5f));
	TestTrue(TEXT("the keeper far ahead: full tension"), A::TensionFromState(0.2f, 1.f) == 1.f);
	TestTrue(TEXT("the keeper about to fall: calm"), A::TensionFromState(1.f, 0.2f) == 0.f);
	TestTrue(TEXT("rises with the keeper's lead"), A::TensionFromState(0.8f, 0.9f) < A::TensionFromState(0.6f, 0.9f));
	return true;
}

#endif
