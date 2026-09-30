// Hellwalker — the arena, built in C++ from engine basic shapes (greybox through B4, PLAN §9.5).
// A dusk courtyard: an octagonal stone floor walled by low parapets, iron pillars with crimson bands,
// lanterns, a bronze seal at the centre, and mountains on the horizon. All lighting is dynamic.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HWArena.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;
class UDirectionalLightComponent;
class USkyLightComponent;
class USkyAtmosphereComponent;
class UExponentialHeightFogComponent;
class UPointLightComponent;

UCLASS()
class HELLWALKERRL_API AHWArena : public AActor
{
	GENERATED_BODY()

public:
	AHWArena();
	virtual void BeginPlay() override;

	static constexpr float WallRadius = 1500.f;

private:
	UStaticMeshComponent* AddPart(const FString& PartName, UStaticMesh* InMesh, const FVector& Location, const FVector& Scale,
		const FRotator& Rotation, const FLinearColor& Color, bool bCollide);

	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TObjectPtr<UDirectionalLightComponent> Sun;
	UPROPERTY() TObjectPtr<USkyLightComponent> SkyLight;
	UPROPERTY() TObjectPtr<USkyAtmosphereComponent> Atmosphere;
	UPROPERTY() TObjectPtr<UExponentialHeightFogComponent> Fog;
	UPROPERTY() TArray<TObjectPtr<UPointLightComponent>> Lanterns;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Parts;
	UPROPERTY() TObjectPtr<UMaterialInterface> BaseMaterial;
	TArray<FLinearColor> PartColors;
};
