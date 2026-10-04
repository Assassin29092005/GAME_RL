// HellwalkerRL — the parry assist (HWParryAssist.h), against a real HW::FDuel (pure: no world).
//
//   Project.HellwalkerRL.Assist.Window       every parryable keeper swing: a press made on any frame the ring is lit for the real
//                                          impact parries, on any other frame it does not (one frame before the first lit frame
//                                          and one after the last both fail); feints and the delayed heavy first light for the
//                                          moment their wind-up shows — a bait: a press there fails — then for the real one
//   Project.HellwalkerRL.Assist.Buffered     the same while the player is still recovering from a whiffed attack: the ring
//                                          shifts by the frames the player needs (the press waits in the input buffer),
//                                          grey when the player cannot make it
//   Project.HellwalkerRL.Assist.Feints       a feint's ring times its fake swing (what a player sees) and a press there fails;
//                                          after the fake it lights again for the real strike, and a press there parries
//   Project.HellwalkerRL.Assist.Unparryable  the killer thrust and the grab never light
//   Project.HellwalkerRL.Assist.Quiet        nothing for a swing that cannot reach, a resolved swing, or a parry already
//                                          pressed that will catch it
//   Project.HellwalkerRL.Assist.TimeScale    slow motion only while lit and the player can act, and only for RingSlow

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HWParryAssist.h"
#include "HWSettings.h"
#include "HWCore/HWDuel.h"

#include <vector>

namespace
{
	constexpr EAutomationTestFlags HWAssistTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;
	constexpr float NearCm = 100.f;

	/** The keeper's swings always connect; the player's never do (a whiffed attack is only recovery). */
	class FAssistOracle : public HW::IContactOracle
	{
	public:
		bool Contacts(const HW::FDuel& Duel, HW::ESide Attacker) override
		{
			(void)Duel;
			return Attacker == HW::ESide::Boss;
		}
	};

	void Step(HW::FDuel& Duel, HW::IContactOracle& Oracle, int32 N)
	{
		std::vector<HW::FDuelEvent> Ev;
		for (int32 I = 0; I < N; ++I) { Duel.Step(Oracle); Duel.TakeEvents(Ev); }
	}

	/**
	 * A parry pressed between steps, as UHWDuelSubsystem applies it: tried before each step until it commits or the input
	 * buffer runs out (ApplyDueInput), then the duel runs until the keeper's swing resolves.
	 */
	HW::EHitOutcome PressAndResolve(HW::FDuel& Duel, HW::IContactOracle& Oracle)
	{
		bool bPending = true;
		std::vector<HW::FDuelEvent> Ev;
		for (int32 K = 0; K < 120; ++K)
		{
			if (bPending)
			{
				const bool bApplied = Duel.Commit(HW::ESide::Player, HW::EMoveId::PParry);
				if (bApplied || K > HWParryAssist::ParryBufferFrames) { bPending = false; }
			}
			Duel.Step(Oracle);
			Duel.TakeEvents(Ev);
			for (const HW::FDuelEvent& E : Ev)
			{
				if (E.Type == HW::EDuelEvent::Outcome && E.Side == HW::ESide::Boss) { return E.Outcome; }
			}
		}
		return HW::EHitOutcome::None;
	}

	FHWParryCue CueNow(const HW::FDuel& Duel, float Distance = NearCm)
	{
		return HWParryAssist::Evaluate(Duel.Get(HW::ESide::Boss), Duel.Get(HW::ESide::Player), 0.f, Distance, 0.f, false);
	}

	/** The keeper's swing committed on frame 0, the player idle, the duel stepped to the keeper's frame T. */
	void StartSwing(HW::FDuel& Duel, HW::IContactOracle& Oracle, HW::EMoveId Move, int32 T)
	{
		Duel.Reset();
		Duel.Commit(HW::ESide::Boss, Move);
		Step(Duel, Oracle, T);
	}

	/** The ring times the moment the wind-up shows (a feint's fake swing, a delayed heavy's held wind-up) until it passes. */
	bool IsBaitFrame(const HW::FMoveData& M, int32 T) { return M.FakeImpactFrame >= 0 && T < M.FakeImpactFrame; }

