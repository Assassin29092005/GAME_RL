// Hellwalker — C2 automation tests.
//
//   Project.HellwalkerRL.Anim.FrameLock   the clip's contact pose lands EXACTLY on the move's impact frame, for
//                                       every attack; a delayed heavy reads as the ordinary heavy until it
//                                       holds; a feint's fake swing visibly arrives, then pulls back.
//   Project.HellwalkerRL.Anim.Casts       with the Fab packs present: every cast loads, every attack clip has a
//                                       measured contact, and every sweep's clip crosses the way its hitbox
//                                       tracks (legibility, PLAN §2.4a). Skipped when the packs are absent.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HWAnimTypes.h"
#include "HWCore/HWMoves.h"

namespace
{
	constexpr EAutomationTestFlags HWAnimTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	FHWClip SyntheticSwing()
	{
		FHWClip C;
		C.Start = 0.f;
		C.Apex = 0.30f;
		C.Contact = 0.45f;
		C.End = 1.50f;
		return C;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWAnimFrameLockTest, "Project.HellwalkerRL.Anim.FrameLock", HWAnimTestFlags)

bool FHWAnimFrameLockTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	HW::ResetMoveTable();
	const FHWClip C = SyntheticSwing();
	int32 Checked = 0;
	for (int32 I = 1; I < HW::NumMoves; ++I)
	{
		const HW::FMoveData& M = HW::Move(static_cast<HW::EMoveId>(I));
		if (!M.IsAttack()) { continue; }
		++Checked;
		const float AtImpact = HWWarpMoveTime(M, C, static_cast<float>(M.Startup));
		if (!FMath::IsNearlyEqual(AtImpact, C.Contact, 1e-4f))
		{
			AddError(FString::Printf(TEXT("%hs: clip time at the impact frame is %.4f, contact is %.4f"), M.Name, AtImpact, C.Contact));
		}
		// Before impact the pose must not already be past contact (a swing must not land early).
		for (int32 T = 0; T < M.Startup; ++T)
		{
			if (HWWarpMoveTime(M, C, static_cast<float>(T)) > C.Contact + 1e-4f)
			{
				AddError(FString::Printf(TEXT("%hs: frame %d already shows the contact pose (impact %d)"), M.Name, T, M.Startup));
				break;
			}
		}
		// After impact: the follow-through only goes forward.
		float Last = AtImpact;
		for (int32 T = M.Startup + 1; T <= M.TotalFrames(); ++T)
		{
			const float Now = HWWarpMoveTime(M, C, static_cast<float>(T));
			if (Now + 1e-4f < Last) { AddError(FString::Printf(TEXT("%hs: follow-through runs backwards at frame %d"), M.Name, T)); break; }
			Last = Now;
		}
	}
	TestTrue(TEXT("attacks checked"), Checked >= 10);

	// The delayed heavy is the cleave until the cleave would start its downswing.
	const HW::FMoveData& Cleave = HW::Move(HW::EMoveId::BHeavyCleave);
	const HW::FMoveData& Delayed = HW::Move(HW::EMoveId::BDelayedHeavy);
	TestEqual(TEXT("delayed heavy imitates the cleave's impact"), Delayed.FakeImpactFrame, Cleave.Startup);
	for (int32 T = 0; T <= Delayed.FakeImpactFrame / 2; ++T)
	{
		const float A = HWWarpMoveTime(Cleave, C, static_cast<float>(T));
		const float B = HWWarpMoveTime(Delayed, C, static_cast<float>(T));
		if (!FMath::IsNearlyEqual(A, B, 1e-3f))
		{
			AddError(FString::Printf(TEXT("delayed heavy differs from the cleave at frame %d (%.3f vs %.3f)"), T, B, A));
			break;
		}
	}
	// ...then it holds at the apex: at the frame the cleave lands, the delayed heavy is NOT at contact.
	TestTrue(TEXT("delayed heavy holds while the cleave lands"),
		HWWarpMoveTime(Delayed, C, static_cast<float>(Cleave.Startup)) < C.Contact - 0.05f);

	// Feints: the fake swing is past the apex at the fake impact, back at the apex before the real one.
	for (HW::EMoveId Id : { HW::EMoveId::BFeintEarly, HW::EMoveId::BFeintMid, HW::EMoveId::BFeintLate })
	{
		const HW::FMoveData& F = HW::Move(Id);
		const float AtFake = HWWarpMoveTime(F, C, static_cast<float>(F.FakeImpactFrame));
		TestTrue(FString::Printf(TEXT("%hs: the fake swing arrives"), F.Name), AtFake > C.Apex + 0.02f);
		float Lowest = AtFake;
		for (int32 T = F.FakeImpactFrame + 1; T < F.Startup; ++T) { Lowest = FMath::Min(Lowest, HWWarpMoveTime(F, C, static_cast<float>(T))); }
		TestTrue(FString::Printf(TEXT("%hs: it pulls back to the top before the real strike"), F.Name), Lowest <= C.Apex + 1e-3f);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWAnimCastsTest, "Project.HellwalkerRL.Anim.Casts", HWAnimTestFlags)

bool FHWAnimCastsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	HW::ResetMoveTable();
	int32 Loaded = 0;
	for (const FName& Name : HWCastNames())
	{
		UHWAnimSet* Set = UHWAnimSet::Load(GetTransientPackage(), Name);
		if (Set == nullptr)
		{
			AddInfo(FString::Printf(TEXT("%s: pack not in the project — greybox (skipped)."), *Name.ToString()));
			continue;
		}
		++Loaded;
		TestEqual(FString::Printf(TEXT("%s: every authored clip loads"), *Name.ToString()), Set->MissingClips, 0);
		for (int32 I = 1; I < HW::NumMoves; ++I)
		{
			const HW::FMoveData& M = HW::Move(static_cast<HW::EMoveId>(I));
			const FHWClip& C = Set->Move(M.Id);
			if (!C.IsValid() || !M.IsAttack()) { continue; }
			if (C.Contact < C.Start || C.Contact > C.End)
			{
				AddError(FString::Printf(TEXT("%s %hs: no measured contact"), *Name.ToString(), M.Name));
				continue;
			}
			// Legibility: a sweep's weapon crosses toward the side its hitbox tracks.
			if (M.Coverage == HW::ECoverage::SweepLeft && C.SweepSide < 0.3f)
			{
				AddError(FString::Printf(TEXT("%s %hs: clip %s crosses %+.2f, must cross to the attacker's right (> +0.3)"),
					*Name.ToString(), M.Name, *C.Anim->GetName(), C.SweepSide));
			}
			if (M.Coverage == HW::ECoverage::SweepRight && C.SweepSide > -0.3f)
			{
				AddError(FString::Printf(TEXT("%s %hs: clip %s crosses %+.2f, must cross to the attacker's left (< -0.3)"),
					*Name.ToString(), M.Name, *C.Anim->GetName(), C.SweepSide));
			}
			// Playback rate the frame lock needs to land contact on the impact frame.
			const float Rate = (C.Contact - C.Start) / FMath::Max(static_cast<float>(M.Startup) / HW::FramesPerSecond, 0.001f);
			AddInfo(FString::Printf(TEXT("%s %-16hs %-28s contact %.2f s  side %+.2f  startup rate %.2fx"),
				*Name.ToString(), M.Name, *C.Anim->GetName(), C.Contact, C.SweepSide, Rate));
		}
	}
	if (Loaded == 0) { AddInfo(TEXT("No Fab packs in Content/: nothing to check (the greybox is the fallback).")); }
	return !HasAnyErrors();
}

#endif
