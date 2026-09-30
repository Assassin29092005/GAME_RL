#include "HWCombatAnimConfig.h"

#include "HellwalkerRL.h"
#include "HWTypesUE.h"
#include "HWCore/HWMoves.h"

UHWCombatAnimConfig::UHWCombatAnimConfig()
{
	CaptureFromCore();
}

void UHWCombatAnimConfig::CaptureFromCore()
{
	const HW::FCombatTuning& K = HW::Tuning();
	Tuning.ParryWindowFrames = K.ParryWindowFrames;
	Tuning.ParryWhiffRecovery = K.ParryWhiffRecovery;
	Tuning.ParryRewardShaChi = K.ParryRewardShaChi;
	Tuning.ParryRewardStagger = K.ParryRewardStagger;
	Tuning.Hitstun = K.Hitstun;
	Tuning.BossHitstun = K.BossHitstun;
	Tuning.Blockstun = K.Blockstun;
	Tuning.StepIFrameStart = K.StepIFrameStart;
	Tuning.StepIFrameEnd = K.StepIFrameEnd;
	Tuning.StepShaChiCost = K.StepShaChiCost;
	Tuning.PlayerShaChiMax = K.PlayerShaChiMax;
	Tuning.BossShaChiMax = K.BossShaChiMax;
	Tuning.ShaChiRegenPerSec = K.ShaChiRegenPerSec;
	Tuning.BlockChip = K.BlockChip;
	Tuning.PlayerHealthMax = K.PlayerHealthMax;
	Tuning.BossHealthMax = K.BossHealthMax;

	Moves.Reset();
	for (int32 I = 1; I < HW::NumMoves; ++I)
	{
		const HW::FMoveData& M = HW::Move(static_cast<HW::EMoveId>(I));
		FHWMoveFrameData D;
		D.Move = FName(UTF8_TO_TCHAR(M.Name));
		D.Startup = M.Startup;
		D.Active = M.Active;
		D.Recovery = M.Recovery;
		D.CancelFrame = M.CancelFrame;
		D.HyperArmorFrom = M.HyperArmorFrom;
		D.FakeImpactFrame = M.FakeImpactFrame;
		D.Damage = M.Damage;
		D.ShaChiDrainOnBlock = M.ShaChiDrainOnBlock;
		D.Range = M.Range;
		D.HalfWidth = M.HalfWidth;
		D.SweepReach = M.SweepReach;
		D.Distance = M.Distance;
		Moves.Add(D);
	}
}

int32 UHWCombatAnimConfig::ApplyToCore() const
{
	HW::FCombatTuning& K = HW::Tuning();
	K.ParryWindowFrames = Tuning.ParryWindowFrames;
	K.ParryWhiffRecovery = Tuning.ParryWhiffRecovery;
	K.ParryRewardShaChi = Tuning.ParryRewardShaChi;
	K.ParryRewardStagger = Tuning.ParryRewardStagger;
	K.Hitstun = Tuning.Hitstun;
	K.BossHitstun = Tuning.BossHitstun;
	K.Blockstun = Tuning.Blockstun;
	K.StepIFrameStart = Tuning.StepIFrameStart;
	K.StepIFrameEnd = Tuning.StepIFrameEnd;
	K.StepShaChiCost = Tuning.StepShaChiCost;
	K.PlayerShaChiMax = Tuning.PlayerShaChiMax;
	K.BossShaChiMax = Tuning.BossShaChiMax;
	K.ShaChiRegenPerSec = Tuning.ShaChiRegenPerSec;
	K.BlockChip = Tuning.BlockChip;
	K.PlayerHealthMax = Tuning.PlayerHealthMax;
	K.BossHealthMax = Tuning.BossHealthMax;
	HW::ResetMoveTable(); // tuning-derived move fields (step i-frames, parry window) pick up the new values

	int32 Applied = 0;
	for (const FHWMoveFrameData& D : Moves)
	{
		for (int32 I = 1; I < HW::NumMoves; ++I)
		{
			HW::FMoveData& M = HW::MutableMove(static_cast<HW::EMoveId>(I));
			if (D.Move != FName(UTF8_TO_TCHAR(M.Name))) { continue; }
			M.Startup = D.Startup;
			M.Active = D.Active;
			M.Recovery = D.Recovery;
			M.CancelFrame = D.CancelFrame;
			M.HyperArmorFrom = D.HyperArmorFrom;
			M.FakeImpactFrame = D.FakeImpactFrame;
			M.Damage = D.Damage;
			M.ShaChiDrainOnBlock = D.ShaChiDrainOnBlock;
			M.Range = D.Range;
			M.HalfWidth = D.HalfWidth;
			M.SweepReach = D.SweepReach;
			M.Distance = D.Distance;
			++Applied;
			break;
		}
	}
	UE_LOG(LogHellwalkerRL, Log, TEXT("Combat config %s applied: %d moves."), *GetName(), Applied);
	return Applied;
}
