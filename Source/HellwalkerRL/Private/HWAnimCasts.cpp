// Hellwalker — C2 casts, authored against the Fab packs by path (no binary assets of our own).
//
//   Soul     the player: UE5 Manny + the Fighter Animation Pack (9CG, "Sequence2" = the UE5 skeleton).
//            Dodges carry the ghoststep, the block / parry / hit sets carry defence. Twin blades and the
//            glaive are attached to the hands in code.
//   Sevarog  the Warden (default): Paragon Sevarog on its native skeleton — no retarget (PLAN §4 C2).
//            Swing 1-3 in Fast / Medium / Slow cuts cover the fast slashes, sweeps and heavies.
//   Wukong   the Warden, alternative look: Paragon Wukong's staff set (-HWBoss=Wukong).
//   Golem    the Warden, Returned (the final shrine): the Stone Golem pack's body over a UE5 Manny driver that
//            fights with the Fighter pack's strikes (the golem has no attacks of its own); the Game Animation
//            Sample's RTG_UEFN_to_UE4_Mannequin retargets the pose onto its UE4-mannequin skeleton live.
//
// Unset marks are measured from the clip (HWAnimSet.cpp). Which clip reads as which sweep side is a
// MEASURED fact too: -HWAnimSurvey writes AnimSurvey_<cast>.txt (every clip's contact time and the side
// its weapon crosses to at contact), and the choices below come from it. A sweep's clip must cross the
// way its hitbox tracks (SweepLeft = toward the attacker's right, side > 0); a move's clip should reach
// contact near its impact frame so the frame lock plays it at 0.7-1.5x.

