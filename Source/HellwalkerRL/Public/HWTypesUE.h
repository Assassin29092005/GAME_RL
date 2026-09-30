// Hellwalker — Unreal-facing mirrors of the engine-free core's enums (for Blueprint, HUD, delegates).

#pragma once

#include "CoreMinimal.h"
#include "HWCore/HWTypes.h"
#include "HWTypesUE.generated.h"

/** Difficulty tier — one brain, one flag (PLAN B1/B4). */
UENUM(BlueprintType)
enum class EHWTier : uint8
{
	Pathbreaker UMETA(DisplayName = "Pathbreaker (scripted)"),
	Hellwalker  UMETA(DisplayName = "Hellwalker (adaptive)")
};

/** Mirror of HW::EHitOutcome — raised once per swing (PLAN §5.4). */
UENUM(BlueprintType)
enum class EHWOutcome : uint8
{
	None, Hit, Whiff, Blocked, Parried
};

UENUM(BlueprintType)
enum class EHWPlayerAction : uint8
{
	Light, Heavy, Parry, Step, Switch, GuardDown, GuardUp
};

/** Open world difficulty (the research's tiers): 66 Days is Hellwalker with 66 lives and permadeath. */
UENUM(BlueprintType)
enum class EHWPlayMode : uint8
{
	Pathbreaker UMETA(DisplayName = "Pathbreaker"),
	Hellwalker  UMETA(DisplayName = "Hellwalker"),
	SixtySixDays UMETA(DisplayName = "66 Days")
};

UENUM(BlueprintType)
enum class EHWEncounterState : uint8
{
	WaitingToStart, Running, PlayerWon, PlayerLost
};

class AActor;

/** A swing resolved: who swung, what became of it, and the attacker's frame advantage after it. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FHWSwingOutcomeSignature, AActor*, Attacker, EHWOutcome, Outcome, int32, FrameAdvantage);

inline EHWOutcome ToUE(HW::EHitOutcome O) { return static_cast<EHWOutcome>(static_cast<uint8>(O)); }
inline FString ToFString(const char* S) { return FString(UTF8_TO_TCHAR(S)); }
