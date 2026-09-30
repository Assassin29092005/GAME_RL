// Hellwalker — UCombatAnimConfig (PLAN §5): integer frame counts, designer-editable, no rebuild.
//
// Defaults are captured from the engine-free core's compiled table, so a fresh asset is the B0-validated
// data. ApplyToCore() pushes an edited asset into the core (both fighters, the simulator-equivalent
// rules) and re-measures the derived payoff matrix. Assign one on the game mode, or leave it empty.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HWCombatAnimConfig.generated.h"

USTRUCT(BlueprintType)
struct FHWMoveFrameData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Move") FName Move;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Frames", meta = (ClampMin = "0")) int32 Startup = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Frames", meta = (ClampMin = "0")) int32 Active = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Frames", meta = (ClampMin = "0")) int32 Recovery = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Frames", meta = (ClampMin = "0")) int32 CancelFrame = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Frames") int32 HyperArmorFrom = -1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Frames") int32 FakeImpactFrame = -1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit") float Damage = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit") float ShaChiDrainOnBlock = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit") float Range = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit") float HalfWidth = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit") float SweepReach = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Displacement") float Distance = 0.f;
};

USTRUCT(BlueprintType)
struct FHWCombatTuningData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parry") int32 ParryWindowFrames = 8;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parry") int32 ParryWhiffRecovery = 16;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parry") float ParryRewardShaChi = 15.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parry") int32 ParryRewardStagger = 20;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stun") int32 Hitstun = 18;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stun") int32 BossHitstun = 22;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stun") int32 Blockstun = 11;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ghoststep") int32 StepIFrameStart = 3;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ghoststep") int32 StepIFrameEnd = 18;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ghoststep") float StepShaChiCost = 12.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShaChi") float PlayerShaChiMax = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShaChi") float BossShaChiMax = 80.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShaChi") float ShaChiRegenPerSec = 8.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Block") float BlockChip = 0.2f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Health") float PlayerHealthMax = 360.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Health") float BossHealthMax = 1100.f;
};

UCLASS(BlueprintType)
class HELLWALKERRL_API UHWCombatAnimConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UHWCombatAnimConfig();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hellwalker")
	FHWCombatTuningData Tuning;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hellwalker")
	TArray<FHWMoveFrameData> Moves;

	/** Fill this asset from the core's current values. */
	void CaptureFromCore();
	/** Push this asset into the core and re-measure the derived payoffs. Returns the number of moves applied. */
	int32 ApplyToCore() const;
};
