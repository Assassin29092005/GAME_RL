// HellwalkerRL — engine-free core. RL layout helpers: names and buckets.

#include "HWCore/HWRLTypes.h"

namespace HW
{
	namespace RL
	{
		FSkillParams SkillParams(float Skill)
		{
			const float S = Skill < 0.f ? 0.f : (Skill > 1.f ? 1.f : Skill);
			const float Ease = 1.f - S;
			auto Round = [](float V) { return static_cast<int32_t>(V + 0.5f); };
			FSkillParams P;
			P.Skill = S;
			P.Perception = PerceptionFrames + Round(static_cast<float>(MaxPerceptionFrames - PerceptionFrames) * Ease);
			P.DecisionGap = DecisionGapFrames + Round(6.f * Ease);
			P.MaxString = S >= 0.6f ? MaxStringAttacks : (S >= 0.25f ? 2 : 1);
			P.GrabCooldown = GrabCooldownFrames + Round(static_cast<float>(GrabCooldownFrames) * Ease);
			P.KillerCooldown = KillerCooldownFrames + Round(static_cast<float>(KillerCooldownFrames) * Ease);
			return P;
		}

		float TargetSwingsPerMin(float BaseTarget, float Skill)
		{
			const float S = Skill < 0.f ? 0.f : (Skill > 1.f ? 1.f : Skill);
			return BaseTarget - 16.f * (1.f - S);
		}

		const char* KeeperName(int32_t Identity)
		{
			switch (Identity)
			{
			case 0:  return "Warden";
			case 1:  return "Sage";
			case 2:  return "Returned";
			default: return "?";
			}
		}

		float KeeperHealthScale(int32_t Identity)
		{
			switch (Identity)
			{
			case 1:  return 0.9f;
			case 2:  return 1.25f;
			default: return 1.f;
			}
		}

		const char* ActionName(int32_t A)
		{
			if (A == ActionWait) { return "Wait"; }
			const EMoveId M = ActionMove(A);
			return M == EMoveId::None ? "?" : Move(M).Name;
		}

		int32_t LeadBucket(int32_t LeadFrames)
		{
			// Buckets of the press lead before the perceived impact: <=-1 | 0-2 | 3-5 | 6-8 | 9-11 | 12-15 | 16-21 | >=22
			if (LeadFrames < 0) { return 2; }
			if (LeadFrames <= 2) { return 3; }
			if (LeadFrames <= 5) { return 4; }
			if (LeadFrames <= 8) { return 5; }
			if (LeadFrames <= 11) { return 6; }
			if (LeadFrames <= 15) { return 7; }
			if (LeadFrames <= 21) { return 8; }
			return 9;
		}

		const char* ObsFeatureName(int32_t I)
		{
			static const char* States[7] = { "Idle", "Acting", "Hitstun", "Blockstun", "Stagger", "GuardBroken", "Dead" };
			static const char* Phases[4] = { "AttackStartup", "AttackActive", "AttackRecovery", "OtherMove" };
			static const char* Outcomes[5] = { "None", "Hit", "Whiff", "Blocked", "Parried" };
			static const char* PlayerMoves[13] = { "PLight1", "PLight2", "PLight3", "PHeavy", "GLight1", "GLight2", "GHeavy",
				"PParry", "PStepF", "PStepB", "PStepL", "PStepR", "PSwitch" };
			static char Buf[64];
			auto Fmt = [](const char* A, const char* B) -> const char*
			{
				int32_t N = 0;
				for (const char* P = A; *P != '\0' && N < 62; ++P) { Buf[N++] = *P; }
				for (const char* P = B; *P != '\0' && N < 63; ++P) { Buf[N++] = *P; }
				Buf[N] = '\0';
				return Buf;
			};
			if (I >= ObsSelfState && I < ObsSelfState + 7) { return Fmt("self.state.", States[I - ObsSelfState]); }
			if (I >= ObsSelfMove && I < ObsSelfMove + NumBossMoves) { return Fmt("self.move.", ActionName(I - ObsSelfMove)); }
			if (I >= ObsSelfPhase && I < ObsSelfPhase + 4) { return Fmt("self.phase.", Phases[I - ObsSelfPhase]); }
			if (I >= ObsSelfLastOutcome && I < ObsSelfLastOutcome + 5) { return Fmt("self.last_outcome.", Outcomes[I - ObsSelfLastOutcome]); }
			if (I >= ObsPlayerState && I < ObsPlayerState + 7) { return Fmt("player.state.", States[I - ObsPlayerState]); }
			if (I >= ObsPlayerMove && I < ObsPlayerMove + 13) { return Fmt("player.move.", PlayerMoves[I - ObsPlayerMove]); }
			if (I >= ObsPlayerPhase && I < ObsPlayerPhase + 4) { return Fmt("player.phase.", Phases[I - ObsPlayerPhase]); }
			if (I >= ObsPlayerWeapon && I < ObsPlayerWeapon + 2) { return I == ObsPlayerWeapon ? "player.weapon.TwinBlades" : "player.weapon.Glaive"; }
			if (I >= ObsPlayerLastOutcome && I < ObsPlayerLastOutcome + 5) { return Fmt("player.last_outcome.", Outcomes[I - ObsPlayerLastOutcome]); }
			if (I >= ObsIdentity && I < ObsIdentity + NumKeepers) { return Fmt("keeper.identity.", KeeperName(I - ObsIdentity)); }
			switch (I)
			{
			case ObsSelfHealth:            return "self.health";
			case ObsSelfShaChi:            return "self.shachi";
			case ObsSelfProgress:          return "self.progress";
			case ObsSelfUntilActionable:   return "self.until_actionable";
			case ObsSelfArmor:             return "self.armor";
			case ObsSelfInvulnerable:      return "self.invulnerable";
			case ObsSelfStun:              return "self.stun";
			case ObsSelfSinceOutcome:      return "self.since_outcome";
			case ObsSelfDefended:          return "self.defended";
			case ObsSelfString:            return "self.string";
			case ObsSelfGrabCooldown:      return "self.grab_cooldown";
			case ObsSelfKillerCooldown:    return "self.killer_cooldown";
			case ObsSelfChainWindow:       return "self.chain_window";
			case ObsPlayerDistance:        return "player.distance";
			case ObsPlayerRadialVel:       return "player.radial_vel";
			case ObsPlayerLateralVel:      return "player.lateral_vel";
			case ObsPlayerBearingSin:      return "player.bearing_sin";
			case ObsPlayerBearingCos:      return "player.bearing_cos";
			case ObsPlayerHealth:          return "player.health";
			case ObsPlayerShaChi:          return "player.shachi";
			case ObsPlayerProgress:        return "player.progress";
			case ObsPlayerUntilActionable: return "player.until_actionable";
			case ObsPlayerGuardHeld:       return "player.guard_held";
			case ObsPlayerGuarding:        return "player.guarding";
			case ObsPlayerParryLive:       return "player.parry_live";
			case ObsPlayerInvulnerable:    return "player.invulnerable";
			case ObsPlayerArmor:           return "player.armor";
			case ObsPlayerChain:           return "player.chain";
			case ObsPlayerSinceCommit:     return "player.since_commit";
			case ObsFrameAdvantage:        return "ctx.frame_advantage";
			case ObsFightTime:             return "ctx.fight_time";
			case ObsFightIndex:            return "ctx.fight_index";
			case ObsSwingDeficit:          return "ctx.swing_deficit";
			case ObsSinceOwnSwing:         return "ctx.since_own_swing";
			case ObsSkill:                 return "keeper.skill";
			default:                       return "?";
			}
		}
	}
}
