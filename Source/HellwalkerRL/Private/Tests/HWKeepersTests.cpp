// HellwalkerRL — the keepers' difficulty and identity wiring (pure functions: no world).
//
//   Project.HellwalkerRL.Keepers.Identity    which keeper the RL network plays: command line > explicit > look > script
//   Project.HellwalkerRL.Keepers.Insight     consistent reads raise the keeper's skill across fights within the difficulty's
//                                            [SkillLo, SkillHi] (looser and with a breather while it does not know you);
//                                            changed tricks lower it; the session's ResetMemory drops it
//   Project.HellwalkerRL.Keepers.MusicTension the duel music tightens as the keeper pulls ahead, calms when it is losing

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HWDuelSubsystem.h"
#include "HWSessionSubsystem.h"
#include "HWSettings.h"
#include "HWAudio.h"
#include "HWCore/HWRLBrain.h"
#include "HWCore/HWRLTypes.h"
#include "Engine/GameInstance.h"
#include "UObject/Package.h"

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWKeepersInsightTest, "Project.HellwalkerRL.Keepers.Insight", HWKeepersTestFlags)

bool FHWKeepersInsightTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	constexpr float Eps = 1.0e-4f;
	// Pure: every difficulty's keeper walks its skill range with the insight.
	for (EHWDifficulty D : { EHWDifficulty::Easy, EHWDifficulty::Normal, EHWDifficulty::Hard, EHWDifficulty::Hellwalker })
	{
		const FHWDifficultyPreset P = UHWSettingsSubsystem::PresetFor(D);
		const FString Name = UHWSettingsSubsystem::DifficultyName(D);
		HW::FRLInsight Insight;
		float Skill = -1.f;
		float Temperature = -1.f;
		int32 Gap = -1;
		UHWSessionSubsystem::KeeperConfigFor(D, Insight, Skill, Temperature, Gap);
		TestTrue(FString::Printf(TEXT("%s, a stranger: the bottom of its range, loose, with a breather"), *Name),
			FMath::IsNearlyEqual(Skill, P.SkillLo, Eps) && Temperature > 0.f && Gap > 0);
		// The same trick every fight: its calls keep coming true (18 of 20), so the skill climbs, fight after fight.
		float Last = Skill;
		bool bRising = true;
		bool bInRange = true;
		for (int32 Fight = 0; Fight < 5; ++Fight)
		{
			Insight.AfterFight(20, 18);
			UHWSessionSubsystem::KeeperConfigFor(D, Insight, Skill, Temperature, Gap);
			bRising = bRising && Skill > Last;
			bInRange = bInRange && Skill >= P.SkillLo - Eps && Skill <= P.SkillHi + Eps;
			Last = Skill;
		}
		TestTrue(FString::Printf(TEXT("%s: consistent reads raise the skill every fight"), *Name), bRising);
		TestTrue(FString::Printf(TEXT("%s: ... within [%.2f, %.2f]"), *Name, P.SkillLo, P.SkillHi), bInRange);
		TestTrue(FString::Printf(TEXT("%s: after five fights it knows you: past halfway, greedy, no breather"), *Name),
			Skill > P.SkillLo + 0.5f * (P.SkillHi - P.SkillLo) && Temperature == 0.f && Gap == 0);
		// You change tricks: its calls go wrong (4 of 20) and it has to learn you again.
		const float Knowing = Skill;
		Insight.AfterFight(20, 4);
		UHWSessionSubsystem::KeeperConfigFor(D, Insight, Skill, Temperature, Gap);
		TestTrue(FString::Printf(TEXT("%s: changed tricks lower it"), *Name), Skill < Knowing && Skill >= P.SkillLo - Eps);
	}

	// The session: insight from the notebook's calls, per recorded fight; ResetMemory drops it.
	// A bare game instance (never Init'd: no subsystems, so no settings — the default difficulty, Normal) as the session's
	// outer (game instance subsystems must live in one).
	UGameInstance* GI = NewObject<UGameInstance>(GetTransientPackage());
	UHWSessionSubsystem* S = NewObject<UHWSessionSubsystem>(GI);
	const FHWDifficultyPreset Normal = UHWSettingsSubsystem::PresetFor(FHWSettingsData().Difficulty);
	float Skill = -1.f;
	float Temperature = -1.f;
	bool bAdaptive = false;
	int32 Gap = -1;
	S->GetKeeperConfig(Skill, Temperature, bAdaptive, Gap);
	TestTrue(TEXT("a fresh session: insight 0, the bottom of the range, set by the insight ramp"), S->GetInsight() == 0.f
		&& FMath::IsNearlyEqual(Skill, Normal.SkillLo, Eps) && bAdaptive);
	float Last = Skill;
	bool bRising = true;
	bool bInRange = true;
	for (int32 Fight = 0; Fight < 5; ++Fight)
	{
		HW::FRLNotebook& Book = S->GetNotebookMutable();
		Book.Predictions += 20; // what FRLBrain::FlushNotebook adds over a fight: 20 calls on its attacks, 18 right
		Book.Correct += 18;
		Book.SwingPredictions += 20; // (insight reads the calls on its attacks)
		Book.SwingCorrect += 18;
		Book.FightPredictions[Fight] += 20;
		Book.FightCorrect[Fight] += 18;
		++Book.Fights;
		const float Change = S->RecordFightForAdaptive(false, 0.f, 0.8f);
		S->GetKeeperConfig(Skill, Temperature, bAdaptive, Gap);
		bRising = bRising && Change > 0.f && Skill > Last && FMath::IsNearlyEqual(Change, S->GetLastInsightChange()) && FMath::IsNearlyEqual(Skill, S->GetAdaptiveSkill());
		bInRange = bInRange && Skill >= Normal.SkillLo - Eps && Skill <= Normal.SkillHi + Eps;
		Last = Skill;
	}
	TestTrue(TEXT("consistent reads raise the session's keeper every fight"), bRising);
	TestTrue(TEXT("... within Normal's range"), bInRange);
	TestTrue(TEXT("a record with no new calls moves nothing"), S->RecordFightForAdaptive(true, 0.5f, 0.f) == 0.f);
	S->ResetMemory();
	S->GetKeeperConfig(Skill, Temperature, bAdaptive, Gap);
	TestTrue(TEXT("ResetMemory zeroes the insight (and the notebook)"), S->GetInsight() == 0.f && S->GetLastInsightChange() == 0.f
		&& S->GetNotebook().Predictions == 0 && FMath::IsNearlyEqual(Skill, Normal.SkillLo, Eps));
	S->GetNotebookMutable().Predictions = 20;
	S->GetNotebookMutable().Correct = 18;
	S->GetNotebookMutable().SwingPredictions = 20;
	S->GetNotebookMutable().SwingCorrect = 18;
	TestTrue(TEXT("after a reset the next fight counts from zero again"), S->RecordFightForAdaptive(false, 0.f, 1.f) > 0.f);
	S->MarkAsGarbage();
	GI->MarkAsGarbage();
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
