// Hellwalker — engine-free core. Default move table (PLAN §6 placeholders, all wrong on purpose).

#include "HWCore/HWMoves.h"

#include <array>

namespace HW
{
	namespace
	{
		FMoveData MakeAttack(const char* Name, EMoveId Id, ESide Owner, ESym Sym, int32_t S, int32_t A, int32_t R, int32_t Cancel,
			float Dmg, float Drain, float Range, float HalfWidth)
		{
			FMoveData M;
			M.Name = Name; M.Id = Id; M.Kind = EMoveKind::Attack; M.Owner = Owner; M.Symbol = Sym;
			M.Slot = ESlotType::Attack;
			M.Startup = S; M.Active = A; M.Recovery = R; M.CancelFrame = Cancel;
			M.Damage = Dmg; M.ShaChiDrainOnBlock = Drain; M.Range = Range; M.HalfWidth = HalfWidth;
			return M;
		}

		FMoveData MakeStep(const char* Name, EMoveId Id, ESide Owner, ESym Sym, EDir Dir, int32_t Frames, int32_t Recovery,
			int32_t IStart, int32_t IEnd, float Dist, float Cost)
		{
			FMoveData M;
			M.Name = Name; M.Id = Id; M.Kind = EMoveKind::Step; M.Owner = Owner; M.Symbol = Sym;
			M.Slot = ESlotType::Reposition;
			M.Startup = 0; M.Active = Frames; M.Recovery = Recovery;
			M.Dir = Dir; M.Distance = Dist; M.IFrameStart = IStart; M.IFrameEnd = IEnd; M.ShaChiCost = Cost;
			return M;
		}

		FMoveData MakeLoco(const char* Name, EMoveId Id, ESym Sym, EDir Dir, int32_t Frames, int32_t Recovery, float Dist, int32_t Cancel)
		{
			FMoveData M;
			M.Name = Name; M.Id = Id; M.Kind = EMoveKind::Locomotion; M.Owner = ESide::Boss; M.Symbol = Sym;
			M.Slot = ESlotType::Reposition;
			M.Startup = 0; M.Active = Frames; M.Recovery = Recovery; M.Dir = Dir; M.Distance = Dist;
			M.CancelFrame = Cancel;
			return M;
		}