#include "HWAnimTypes.h"

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
	FString AssetPath(const TCHAR* Base, const TCHAR* Rel)
	{
		const FString Pkg = FString(Base) + Rel;
		return Pkg + TEXT(".") + FPaths::GetBaseFilename(Pkg);
	}

	FHWClipSpec Clip(const TCHAR* Base, const TCHAR* Rel, EHWDrift Drift = EHWDrift::Linear, bool bLoop = false)
	{
		FHWClipSpec S;
		S.Path = AssetPath(Base, Rel);
		S.Drift = Drift;
		S.bLoop = bLoop;
		return S;
	}

	FHWClipSpec Loop(const TCHAR* Base, const TCHAR* Rel, float NominalSpeed = 0.f)
	{
		FHWClipSpec S = Clip(Base, Rel, EHWDrift::None, true);
		S.NominalSpeed = NominalSpeed;
		return S;
	}

	struct FCastBuilder
	{
		FHWCastSpec C;
		void Role(EHWAnimRole R, const FHWClipSpec& S) { C.Roles[static_cast<int32>(R)] = S; }
		void Move(HW::EMoveId Id, const FHWClipSpec& S) { C.Moves[static_cast<int32>(Id)] = S; }
	};

	FHWCastSpec MakeSoul()
	{
		using namespace HW;
		FCastBuilder B;
		B.C.Name = TEXT("Soul");
		B.C.MeshPath = TEXT("/Game/Fighter_Animations/Demo/Mannequins/Meshes/SKM_Manny.SKM_Manny");
		B.C.Height = 182.f;
		B.C.TipPoints = { TEXT("hand_r"), TEXT("hand_l"), TEXT("foot_r"), TEXT("foot_l") };
		B.C.TrailSocket = TEXT("hand_r");
		B.C.bHandBlades = true;
		B.C.SurveyPath = TEXT("/Game/Fighter_Animations/Animation/Sequence2");
		B.C.TrailSystem = TEXT("/Game/SlashTrail_SoftTofu/Niagara/Ice/NS_SlashTrail_Ice_Loop.NS_SlashTrail_Ice_Loop");
		B.C.TrailColor = FLinearColor(0.45f, 0.65f, 1.f);
		B.C.TrailWidth = 110.f;

		const TCHAR* F = TEXT("/Game/Fighter_Animations/Animation/Sequence2/");
		B.Role(EHWAnimRole::Idle, Loop(F, TEXT("01_Idle/Idle_Combat_Seq")));
		B.Role(EHWAnimRole::MoveF, Loop(F, TEXT("03_Run/01_Run_RM/01_Run/01_Run_F_0_RM/Run_F_0_Loop_RM_Seq")));
		B.Role(EHWAnimRole::MoveB, Loop(F, TEXT("03_Run/01_Run_RM/01_Run/06_Run_B_180_RM/Run_B_180_Loop_RM_Seq")));
		B.Role(EHWAnimRole::MoveL, Loop(F, TEXT("03_Run/01_Run_RM/01_Run/04_Run_F_L_90_RM/Run_F_L_90_Loop_RM_Seq")));
		B.Role(EHWAnimRole::MoveR, Loop(F, TEXT("03_Run/01_Run_RM/01_Run/05_Run_F_R_90_RM/Run_F_R_90_Loop_RM_Seq")));
		B.Role(EHWAnimRole::GuardIdle, Loop(F, TEXT("07_Hit/Block_Idle_Seq")));
		B.Role(EHWAnimRole::GuardMoveF, Loop(F, TEXT("02_Walk/05_Walk_Block_RM/01_Walk_Block_F_0_RM/Walk_Block_F_0_Loop_RM_Seq")));
		B.Role(EHWAnimRole::GuardMoveB, Loop(F, TEXT("02_Walk/05_Walk_Block_RM/06_Walk_Block_B_180_RM/Walk_Block_B_180_Loop_RM_Seq")));
		B.Role(EHWAnimRole::GuardMoveL, Loop(F, TEXT("02_Walk/05_Walk_Block_RM/04_Walk_Block_F_L_90_RM/Walk_Block_F_L_90_Loop_RM_Seq")));
		B.Role(EHWAnimRole::GuardMoveR, Loop(F, TEXT("02_Walk/05_Walk_Block_RM/05_Walk_Block_F_R_90_RM/Walk_Block_F_R_90_Loop_RM_Seq")));
		B.Role(EHWAnimRole::BlockHit, Clip(F, TEXT("07_Hit/Block_Hit_Seq")));
		B.Role(EHWAnimRole::GuardBreak, Clip(F, TEXT("07_Hit/Block_Hit_Break_Seq")));
		B.Role(EHWAnimRole::HitF, Clip(F, TEXT("07_Hit/Hit_F_Seq")));
		B.Role(EHWAnimRole::HitB, Clip(F, TEXT("07_Hit/Hit_B_Seq")));
		B.Role(EHWAnimRole::HitL, Clip(F, TEXT("07_Hit/Hit_L_Seq")));
		B.Role(EHWAnimRole::HitR, Clip(F, TEXT("07_Hit/Hit_R_Seq")));
		B.Role(EHWAnimRole::Stagger, Clip(F, TEXT("07_Hit/Hit_Heavy_Seq")));
		B.Role(EHWAnimRole::Death, Clip(F, TEXT("07_Hit/Hit_Death_Seq"), EHWDrift::None));

		// Twin blades: a 3-hit string, then a committed heavy.
		B.Move(EMoveId::PLight1, Clip(F, TEXT("08_Combo_01/Combo_01-1_Seq")));
		B.Move(EMoveId::PLight2, Clip(F, TEXT("08_Combo_01/Combo_01-2_Seq")));
		B.Move(EMoveId::PLight3, Clip(F, TEXT("08_Combo_01/Combo_01-3_Seq")));
		B.Move(EMoveId::PHeavy, Clip(F, TEXT("13_Attack/Attack_03_Seq")));
		// Glaive (C1): a different string with genuinely different timing.
		B.Move(EMoveId::GLight1, Clip(F, TEXT("10_Combo_03/Combo_03-1_Seq")));
		B.Move(EMoveId::GLight2, Clip(F, TEXT("10_Combo_03/Combo_03-2_Seq")));
		B.Move(EMoveId::GHeavy, Clip(F, TEXT("15_Execution/01_Execution/Execution_02_Seq")));
		B.Move(EMoveId::PParry, Clip(F, TEXT("07_Hit/Parry_R_Seq")));
		// Ghoststep: the root-motion source moves the capsule; the clip is pinned over it.
		B.Move(EMoveId::PStepF, Clip(F, TEXT("05_Dodge/01_Dodge/Dodge_Front_Seq"), EHWDrift::Full));
		B.Move(EMoveId::PStepB, Clip(F, TEXT("05_Dodge/01_Dodge/Dodge_Back_Seq"), EHWDrift::Full));
		B.Move(EMoveId::PStepL, Clip(F, TEXT("05_Dodge/01_Dodge/Dodge_L_Seq"), EHWDrift::Full));
		B.Move(EMoveId::PStepR, Clip(F, TEXT("05_Dodge/01_Dodge/Dodge_R_Seq"), EHWDrift::Full));
		B.Move(EMoveId::PSwitch, Clip(F, TEXT("01_Idle/Idle_to_Idle_Combat_Seq")));
		return B.C;
	}

	FHWCastSpec MakeSevarog()
	{
		using namespace HW;
		FCastBuilder B;
		B.C.Name = TEXT("Sevarog");
		B.C.MeshPath = TEXT("/Game/ParagonSevarog/Characters/Heroes/Sevarog/Meshes/Sevarog.Sevarog");
		B.C.Height = 265.f;
		B.C.TipPoints = { TEXT("FX_Trail_R_02"), TEXT("FX_Trail_R_01"), TEXT("hand_r") };
		B.C.TrailSocket = TEXT("FX_Trail_R_02");
		B.C.SurveyPath = TEXT("/Game/ParagonSevarog/Characters/Heroes/Sevarog/Animations");
		B.C.TrailSystem = TEXT("/Game/SlashTrail_SoftTofu/Niagara/Dark/NS_SlashTrail_Dark_Loop.NS_SlashTrail_Dark_Loop");
		B.C.TrailColor = FLinearColor(0.3f, 1.f, 0.5f);   // Sevarog's soul-green; killer moves turn it violet
		B.C.TrailWidth = 160.f;
		B.C.TrailSockets = { TEXT("FX_Trail_R_01") };
		B.C.TelegraphSockets = { TEXT("hand_r") };

		const TCHAR* S = TEXT("/Game/ParagonSevarog/Characters/Heroes/Sevarog/Animations/");
		B.Role(EHWAnimRole::Idle, Loop(S, TEXT("Idle")));
		B.Role(EHWAnimRole::MoveF, Loop(S, TEXT("Jog_Fwd"), 380.f));
		B.Role(EHWAnimRole::MoveB, Loop(S, TEXT("Jog_Bwd"), 380.f));
		B.Role(EHWAnimRole::MoveL, Loop(S, TEXT("Jog_Left"), 380.f));
		B.Role(EHWAnimRole::MoveR, Loop(S, TEXT("Jog_Right"), 380.f));
		B.Role(EHWAnimRole::BlockHit, Clip(S, TEXT("Hitreact_Front")));
		B.Role(EHWAnimRole::HitF, Clip(S, TEXT("Hitreact_Front")));
		B.Role(EHWAnimRole::HitB, Clip(S, TEXT("Hitreact_Back")));
		B.Role(EHWAnimRole::HitL, Clip(S, TEXT("Hitreact_Left")));
		B.Role(EHWAnimRole::HitR, Clip(S, TEXT("Hitreact_Right")));
		B.Role(EHWAnimRole::Stagger, Clip(S, TEXT("Knock_back")));
		B.Role(EHWAnimRole::StunStart, Clip(S, TEXT("Stun_Start")));
		B.Role(EHWAnimRole::StunLoop, Loop(S, TEXT("Stun_Loop")));
		B.Role(EHWAnimRole::StunEnd, Clip(S, TEXT("Stun_End")));
		B.Role(EHWAnimRole::Death, Clip(S, TEXT("Death_front"), EHWDrift::None));

		// Survey: Swing1 crosses to the attacker's LEFT (side -0.9), Swing2 FAST_120fps / Medium to the RIGHT
		// (+0.6 / +0.8), Swing3 and the Slow cuts are near-vertical (|side| < 0.3).
		B.Move(EMoveId::BFastSlash, Clip(S, TEXT("Swing3_FAST_v2")));                // vertical, contact 0.23
		B.Move(EMoveId::BSweepLeft, Clip(S, TEXT("Swing2_FAST_120fps")));            // +0.59, contact 0.23
		B.Move(EMoveId::BSweepLeftLate, Clip(S, TEXT("Swing2_FAST_120fps")));
		B.Move(EMoveId::BSweepRight, Clip(S, TEXT("Swing1_FAST_v2")));               // -0.87, contact 0.27
		B.Move(EMoveId::BSweepRightLate, Clip(S, TEXT("Swing1_Medium")));            // -0.87
		// Heavies. The delayed heavy is the SAME clip as the cleave — it must read as one until it holds.
		B.Move(EMoveId::BHeavyCleave, Clip(S, TEXT("Swing1_Slow")));                 // vertical, contact 0.45
		B.Move(EMoveId::BDelayedHeavy, Clip(S, TEXT("Swing1_Slow")));
		B.Move(EMoveId::BHeavySweepLeft, Clip(S, TEXT("Swing2_Medium")));            // +0.76
		B.Move(EMoveId::BHeavySweepRight, Clip(S, TEXT("Swing1_120fps")));           // -0.93, contact 0.47
		// Feints imitate the move whose impact their fake lands on: early = the fast slash (12), late = the
		// cleave (24), mid between them.
		B.Move(EMoveId::BFeintEarly, Clip(S, TEXT("Swing3_FAST_v2")));
		B.Move(EMoveId::BFeintMid, Clip(S, TEXT("Swing3_Medium")));
		B.Move(EMoveId::BFeintLate, Clip(S, TEXT("Swing1_Slow")));
		B.Move(EMoveId::BKillerThrust, Clip(S, TEXT("Ultimate_Swing_120fps")));      // contact 0.68 for impact 30
		B.Move(EMoveId::BGrab, Clip(S, TEXT("Soul_Siphon")));
		B.Move(EMoveId::BGuard, Clip(S, TEXT("Soul_Siphon_Targeting_Loop"), EHWDrift::None, true));
		B.Move(EMoveId::BCounterStance, Clip(S, TEXT("Ultimate_Targeting_Loop"), EHWDrift::None, true));
		B.Move(EMoveId::BBackstep, Clip(S, TEXT("Travel_Mode_Bwd_Start"), EHWDrift::Full));
		B.Move(EMoveId::BSideStepL, Clip(S, TEXT("Run_Left"), EHWDrift::Full, true));
		B.Move(EMoveId::BSideStepR, Clip(S, TEXT("Run_Right"), EHWDrift::Full, true));
		return B.C;
	}

	FHWCastSpec MakeWukong()
	{
		using namespace HW;
		FCastBuilder B;
		B.C.Name = TEXT("Wukong");
		B.C.MeshPath = TEXT("/Game/ParagonSunWukong/Characters/Heroes/Wukong/Meshes/Wukong.Wukong");
		B.C.Height = 235.f;
		B.C.TipPoints = { TEXT("FX_Staff_Tip_A"), TEXT("FX_Staff_Tip_B"), TEXT("hand_r") };
		B.C.TrailSocket = TEXT("FX_Staff_Tip_A");
		B.C.SurveyPath = TEXT("/Game/ParagonSunWukong/Characters/Heroes/Wukong/Animations");
		B.C.TrailSystem = TEXT("/Game/SlashTrail_SoftTofu/Niagara/Fire/NS_SlashTrail_Fire_Loop.NS_SlashTrail_Fire_Loop");
		B.C.TrailColor = FLinearColor(1.f, 0.7f, 0.2f);
		B.C.TrailWidth = 140.f;
		B.C.TrailSockets = { TEXT("FX_Staff_Tip_A") };
		B.C.TelegraphSockets = { TEXT("hand_r") };

		const TCHAR* W = TEXT("/Game/ParagonSunWukong/Characters/Heroes/Wukong/Animations/");
		B.Role(EHWAnimRole::Idle, Loop(W, TEXT("Idle")));
		B.Role(EHWAnimRole::MoveF, Loop(W, TEXT("Jog_Fwd"), 400.f));
		B.Role(EHWAnimRole::MoveB, Loop(W, TEXT("Jog_Bwd"), 400.f));
		B.Role(EHWAnimRole::MoveL, Loop(W, TEXT("Jog_Left"), 400.f));
		B.Role(EHWAnimRole::MoveR, Loop(W, TEXT("Jog_Right"), 400.f));
		B.Role(EHWAnimRole::BlockHit, Clip(W, TEXT("HitReact_Front")));
		B.Role(EHWAnimRole::HitF, Clip(W, TEXT("HitReact_Front")));
		B.Role(EHWAnimRole::HitB, Clip(W, TEXT("HitReact_Back")));
		B.Role(EHWAnimRole::HitL, Clip(W, TEXT("HitReact_Left")));
		B.Role(EHWAnimRole::HitR, Clip(W, TEXT("HitReact_Right")));
		B.Role(EHWAnimRole::Stagger, Clip(W, TEXT("Knockback")));
		B.Role(EHWAnimRole::StunStart, Clip(W, TEXT("Stun_Start")));
		B.Role(EHWAnimRole::StunLoop, Loop(W, TEXT("Stun_Loop")));
		B.Role(EHWAnimRole::Death, Clip(W, TEXT("Death"), EHWDrift::None));

		// Survey: Melee A / C cross to the attacker's LEFT (-0.85 / -0.59), B and Air to the RIGHT (+0.67 /
		// +0.73), D and E are near-vertical.
		B.Move(EMoveId::BFastSlash, Clip(W, TEXT("Primary_Melee_E_Slow")));
		B.Move(EMoveId::BSweepLeft, Clip(W, TEXT("Primary_Melee_B_Slow")));
		B.Move(EMoveId::BSweepLeftLate, Clip(W, TEXT("Primary_Melee_B_Slow")));
		B.Move(EMoveId::BSweepRight, Clip(W, TEXT("Primary_Melee_A_Slow")));
		B.Move(EMoveId::BSweepRightLate, Clip(W, TEXT("Primary_Melee_C_Slow")));
		B.Move(EMoveId::BHeavyCleave, Clip(W, TEXT("Primary_Melee_D_Slow")));
		B.Move(EMoveId::BDelayedHeavy, Clip(W, TEXT("Primary_Melee_D_Slow")));
		B.Move(EMoveId::BHeavySweepLeft, Clip(W, TEXT("Primary_Melee_Air")));
		B.Move(EMoveId::BHeavySweepRight, Clip(W, TEXT("Primary_Melee_C_Slow")));
		B.Move(EMoveId::BFeintEarly, Clip(W, TEXT("Primary_Melee_E_Slow")));
		B.Move(EMoveId::BFeintMid, Clip(W, TEXT("Primary_Melee_D_Slow")));
		B.Move(EMoveId::BFeintLate, Clip(W, TEXT("Primary_Melee_D_Slow")));
		B.Move(EMoveId::BKillerThrust, Clip(W, TEXT("Q_Slam")));                     // contact 0.52 for impact 30
		B.Move(EMoveId::BGrab, Clip(W, TEXT("RMB_Push")));
		B.Move(EMoveId::BGuard, Clip(W, TEXT("RMB_Targeting_Loop"), EHWDrift::None, true));
		B.Move(EMoveId::BCounterStance, Clip(W, TEXT("RMB_Evade_Pose_A"), EHWDrift::None, true));
		B.Move(EMoveId::BBackstep, Clip(W, TEXT("Q_Flip_Bwd"), EHWDrift::Full));
		B.Move(EMoveId::BSideStepL, Clip(W, TEXT("Jog_Left"), EHWDrift::Full, true));
		B.Move(EMoveId::BSideStepR, Clip(W, TEXT("Jog_Right"), EHWDrift::Full, true));
		return B.C;
	}

	FHWCastSpec MakeGolem()
	{
		using namespace HW;
		FCastBuilder B;
		B.C.Name = TEXT("Golem");
		B.C.MeshPath = TEXT("/Game/Fighter_Animations/Demo/Mannequins/Meshes/SKM_Manny.SKM_Manny");
		B.C.Height = 285.f;
		B.C.LookMeshPath = TEXT("/Game/Stone_Golem/mesh/SKM_Stone_Golem.SKM_Stone_Golem");
		B.C.LookRetargetTag = TEXT("RTG_UEFN_to_UE4_Mannequin");
		B.C.LookHeight = 285.f;
		B.C.TipPoints = { TEXT("hand_r"), TEXT("hand_l"), TEXT("foot_r"), TEXT("foot_l") };
		B.C.TrailSocket = TEXT("hand_r");
		B.C.SurveyPath = TEXT("/Game/Fighter_Animations/Animation/Sequence2");
		B.C.TrailSystem = TEXT("/Game/SlashTrail_SoftTofu/Niagara/Fire/NS_SlashTrail_Fire_Loop.NS_SlashTrail_Fire_Loop");
		B.C.TrailColor = FLinearColor(1.f, 0.35f, 0.08f);  // the lava in its cracks
		B.C.TrailWidth = 170.f;
		B.C.TrailSockets = { TEXT("hand_r"), TEXT("hand_l") };
		B.C.TelegraphSockets = { TEXT("hand_r"), TEXT("hand_l") }; // it strikes with both fists

		const TCHAR* F = TEXT("/Game/Fighter_Animations/Animation/Sequence2/");
		B.Role(EHWAnimRole::Idle, Loop(F, TEXT("01_Idle/Idle_Combat_Seq")));
		// A walker, not a runner: the combat walk loops, played up to the keeper's speed.
		B.Role(EHWAnimRole::MoveF, Loop(F, TEXT("02_Walk/03_Walk_Combat_RM/01_Walk_Combat_F_0_RM/Walk_Combat_F_0_Loop_RM_Seq")));
		B.Role(EHWAnimRole::MoveB, Loop(F, TEXT("02_Walk/03_Walk_Combat_RM/06_Walk_Combat_B_180_RM/Walk_Combat_B_180_Loop_RM_Seq")));
		B.Role(EHWAnimRole::MoveL, Loop(F, TEXT("02_Walk/03_Walk_Combat_RM/04_Walk_Combat_F_L_90_RM/Walk_Combat_F_L_90_Loop_RM_Seq")));
		B.Role(EHWAnimRole::MoveR, Loop(F, TEXT("02_Walk/03_Walk_Combat_RM/05_Walk_Combat_F_R_90_RM/Walk_Combat_F_R_90_Loop_RM_Seq")));
		B.Role(EHWAnimRole::BlockHit, Clip(F, TEXT("07_Hit/Block_Hit_Seq")));
		B.Role(EHWAnimRole::GuardBreak, Clip(F, TEXT("07_Hit/Block_Hit_Break_Seq")));
		B.Role(EHWAnimRole::HitF, Clip(F, TEXT("07_Hit/Hit_F_Seq")));
		B.Role(EHWAnimRole::HitB, Clip(F, TEXT("07_Hit/Hit_B_Seq")));
		B.Role(EHWAnimRole::HitL, Clip(F, TEXT("07_Hit/Hit_L_Seq")));
		B.Role(EHWAnimRole::HitR, Clip(F, TEXT("07_Hit/Hit_R_Seq")));
		B.Role(EHWAnimRole::Stagger, Clip(F, TEXT("07_Hit/Hit_Heavy_Seq")));
		B.Role(EHWAnimRole::Death, Clip(F, TEXT("07_Hit/Hit_Death_Seq"), EHWDrift::None));

		// From AnimSurvey_Soul.txt (same skeleton): contact time and the side each strike crosses to.
		B.Move(EMoveId::BFastSlash, Clip(F, TEXT("13_Attack/Attack_04_Seq")));                          // +0.03, contact 0.22
		B.Move(EMoveId::BSweepLeft, Clip(F, TEXT("15_Execution/01_Execution/Execution_01_Seq")));       // +0.57, 0.25
		B.Move(EMoveId::BSweepLeftLate, Clip(F, TEXT("15_Execution/01_Execution/Execution_01_Seq")));
		B.Move(EMoveId::BSweepRight, Clip(F, TEXT("13_Attack/Run_Attack_02_Seq")));                    // -0.42, 0.27
		B.Move(EMoveId::BSweepRightLate, Clip(F, TEXT("13_Attack/Attack_02_Seq")));                    // -0.58, 0.42
		B.Move(EMoveId::BHeavyCleave, Clip(F, TEXT("15_Execution/01_Execution/Execution_02_Seq")));     // -0.09, 0.50
		B.Move(EMoveId::BDelayedHeavy, Clip(F, TEXT("15_Execution/01_Execution/Execution_02_Seq")));
		B.Move(EMoveId::BHeavySweepLeft, Clip(F, TEXT("10_Combo_03/Combo_03-2_Seq")));                 // +0.71, 0.22 (slowed: reads heavy)
		B.Move(EMoveId::BHeavySweepRight, Clip(F, TEXT("15_Execution/01_Execution/Execution_03_Seq")));  // -0.81, 0.43
		B.Move(EMoveId::BFeintEarly, Clip(F, TEXT("13_Attack/Attack_04_Seq")));
		B.Move(EMoveId::BFeintMid, Clip(F, TEXT("09_Combo_02/Combo_02-3_Seq")));                        // +0.03, 0.38
		B.Move(EMoveId::BFeintLate, Clip(F, TEXT("15_Execution/01_Execution/Execution_02_Seq")));
		B.Move(EMoveId::BKillerThrust, Clip(F, TEXT("09_Combo_02/Combo_02-4_Seq")));                   // the long wind-up: apex 0.83
		B.Move(EMoveId::BGrab, Clip(F, TEXT("13_Attack/Attack_05_Seq")));
		B.Move(EMoveId::BGuard, Clip(F, TEXT("07_Hit/Block_Idle_Seq"), EHWDrift::None, true));
		B.Move(EMoveId::BCounterStance, Clip(F, TEXT("18_Charge_Attack/Charge_Attack_Loop_Seq"), EHWDrift::None, true));
		B.Move(EMoveId::BBackstep, Clip(F, TEXT("05_Dodge/01_Dodge/Dodge_Back_Seq"), EHWDrift::Full));
		B.Move(EMoveId::BSideStepL, Clip(F, TEXT("02_Walk/03_Walk_Combat_RM/04_Walk_Combat_F_L_90_RM/Walk_Combat_F_L_90_Loop_RM_Seq"), EHWDrift::Full, true));
		B.Move(EMoveId::BSideStepR, Clip(F, TEXT("02_Walk/03_Walk_Combat_RM/05_Walk_Combat_F_R_90_RM/Walk_Combat_F_R_90_Loop_RM_Seq"), EHWDrift::Full, true));
		return B.C;
	}

	const TArray<FHWCastSpec>& Casts()
	{
		static const TArray<FHWCastSpec> All = { MakeSoul(), MakeSevarog(), MakeWukong(), MakeGolem() };
		return All;
	}
}

const FHWCastSpec* HWFindCastSpec(FName Name)
{
	for (const FHWCastSpec& C : Casts())
	{
		if (C.Name == Name) { return &C; }
	}
	return nullptr;
}

TArray<FName> HWCastNames()
{
	TArray<FName> Out;
	for (const FHWCastSpec& C : Casts()) { Out.Add(C.Name); }
	return Out;
}

FName HWCastForSide(HW::ESide Side)
{
	const TCHAR* Cmd = FCommandLine::Get();
	if (FParse::Param(Cmd, TEXT("HWGreybox"))) { return NAME_None; }
	if (Side == HW::ESide::Player) { return TEXT("Soul"); }
	FString Boss;
	if (FParse::Value(Cmd, TEXT("HWBoss="), Boss) && HWFindCastSpec(FName(*Boss)) != nullptr) { return FName(*Boss); }
	return TEXT("Sevarog");
}