	TArray<HW::EMoveId> BossAttacks(bool bParryable)
	{
		TArray<HW::EMoveId> Out;
		for (int32 I = 1; I < HW::NumMoves; ++I)
		{
			const HW::FMoveData& M = HW::Move(static_cast<HW::EMoveId>(I));
			if (M.Owner == HW::ESide::Boss && M.IsAttack() && M.bUnparryable != bParryable) { Out.Add(M.Id); }
		}
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWAssistWindowTest, "Project.HellwalkerRL.Assist.Window", HWAssistTestFlags)

bool FHWAssistWindowTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAssistOracle Oracle;
	const HW::FMoveData& Parry = HW::Move(HW::EMoveId::PParry);
	const int32 First = Parry.Startup;
	const int32 Last = Parry.Startup + Parry.Active - 1;
	const TArray<HW::EMoveId> Moves = BossAttacks(true);
	TestTrue(TEXT("there are parryable keeper swings to test"), Moves.Num() >= 10);
	for (const HW::EMoveId Id : Moves)
	{
		const HW::FMoveData& M = HW::Move(Id);
		int32 Lit = 0;
		int32 BaitLit = 0;
		int32 FirstLitD = -1;
		int32 LastLitD = -1;
		float FirstProgress = -1.f;
		float LastProgress = -1.f;
		for (int32 T = 0; T <= M.Startup; ++T)
		{
			HW::FDuel Duel;
			StartSwing(Duel, Oracle, Id, T);
			const FHWParryCue Cue = CueNow(Duel);
			const HW::EHitOutcome O = PressAndResolve(Duel, Oracle);
			const bool bParried = O == HW::EHitOutcome::Parried;
			const bool bBait = IsBaitFrame(M, T);
			if ((Cue.IsLit() && !bBait) != bParried)
			{
				AddError(FString::Printf(TEXT("%hs: pressed at keeper frame %d (D %d%s): the ring is %s but the press %s"), M.Name, T, M.Startup - T,
					bBait ? TEXT(", a bait frame") : TEXT(""), Cue.IsLit() ? TEXT("lit") : TEXT("off"),
					bParried ? TEXT("parries") : *FString::Printf(TEXT("ends in %hs"), HW::OutcomeName(O))));
			}
			if (Cue.IsLit() && bBait) { ++BaitLit; }
			if (Cue.IsLit() && !bBait)
			{
				++Lit;
				if (FirstLitD < 0) { FirstLitD = Cue.FramesToImpact; FirstProgress = Cue.Progress; }
				LastLitD = Cue.FramesToImpact;
				LastProgress = Cue.Progress;
			}
		}
		// The window is the A3 parry window: D in [First, Last], no countdown before it, nothing after it.
		if (Lit != Parry.Active || FirstLitD != Last || LastLitD != First)
		{
			AddError(FString::Printf(TEXT("%hs: lit on %d frames (D %d..%d); expected %d (D %d..%d)"), M.Name, Lit, FirstLitD, LastLitD, Parry.Active, Last, First));
		}
		if (!(FirstProgress >= 0.f && FirstProgress < LastProgress && LastProgress <= 1.f))
		{
			AddError(FString::Printf(TEXT("%hs: the ring should close as the window does (progress %.2f -> %.2f)"), M.Name, FirstProgress, LastProgress));
		}
		// A feint or the delayed heavy (it reads like the cleave, impact 24, and holds to 36) also lights for the moment its
		// wind-up shows: a full window's worth of bait frames, none of which parries.
		const int32 ExpectedBait = M.FakeImpactFrame >= 0 ? FMath::Min(M.FakeImpactFrame, static_cast<int32>(Parry.Active)) : 0;
		if (BaitLit != ExpectedBait)
		{
			AddError(FString::Printf(TEXT("%hs: lit on %d bait frames; expected %d"), M.Name, BaitLit, ExpectedBait));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWAssistBufferedTest, "Project.HellwalkerRL.Assist.Buffered", HWAssistTestFlags)

bool FHWAssistBufferedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAssistOracle Oracle;
	const HW::FMoveData& Parry = HW::Move(HW::EMoveId::PParry);
	int32 Checked = 0;
	int32 ShiftedLit = 0;
	int32 Grey = 0;
	for (const HW::EMoveId Id : { HW::EMoveId::BFastSlash, HW::EMoveId::BHeavyCleave, HW::EMoveId::BDelayedHeavy, HW::EMoveId::BFeintLate })
	{
		const HW::FMoveData& M = HW::Move(Id);
		for (int32 Q = 0; Q <= M.Startup - 2; Q += 2)  // the player swings (and whiffs) at keeper frame Q...
		{
			for (int32 T = Q; T <= M.Startup; ++T)    // ...and presses parry at keeper frame T, mid-recovery or after
			{
				HW::FDuel Duel;
				StartSwing(Duel, Oracle, Id, Q);
				if (!Duel.CommitPlayerAttack(false)) { AddError(TEXT("the player could not swing")); return false; }
				Step(Duel, Oracle, T - Q);
				const int32 N = Duel.Get(HW::ESide::Player).FramesUntilActionable();
				const FHWParryCue Cue = CueNow(Duel);
				const bool bParried = PressAndResolve(Duel, Oracle) == HW::EHitOutcome::Parried;
				const bool bBait = IsBaitFrame(M, T + FMath::Min(N, HWParryAssist::MaxPressDelay)); // judged where the buffered press commits
				++Checked;
				if ((Cue.IsLit() && !bBait) != bParried)
				{
					AddError(FString::Printf(TEXT("%hs: player swung at %d, pressed at %d (D %d, %d frames to act%s): the ring is %s but the press %s"), M.Name, Q, T,
						M.Startup - T, N, bBait ? TEXT(", a bait frame") : TEXT(""), Cue.IsLit() ? TEXT("lit") : TEXT("off"), bParried ? TEXT("parries") : TEXT("fails")));
				}
				if (Cue.IsLit() && N > 0) { ++ShiftedLit; }
				if (Cue.bInWindow && !Cue.bCanAct)
				{
					++Grey;
					if (bParried) { AddError(FString::Printf(TEXT("%hs: a grey ring (D %d, %d frames to act) but the press parried"), M.Name, M.Startup - T, N)); }
				}
			}
		}
	}
	TestTrue(TEXT("presses made mid-recovery (buffered) were checked"), ShiftedLit > 0);
	TestTrue(TEXT("grey rings (window open, the player cannot make it) were seen"), Grey > 0);
	TestTrue(FString::Printf(TEXT("%d presses checked against a real duel"), Checked), Checked > 100 && Parry.Active > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWAssistFeintsTest, "Project.HellwalkerRL.Assist.Feints", HWAssistTestFlags)

bool FHWAssistFeintsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAssistOracle Oracle;
	int32 Feints = 0;
	for (const HW::EMoveId Id : BossAttacks(true))
	{
		const HW::FMoveData& M = HW::Move(Id);
		if (M.Symbol != HW::ESym::BFeint || M.FakeImpactFrame < 0) { continue; }
		++Feints;
		// The last frame before the fake swing arrives: the ring is lit (it times what the wind-up shows) — and a press there
		// is the bait: the parry is spent before the real strike lands.
		{
			HW::FDuel Duel;
			StartSwing(Duel, Oracle, Id, M.FakeImpactFrame - 1);
			const FHWParryCue Cue = CueNow(Duel);
			TestTrue(FString::Printf(TEXT("%hs: lit for the fake swing (what the player sees)"), M.Name), Cue.IsLit());
			TestTrue(FString::Printf(TEXT("%hs: ...and a press there is baited"), M.Name), PressAndResolve(Duel, Oracle) != HW::EHitOutcome::Parried);
		}
		// As the fake passes, the real strike is still incoming (no ring), then it lights for it and a press there parries.
		{
			HW::FDuel Duel;
			StartSwing(Duel, Oracle, Id, M.FakeImpactFrame);
			const FHWParryCue Cue = CueNow(Duel);
			TestTrue(FString::Printf(TEXT("%hs: at the fake the real swing is incoming"), M.Name), Cue.bActive && Cue.bIncoming && !Cue.bInWindow);
		}
		{
			HW::FDuel Duel;
			StartSwing(Duel, Oracle, Id, M.Startup - 1);
			TestTrue(FString::Printf(TEXT("%hs: lit for the real impact"), M.Name), CueNow(Duel).IsLit());
			TestTrue(FString::Printf(TEXT("%hs: ...and a press there parries"), M.Name), PressAndResolve(Duel, Oracle) == HW::EHitOutcome::Parried);
		}
	}
	TestTrue(TEXT("the feints were found"), Feints >= 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWAssistUnparryableTest, "Project.HellwalkerRL.Assist.Unparryable", HWAssistTestFlags)

bool FHWAssistUnparryableTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAssistOracle Oracle;
	const TArray<HW::EMoveId> Moves = BossAttacks(false);
	TestTrue(TEXT("the killer thrust and the grab are unparryable"), Moves.Contains(HW::EMoveId::BKillerThrust) && Moves.Contains(HW::EMoveId::BGrab));
	for (const HW::EMoveId Id : Moves)
	{
		const HW::FMoveData& M = HW::Move(Id);
		for (int32 T = 0; T <= M.Startup; ++T)
		{
			HW::FDuel Duel;
			StartSwing(Duel, Oracle, Id, T);
			const FHWParryCue Cue = CueNow(Duel);
			if (Cue.bActive || Cue.bInWindow || Cue.IsLit())
			{
				AddError(FString::Printf(TEXT("%hs: a cue at keeper frame %d (it cannot be parried: step it)"), M.Name, T));
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWAssistQuietTest, "Project.HellwalkerRL.Assist.Quiet", HWAssistTestFlags)

bool FHWAssistQuietTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAssistOracle Oracle;
	const HW::EMoveId Id = HW::EMoveId::BHeavyCleave;
	const HW::FMoveData& M = HW::Move(Id);
	HW::FDuel Duel;
	StartSwing(Duel, Oracle, Id, M.Startup - 4);
	TestTrue(TEXT("in reach: lit"), CueNow(Duel).IsLit());
	TestFalse(TEXT("out of reach: nothing"), CueNow(Duel, M.Range + 100.f).bActive);
	{
		// A sweep reaches sideways, never farther ahead: straight ahead past its range it cannot connect; to its swept side
		// (the defender's left for SweepLeft = the keeper's right, +Y) it can, far out.
		const HW::FMoveData& Sweep = HW::Move(HW::EMoveId::BSweepLeft);
		TestFalse(TEXT("a sweep straight ahead past its range: cannot reach"), HWParryAssist::CanReach(Sweep, Sweep.Range + 80.f, 0.f));
		TestTrue(TEXT("a sweep, the player out to its swept side: reaches"), HWParryAssist::CanReach(Sweep, 120.f, Sweep.SweepReach - 20.f));
		TestFalse(TEXT("a sweep, the player out to its other side: cannot reach"), HWParryAssist::CanReach(Sweep, 120.f, -Sweep.SweepReach + 20.f));
	}
	{
		// An earlier attack press waiting in the input buffer commits first: the parry would not come out — grey, not red.
		const FHWParryCue Queued = HWParryAssist::Evaluate(Duel.Get(HW::ESide::Boss), Duel.Get(HW::ESide::Player), 0.f, NearCm, 0.f, false, true);
		TestTrue(TEXT("another press pending: the window shows, grey"), Queued.bActive && Queued.bInWindow && !Queued.bCanAct && !Queued.IsLit());
	}

	// Pressed already, in time: the ring has done its job.
	TestTrue(TEXT("the parry commits"), Duel.Commit(HW::ESide::Player, HW::EMoveId::PParry));
	Step(Duel, Oracle, 1);
	TestFalse(TEXT("a parry already pressed that will catch it: nothing"), CueNow(Duel).bActive);
	const HW::EHitOutcome O = PressAndResolve(Duel, Oracle);
	TestTrue(TEXT("...and it parries"), O == HW::EHitOutcome::Parried);
	TestFalse(TEXT("a resolved swing: nothing"), CueNow(Duel).bActive);

	// Pressed too early: the spent parry cannot catch it, a second press cannot come out in time — grey, not red.
	StartSwing(Duel, Oracle, Id, M.Startup - 20);
	TestTrue(TEXT("the early parry commits"), Duel.Commit(HW::ESide::Player, HW::EMoveId::PParry));
	Step(Duel, Oracle, 14);
	const FHWParryCue Late = CueNow(Duel);
	TestTrue(TEXT("a spent parry: the window shows grey"), Late.bActive && Late.bInWindow && !Late.bCanAct && !Late.IsLit());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWAssistTimeScaleTest, "Project.HellwalkerRL.Assist.TimeScale", HWAssistTestFlags)

bool FHWAssistTimeScaleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FHWParryCue Lit;
	Lit.bActive = true;
	Lit.bInWindow = true;
	Lit.bCanAct = true;
	FHWParryCue Grey = Lit;
	Grey.bCanAct = false;
	FHWParryCue Incoming = Lit;
	Incoming.bInWindow = false;
	Incoming.bIncoming = true;

	FHWAssistParams Slow;
	Slow.bRing = true;
	Slow.SlowScale = 0.6f;
	FHWAssistParams RingOnly = Slow;
	RingOnly.SlowScale = 1.f;
	FHWAssistParams Off;

	TestTrue(TEXT("lit and the player can act: slow"), FMath::IsNearlyEqual(HWParryAssist::TargetTimeScale(Lit, Slow), 0.6f));
	TestEqual(TEXT("not lit (incoming): real time"), HWParryAssist::TargetTimeScale(Incoming, Slow), 1.f);
	TestEqual(TEXT("the player cannot act (grey): real time"), HWParryAssist::TargetTimeScale(Grey, Slow), 1.f);
	TestEqual(TEXT("nothing coming: real time"), HWParryAssist::TargetTimeScale(FHWParryCue{}, Slow), 1.f);
	TestEqual(TEXT("Ring only: real time"), HWParryAssist::TargetTimeScale(Lit, RingOnly), 1.f);
	TestEqual(TEXT("Off: real time"), HWParryAssist::TargetTimeScale(Lit, Off), 1.f);

	// The settings' own presets (UHWSettingsSubsystem::AssistFor): only RingSlow slows, by the difficulty's scale.
	for (const EHWDifficulty D : { EHWDifficulty::Easy, EHWDifficulty::Normal, EHWDifficulty::Hard, EHWDifficulty::Hellwalker })
	{
		const FString Name = UHWSettingsSubsystem::DifficultyName(D);
		const float Want = UHWSettingsSubsystem::PresetFor(D).AssistSlowScale;
		TestTrue(FString::Printf(TEXT("%s, RingSlow: slows to its scale when lit"), *Name),
			FMath::IsNearlyEqual(HWParryAssist::TargetTimeScale(Lit, UHWSettingsSubsystem::AssistFor(EHWParryAssist::RingSlow, D)), FMath::Min(Want, 1.f)));
		TestEqual(FString::Printf(TEXT("%s, Ring: never slows"), *Name), HWParryAssist::TargetTimeScale(Lit, UHWSettingsSubsystem::AssistFor(EHWParryAssist::Ring, D)), 1.f);
		TestEqual(FString::Printf(TEXT("%s, Off: never slows"), *Name), HWParryAssist::TargetTimeScale(Lit, UHWSettingsSubsystem::AssistFor(EHWParryAssist::Off, D)), 1.f);
		TestTrue(FString::Printf(TEXT("%s, RingSlow: slows only while lit and you can act"), *Name),
			HWParryAssist::TargetTimeScale(Grey, UHWSettingsSubsystem::AssistFor(EHWParryAssist::RingSlow, D)) == 1.f
			&& HWParryAssist::TargetTimeScale(Incoming, UHWSettingsSubsystem::AssistFor(EHWParryAssist::RingSlow, D)) == 1.f);
	}
	return true;
}

#endif