		std::array<FMoveData, NumMoves> BuildDefaults()
		{
			std::array<FMoveData, NumMoves> T{};
			const FCombatTuning& K = Tuning();
			auto Put = [&T](const FMoveData& M) { T[static_cast<size_t>(M.Id)] = M; };

			// ---------------- Player: Twin Blades (§6 fast 8/4/14 cancel 20, heavy 22/6/30 cancel 44, armor from 10)
			{
				FMoveData M = MakeAttack("TwinLight1", EMoveId::PLight1, ESide::Player, ESym::Light, 8, 4, 14, 20, 20.f, 10.f, 200.f, 70.f);
				Put(M);
				M = MakeAttack("TwinLight2", EMoveId::PLight2, ESide::Player, ESym::Light, 8, 4, 14, 20, 20.f, 10.f, 200.f, 70.f);
				Put(M);
				M = MakeAttack("TwinLight3", EMoveId::PLight3, ESide::Player, ESym::Light, 10, 4, 18, 24, 28.f, 10.f, 210.f, 80.f);
				Put(M);
				M = MakeAttack("TwinHeavy", EMoveId::PHeavy, ESide::Player, ESym::Heavy, 22, 6, 30, 44, 55.f, 35.f, 230.f, 95.f);
				M.HyperArmorFrom = 10;
				Put(M);
			}
			// ---------------- Player: Glaive (C1 — longer, slower, different cancel points)
			{
				FMoveData M = MakeAttack("GlaiveLight1", EMoveId::GLight1, ESide::Player, ESym::Light, 11, 5, 16, 24, 26.f, 10.f, 270.f, 60.f);
				M.Weapon = 1; Put(M);
				M = MakeAttack("GlaiveLight2", EMoveId::GLight2, ESide::Player, ESym::Light, 12, 5, 18, 26, 28.f, 10.f, 270.f, 60.f);
				M.Weapon = 1; Put(M);
				M = MakeAttack("GlaiveHeavy", EMoveId::GHeavy, ESide::Player, ESym::Heavy, 26, 8, 32, 48, 66.f, 40.f, 300.f, 110.f);
				M.Weapon = 1; M.HyperArmorFrom = 12; Put(M);
			}
			// ---------------- Player: parry — press frame is t=0, live t=1..8, then whiff recovery
			{
				FMoveData M;
				M.Name = "Parry"; M.Id = EMoveId::PParry; M.Kind = EMoveKind::Parry; M.Owner = ESide::Player; M.Symbol = ESym::Parry;
				M.Startup = 1; M.Active = K.ParryWindowFrames; M.Recovery = K.ParryWhiffRecovery;
				Put(M);
			}
			// ---------------- Player: ghoststep — i-frames 3-18 of 24, recovery 10, 12 sha-chi
			Put(MakeStep("StepF", EMoveId::PStepF, ESide::Player, ESym::StepF, EDir::Forward, K.StepFrames, K.StepRecovery, K.StepIFrameStart, K.StepIFrameEnd, K.StepDistance, K.StepShaChiCost));
			Put(MakeStep("StepB", EMoveId::PStepB, ESide::Player, ESym::StepB, EDir::Back, K.StepFrames, K.StepRecovery, K.StepIFrameStart, K.StepIFrameEnd, K.StepDistance, K.StepShaChiCost));
			Put(MakeStep("StepL", EMoveId::PStepL, ESide::Player, ESym::StepL, EDir::Left, K.StepFrames, K.StepRecovery, K.StepIFrameStart, K.StepIFrameEnd, K.StepDistance, K.StepShaChiCost));
			Put(MakeStep("StepR", EMoveId::PStepR, ESide::Player, ESym::StepR, EDir::Right, K.StepFrames, K.StepRecovery, K.StepIFrameStart, K.StepIFrameEnd, K.StepDistance, K.StepShaChiCost));
			{
				FMoveData M;
				M.Name = "Switch"; M.Id = EMoveId::PSwitch; M.Kind = EMoveKind::Switch; M.Owner = ESide::Player; M.Symbol = ESym::Switch;
				M.Startup = 0; M.Active = 6; M.Recovery = 0; // switching is cheap — variety is the counter-play (C1)
				Put(M);
			}

			// ---------------- Boss attacks
			Put(MakeAttack("FastSlash", EMoveId::BFastSlash, ESide::Boss, ESym::BFast, 12, 4, 18, 22, 18.f, 10.f, 240.f, 80.f));
			{
				// Sweeps track toward the defender's side. Long active windows catch the END of a ghoststep:
				// early variant catches a step started at boss-frame <= ~6, late variant ~3-14.
				FMoveData M = MakeAttack("SweepLeft", EMoveId::BSweepLeft, ESide::Boss, ESym::BFast, 14, 12, 20, 32, 17.f, 10.f, 250.f, 80.f);
				M.Coverage = ECoverage::SweepLeft; M.SweepReach = 340.f; Put(M);
				M = MakeAttack("SweepLeftLate", EMoveId::BSweepLeftLate, ESide::Boss, ESym::BFast, 22, 12, 20, 40, 19.f, 10.f, 250.f, 80.f);
				M.Coverage = ECoverage::SweepLeft; M.SweepReach = 340.f; Put(M);
				M = MakeAttack("SweepRight", EMoveId::BSweepRight, ESide::Boss, ESym::BFast, 14, 12, 20, 32, 17.f, 10.f, 250.f, 80.f);
				M.Coverage = ECoverage::SweepRight; M.SweepReach = 340.f; Put(M);
				M = MakeAttack("SweepRightLate", EMoveId::BSweepRightLate, ESide::Boss, ESym::BFast, 22, 12, 20, 40, 19.f, 10.f, 250.f, 80.f);
				M.Coverage = ECoverage::SweepRight; M.SweepReach = 340.f; Put(M);
			}
			{
				FMoveData M = MakeAttack("HeavyCleave", EMoveId::BHeavyCleave, ESide::Boss, ESym::BHeavy, 24, 6, 30, 42, 40.f, 35.f, 270.f, 115.f);
				M.HyperArmorFrom = 10; Put(M);
				// Delayed heavy: the wind-up reads like HeavyCleave (impact 24), then holds — punishes parries and
				// dodges timed to the ordinary heavy (Coppermaul-style). No fake swing is shown (not a feint).
				M = MakeAttack("DelayedHeavy", EMoveId::BDelayedHeavy, ESide::Boss, ESym::BHeavy, 36, 6, 28, 50, 44.f, 35.f, 270.f, 115.f);
				M.HyperArmorFrom = 10; M.FakeImpactFrame = 24; Put(M);
				// Heavy tracking sweeps: a heavy slot's answer to a player who always ghoststeps the same way
				// (Red Wraith, theme research). Slower than the fast sweeps, so legibility (a) lets heavy slots use them.
				M = MakeAttack("HeavySweepLeft", EMoveId::BHeavySweepLeft, ESide::Boss, ESym::BHeavy, 26, 10, 30, 46, 36.f, 30.f, 260.f, 100.f);
				M.Coverage = ECoverage::SweepLeft; M.SweepReach = 340.f; M.HyperArmorFrom = 10; Put(M);
				M = MakeAttack("HeavySweepRight", EMoveId::BHeavySweepRight, ESide::Boss, ESym::BHeavy, 26, 10, 30, 46, 36.f, 30.f, 260.f, 100.f);
				M.Coverage = ECoverage::SweepRight; M.SweepReach = 340.f; M.HyperArmorFrom = 10; Put(M);
			}
			{
				// Feints: a fake swing "arrives" at FakeImpactFrame with no hitbox; the real strike lands later,
				// after a parry pressed at the fake has closed and inside its whiff recovery.
				// A parry pressed at boss-frame d is live d+1..d+8, whiff recovery to d+24.
				// Early (real 26) baits d in [2,17]; Mid (real 32) d in [8,23]; Late (real 40) d in [16,31].
				FMoveData M = MakeAttack("FeintEarly", EMoveId::BFeintEarly, ESide::Boss, ESym::BFeint, 26, 5, 22, 44, 30.f, 20.f, 250.f, 90.f);
				M.FakeImpactFrame = 12; Put(M);
				M = MakeAttack("FeintMid", EMoveId::BFeintMid, ESide::Boss, ESym::BFeint, 32, 5, 22, 50, 30.f, 20.f, 250.f, 90.f);
				M.FakeImpactFrame = 18; Put(M);
				M = MakeAttack("FeintLate", EMoveId::BFeintLate, ESide::Boss, ESym::BFeint, 40, 5, 22, 58, 32.f, 20.f, 250.f, 90.f);
				M.FakeImpactFrame = 24; Put(M);
			}
			{
				// Killer move: red telegraph, cannot be blocked or parried — only stepped.
				FMoveData M = MakeAttack("KillerThrust", EMoveId::BKillerThrust, ESide::Boss, ESym::BKiller, 30, 5, 30, 0, 55.f, 0.f, 360.f, 70.f);
				M.bUnblockable = true; M.bUnparryable = true; M.HyperArmorFrom = 12; Put(M);
				// Grab: punishes the player standing still waiting to block / parry. Loses to steps and to a hit
				// during its startup (no armor).
				M = MakeAttack("Grab", EMoveId::BGrab, ESide::Boss, ESym::BKiller, 20, 4, 34, 0, 50.f, 0.f, 175.f, 95.f);
				M.Coverage = ECoverage::Grab; M.bUnblockable = true; M.bUnparryable = true; Put(M);
			}

			// ---------------- Boss defence
			{
				FMoveData M;
				M.Name = "Guard"; M.Id = EMoveId::BGuard; M.Kind = EMoveKind::Guard; M.Owner = ESide::Boss; M.Symbol = ESym::BGuard;
				M.Slot = ESlotType::Defend; M.Startup = 3; M.Active = 40; M.Recovery = 6;
				Put(M);
				M.Name = "CounterStance"; M.Id = EMoveId::BCounterStance; M.Kind = EMoveKind::Counter;
				M.Startup = 4; M.Active = 20; M.Recovery = 20;
				Put(M);
			}
			// ---------------- Boss evasion / movement
			Put(MakeStep("Backstep", EMoveId::BBackstep, ESide::Boss, ESym::BEvade, EDir::Back, 20, 8, 2, 12, 280.f, 0.f));
			Put(MakeStep("SideStepL", EMoveId::BSideStepL, ESide::Boss, ESym::BEvade, EDir::Left, 18, 8, 2, 11, 220.f, 0.f));
			Put(MakeStep("SideStepR", EMoveId::BSideStepR, ESide::Boss, ESym::BEvade, EDir::Right, 18, 8, 2, 11, 220.f, 0.f));
			// Approach is freely cancellable (the brain stops walking the frame the slot's move is in range).
			Put(MakeLoco("Approach", EMoveId::BApproach, ESym::BApproach, EDir::Forward, 40, 0, K.BossWalkSpeed * 40.f / FramesPerSecond, 1));
			Put(MakeLoco("DashIn", EMoveId::BDashIn, ESym::BApproach, EDir::Forward, 16, 4, K.BossDashSpeed * 16.f / FramesPerSecond, 0));
			Put(MakeLoco("Retreat", EMoveId::BRetreat, ESym::BRetreat, EDir::Back, 40, 0, K.BossWalkSpeed * 40.f / FramesPerSecond, 0));
			return T;
		}

