// HellwalkerRL — engine-free core. RL layout helpers: names and buckets.

#include "HWCore/HWRLTypes.h"

namespace HW
{
	namespace RL
	{
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
			default:                       return "?";
			}
		}
	}
}
