// Hellwalker — engine-free core.

#include "HWCore/HWTypes.h"

namespace HW
{
	const char* SymName(ESym S)
	{
		switch (S)
		{
		case ESym::Neutral:   return "Neutral";
		case ESym::Advance:   return "Advance";
		case ESym::Retreat:   return "Retreat";
		case ESym::Light:     return "Light";
		case ESym::Heavy:     return "Heavy";
		case ESym::Block:     return "Block";
		case ESym::Parry:     return "Parry";
		case ESym::StepF:     return "StepF";
		case ESym::StepB:     return "StepB";
		case ESym::StepL:     return "StepL";
		case ESym::StepR:     return "StepR";
		case ESym::Switch:    return "Switch";
		case ESym::BFast:     return "B_Fast";
		case ESym::BHeavy:    return "B_Heavy";
		case ESym::BFeint:    return "B_Feint";
		case ESym::BKiller:   return "B_Killer";
		case ESym::BGuard:    return "B_Guard";
		case ESym::BEvade:    return "B_Evade";
		case ESym::BApproach: return "B_Approach";
		case ESym::BRetreat:  return "B_Retreat";
		default:              return "?";
		}
	}

	const char* OutcomeName(EHitOutcome O)
	{
		switch (O)
		{
		case EHitOutcome::Hit:     return "Hit";
		case EHitOutcome::Whiff:   return "Whiff";
		case EHitOutcome::Blocked: return "Blocked";
		case EHitOutcome::Parried: return "Parried";
		default:                   return "None";
		}
	}

	const char* SlotTypeName(ESlotType T)
	{
		switch (T)
		{
		case ESlotType::Attack:     return "ATTACK";
		case ESlotType::Defend:     return "DEFEND";
		case ESlotType::Reposition: return "REPOSITION";
		default:                    return "?";
		}
	}

	FCombatTuning& Tuning()
	{
		static FCombatTuning Instance;
		return Instance;
	}
}