		std::array<FMoveData, NumMoves>& Table()
		{
			static std::array<FMoveData, NumMoves> Instance = BuildDefaults();
			return Instance;
		}
	}

	const FMoveData& Move(EMoveId Id) { return Table()[static_cast<size_t>(Id)]; }
	FMoveData& MutableMove(EMoveId Id) { return Table()[static_cast<size_t>(Id)]; }
	void ResetMoveTable() { Table() = BuildDefaults(); }

	EMoveId NextLight(int32_t Weapon, int32_t ChainDepth)
	{
		if (Weapon == 1)
		{
			return (ChainDepth % 2 == 0) ? EMoveId::GLight1 : EMoveId::GLight2;
		}
		switch (ChainDepth % 3)
		{
		case 0:  return EMoveId::PLight1;
		case 1:  return EMoveId::PLight2;
		default: return EMoveId::PLight3;
		}
	}

	EMoveId HeavyFor(int32_t Weapon) { return Weapon == 1 ? EMoveId::GHeavy : EMoveId::PHeavy; }

	EMoveId StepFor(EDir Dir)
	{
		switch (Dir)
		{
		case EDir::Forward: return EMoveId::PStepF;
		case EDir::Back:    return EMoveId::PStepB;
		case EDir::Left:    return EMoveId::PStepL;
		case EDir::Right:   return EMoveId::PStepR;
		default:            return EMoveId::None;
		}
	}

	int32_t BossMovesForSlot(ESlotType Slot, EMoveId* OutMoves, int32_t MaxOut)
	{
		int32_t N = 0;
		for (int32_t I = 1; I < NumMoves && N < MaxOut; ++I)
		{
			const FMoveData& M = Table()[static_cast<size_t>(I)];
			if (M.Owner == ESide::Boss && M.Slot == Slot)
			{
				OutMoves[N++] = M.Id;
			}
		}
		return N;
	}
}
