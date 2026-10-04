#include "HWParryAssist.h"

#include "HWCore/HWMoves.h"

bool HWParryAssist::CanReach(const HW::FMoveData& M, float AlongCm, float LateralCm)
{
	// FHWContactOracle::BuildVolume's box (UE: +Y is the attacker's right; "SweepLeft" tracks toward the defender's left,
	// the attacker's right), grown by the player's capsule and a small margin.
	const float Grow = PlayerRadiusCm + ReachMarginCm;
	const float XMax = FMath::Max(40.f, M.Range - 30.f);
	if (AlongCm < -PlayerRadiusCm || AlongCm > XMax + Grow) { return false; }
	float YMin = -M.HalfWidth;
	float YMax = M.HalfWidth;
	switch (M.Coverage)
	{
	case HW::ECoverage::SweepLeft:  YMax = M.SweepReach; break;
	case HW::ECoverage::SweepRight: YMin = -M.SweepReach; break;
	case HW::ECoverage::Wide:       YMin = -M.SweepReach; YMax = M.SweepReach; break;
	default: break;
	}
	return LateralCm >= YMin - Grow && LateralCm <= YMax + Grow;
}

FHWParryCue HWParryAssist::Evaluate(const HW::FFighter& Boss, const HW::FFighter& Player, float FrameAlpha, float AlongCm, float LateralCm,
	bool bHitstop, bool bOtherPressPending)
{
	FHWParryCue Cue;
	if (Boss.State != HW::EFighterState::Acting || Boss.IsDead() || Player.IsDead()) { return Cue; }
	const HW::FMoveData& M = Boss.CurrentMove();
	if (!M.IsAttack() || M.bUnparryable || Boss.bSwingResolved || Boss.bSwingConnected) { return Cue; }
	// A press now commits on step N (0 = the next one); while the player recovers every press made now commits on the
	// same step, so D - N stays put until the player is free.
	const int32 N = Player.FramesUntilActionable();
	// The impact the wind-up SHOWS: a feint's fake swing, a delayed heavy's held wind-up (FMoveData::PerceivedImpact) —
	// until that moment passes (judged on the step a press made now commits on), then the real one. The ring times what a
	// player can see, so the keeper's baits still bait: a player who parries every ring is a habit it can learn and fake
	// (Thesis.bat --arc; README).
	const int32 CommitT = Boss.T + (N <= MaxPressDelay ? N : 0);
	const int32 Impact = (M.FakeImpactFrame >= 0 && CommitT < M.FakeImpactFrame) ? M.FakeImpactFrame : M.Startup;
	const int32 D = Impact - Boss.T;
	if (D < 1 || !CanReach(M, AlongCm, LateralCm)) { return Cue; }

	const HW::FMoveData& Parry = HW::Move(HW::EMoveId::PParry);
	const int32 First = Parry.Startup;
	const int32 Last = Parry.Startup + Parry.Active - 1;
	if (Player.State == HW::EFighterState::Acting && Player.CurrentMove().Kind == HW::EMoveKind::Parry && !Player.bParrySucceeded)
	{
		// Already pressed, and live when the swing lands: the ring has done its job (a second press could only fail).
		const HW::FMoveData& Live = Player.CurrentMove();
		const int32 TAtImpact = Player.T + D;
		if (TAtImpact >= Live.Startup && TAtImpact < Live.Startup + Live.Active) { return Cue; }
	}
	Cue.bActive = true;
	Cue.FramesToImpact = D;
	Cue.bCanAct = !bOtherPressPending && N <= MaxPressDelay && D - N >= First;
	const int32 Shift = Cue.bCanAct ? N : 0;
	Cue.WindowFirst = First + Shift;
	Cue.WindowLast = Last + Shift;
	Cue.bInWindow = D >= Cue.WindowFirst && D <= Cue.WindowLast;
	Cue.bIncoming = D > Cue.WindowLast;
	// Continuous time to impact: the frame alpha moves it toward the next step — unless no step is coming (hit-stop) or
	// the player is still recovering (the press would commit on the same step anyway).
	const float Alpha = (bHitstop || (Cue.bCanAct && N > 0)) ? 0.f : FMath::Clamp(FrameAlpha, 0.f, 1.f);
	const float Span = static_cast<float>(Cue.WindowLast - Cue.WindowFirst + 1);
	Cue.Progress = FMath::Clamp((static_cast<float>(Cue.WindowLast - D) + Alpha) / FMath::Max(Span, 1.f), 0.f, 1.f);
	return Cue;
}

float HWParryAssist::TargetTimeScale(const FHWParryCue& Cue, const FHWAssistParams& Assist)
{
	if (!Assist.bRing || !Cue.IsLit() || Assist.SlowScale >= 1.f) { return 1.f; }
	return FMath::Clamp(Assist.SlowScale, 0.05f, 1.f);
}
