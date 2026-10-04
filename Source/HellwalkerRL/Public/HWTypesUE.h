// Hellwalker — Unreal-facing mirrors of the engine-free core's enums (for Blueprint, HUD, delegates).

#pragma once

#include "CoreMinimal.h"
#include "HWCore/HWTypes.h"
#include "HWTypesUE.generated.h"

/** Which brain fights (PLAN B1/B4). Shown to players as the modes: Pathbreaker = "Normal", Hellwalker = "Adaptive AI". */
UENUM(BlueprintType)
enum class EHWTier : uint8
{
	Pathbreaker UMETA(DisplayName = "Normal (scripted)"),
	Hellwalker  UMETA(DisplayName = "Adaptive AI (the RL keeper)")
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

/**
 * The open world's two play modes (the research's arms; the names stay for saves and telemetry): Pathbreaker is shown as
 * "Normal" (every keeper scripted), Hellwalker as "Adaptive AI" (the RL keepers that learn you). SixtySixDays is retired
 * (1.4): kept so old saves load — as Adaptive AI (UHWSaveGame::LoadOrNull).
 */
UENUM(BlueprintType)
enum class EHWPlayMode : uint8
{
	Pathbreaker UMETA(DisplayName = "Normal"),
	Hellwalker  UMETA(DisplayName = "Adaptive AI"),
	SixtySixDays UMETA(Hidden)
};

/** The modes' display names: "Normal" / "Adaptive AI" (a retired 66 Days walk plays, and reads, as Adaptive AI). */
inline FString PlayModeName(EHWPlayMode M) { return M == EHWPlayMode::Pathbreaker ? FString(TEXT("Normal")) : FString(TEXT("Adaptive AI")); }

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
