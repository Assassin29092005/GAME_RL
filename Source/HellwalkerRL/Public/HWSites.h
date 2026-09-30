// Hellwalker — the open world's interactable sites, built in code.
//
//   AHWBell    a bell pavilion: ring it (E) to rest here — the checkpoint you wake at (PBZ's bells).
//   AHWShrine  a walled duel plaza with a gate: challenge its boss at the seal (E). The duel is the
//              arena game, unchanged, played on this plaza. The final shrine is barred until the others fall.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HWWorldGen.h"
#include "HWSites.generated.h"

class UPointLightComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UNiagaraComponent;

UCLASS()
class HELLWALKERRL_API AHWBell : public AActor
{
	GENERATED_BODY()

public:
	AHWBell();
	virtual void Tick(float DeltaSeconds) override;

	void Setup(const FHWSite& Site);
	void SetLit(bool bLit);
	void Ring();

	int32 BellIndex = -1;
	FString Title;
	/** Where the prompt appears and where you wake. */
	FVector InteractPoint() const { return GetActorLocation(); }
	FVector WakePoint() const;

private:
	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TObjectPtr<USceneComponent> BellPivot;
	UPROPERTY() TObjectPtr<UPointLightComponent> Light;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Flame;
	/** Niagara Examples' fire in the brazier while the bell is lit (the glow is then only the unlit ember). */
	UPROPERTY() TObjectPtr<UNiagaraComponent> Fire;
	float Swing = 0.f;
	float Clock = 0.f;
	bool bIsLit = false;
};

UCLASS()
class HELLWALKERRL_API AHWShrine : public AActor
{
	GENERATED_BODY()

public:
	AHWShrine();

	/** Build the plaza; GateDir points from the centre out through the gate (toward the approach path). */
	void Setup(const FHWSite& Site, const FHWShrineSpec& Spec, const FVector2D& GateDir);
	/** Cleared: the seal goes out and the lanterns turn pale. Sealed: the final shrine's gate is barred. */
	void SetState(bool bCleared, bool bSealed);

	int32 ShrineIndex = -1;
	FHWShrineSpec Spec;
	bool bCleared = false;
	bool bSealed = false;

	static constexpr float WallRadius = 1500.f;

	/** Duel placement: the player starts on the gate side facing in, the boss across the seal. */
	FVector Center() const { return GetActorLocation(); }
	FVector Inward() const;
	float ArenaYaw() const;
	FVector PlayerSpawn() const;
	FVector BossSpawn() const;
	/** The prompt point, just inside the gate, and where you are left after the duel. */
	FVector GatePoint() const;
	FVector ExitPoint() const;

	/** True when the Paragon Monolith pack dressed the plaza (the greybox is then hidden colliders only). */
	bool bDressed = false;

private:
	/** The pack architecture over the greybox; false when the pack is missing. */
	bool Dress(float GateYaw, const TArray<FVector>& LanternPoints);

	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TArray<TObjectPtr<UPointLightComponent>> Lanterns;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> FlameMIDs;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> FlameOrbs;
	/** Real fire on the sconces while the keeper lives; a smoke plume over the shrine marks it from afar. */
	UPROPERTY() TArray<TObjectPtr<UNiagaraComponent>> SconceFires;
	UPROPERTY() TObjectPtr<UNiagaraComponent> Plume;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Seal;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Barrier;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> SealMID;
	FVector2D Gate = FVector2D(1.0, 0.0);
};
