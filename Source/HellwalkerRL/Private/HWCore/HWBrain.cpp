// HellwalkerRL — engine-free core. Shared brain definitions and the boss scripts.

#include "HWCore/HWBrain.h"

#include <cmath>

namespace HW
{
	const char* BrainModeName(EBrainMode M)
	{
		return M == EBrainMode::Hellwalker ? "Hellwalker" : "Pathbreaker";
	}

	const char* DecisionKindName(EDecisionKind K)
	{
		switch (K)
		{
		case EDecisionKind::Script:          return "Script";
		case EDecisionKind::Substitution:    return "Substitution";
		case EDecisionKind::LuckyDrawPress:  return "LuckyDrawPress";
		case EDecisionKind::LuckyDrawAbort:  return "LuckyDrawAbort";
		case EDecisionKind::PerfectPunish:   return "PerfectPunish";
		case EDecisionKind::Spacing:         return "Spacing";
		case EDecisionKind::CapForcedScript: return "CapForcedScript";
		case EDecisionKind::Policy:          return "Policy";
		default:                             return "?";
		}
	}

	float FDuelGeometry::Distance() const
	{
		const float Dx = BossX - PlayerX;
		const float Dy = BossY - PlayerY;
		return std::sqrt(Dx * Dx + Dy * Dy);
	}

	namespace
	{
		// The Ninefold Warden. Chain slots continue a string at the previous attack's cancel frame.
		const FScriptSlot WardenScript[] = {
			{ ESlotType::Reposition, EMoveId::BApproach,     false },
			{ ESlotType::Attack,     EMoveId::BFastSlash,    false },
			{ ESlotType::Attack,     EMoveId::BFastSlash,    true  },
			{ ESlotType::Attack,     EMoveId::BHeavyCleave,  true  },
			{ ESlotType::Defend,     EMoveId::BGuard,        false },
			{ ESlotType::Attack,     EMoveId::BSweepLeft,    false },
			{ ESlotType::Attack,     EMoveId::BFeintMid,     true  },
			{ ESlotType::Reposition, EMoveId::BBackstep,     false },
			{ ESlotType::Attack,     EMoveId::BKillerThrust, false },
			{ ESlotType::Attack,     EMoveId::BFastSlash,    false },
			{ ESlotType::Attack,     EMoveId::BSweepRight,   true  },
			{ ESlotType::Attack,     EMoveId::BDelayedHeavy, true  },
			{ ESlotType::Defend,     EMoveId::BCounterStance,false },
			{ ESlotType::Attack,     EMoveId::BFastSlash,    true  },
			{ ESlotType::Reposition, EMoveId::BApproach,     false },
			{ ESlotType::Attack,     EMoveId::BGrab,         false },
			{ ESlotType::Attack,     EMoveId::BHeavyCleave,  false },
			{ ESlotType::Attack,     EMoveId::BFastSlash,    true  },
			{ ESlotType::Reposition, EMoveId::BRetreat,      false },
		};

		// The Monkey Sage (the open world's second shrine): quick, evasive, fond of the sweep and the feint —
		// fewer heavies, more repositioning. Same move table, same rules, a different habit of its own.
		const FScriptSlot SageScript[] = {
			{ ESlotType::Reposition, EMoveId::BDashIn,        false },
			{ ESlotType::Attack,     EMoveId::BFastSlash,     false },
			{ ESlotType::Attack,     EMoveId::BSweepRight,    true  },
			{ ESlotType::Reposition, EMoveId::BSideStepL,     false },
			{ ESlotType::Attack,     EMoveId::BFeintEarly,    false },
			{ ESlotType::Attack,     EMoveId::BSweepLeft,     true  },
			{ ESlotType::Defend,     EMoveId::BCounterStance, false },
			{ ESlotType::Attack,     EMoveId::BFastSlash,     false },
			{ ESlotType::Attack,     EMoveId::BFastSlash,     true  },
			{ ESlotType::Attack,     EMoveId::BSweepLeftLate, true  },
			{ ESlotType::Reposition, EMoveId::BBackstep,      false },
			{ ESlotType::Attack,     EMoveId::BKillerThrust,  false },
			{ ESlotType::Reposition, EMoveId::BApproach,      false },
			{ ESlotType::Attack,     EMoveId::BFeintMid,      false },
			{ ESlotType::Attack,     EMoveId::BHeavySweepRight, true },
			{ ESlotType::Defend,     EMoveId::BGuard,         false },
			{ ESlotType::Defend,     EMoveId::BGuard,         false },
			{ ESlotType::Attack,     EMoveId::BGrab,          false },
			{ ESlotType::Attack,     EMoveId::BDelayedHeavy,  false },
		};
	}

	int32_t DefaultWardenScript(const FScriptSlot*& OutSlots)
	{
		OutSlots = WardenScript;
		return static_cast<int32_t>(sizeof(WardenScript) / sizeof(WardenScript[0]));
	}

	int32_t BossScript(int32_t Index, const FScriptSlot*& OutSlots)
	{
		if (Index == 1)
		{
			OutSlots = SageScript;
			return static_cast<int32_t>(sizeof(SageScript) / sizeof(SageScript[0]));
		}
		return DefaultWardenScript(OutSlots);
	}
}
