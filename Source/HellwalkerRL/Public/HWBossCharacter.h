// Hellwalker — the boss: "The Ninefold Warden". No AIController, no navmesh (PLAN §5.1): all movement
// is code-driven by UHWBossBrainComponent from the core brain's decisions.

#pragma once

#include "CoreMinimal.h"
#include "HWCharacterBase.h"
#include "HWBossCharacter.generated.h"

class UHWBossBrainComponent;

UCLASS()
class HELLWALKERRL_API AHWBossCharacter : public AHWCharacterBase
{
	GENERATED_BODY()

public:
	AHWBossCharacter();

	UHWBossBrainComponent* GetBrain() const { return Brain; }

protected:
	UPROPERTY(VisibleAnywhere, Category = "Hellwalker") TObjectPtr<UHWBossBrainComponent> Brain;
	UPROPERTY(VisibleAnywhere, Category = "Hellwalker") TObjectPtr<UStaticMeshComponent> Shoulders;
	UPROPERTY(VisibleAnywhere, Category = "Hellwalker") TObjectPtr<UStaticMeshComponent> Haft;
};
