// Hellwalker — the open world actor: generated terrain, sky, scatter and ruins (no .umap).
//
// Terrain: the FHWWorldGen heightfield as 10 x 10 procedural-mesh chunks (5 m grid) with collision and a
// vertex-coloured ground material. Sky: sun, atmosphere, volumetric clouds, height fog. Scatter: boulders and
// spires instanced over free ground (Fab-pack meshes when present). Ruins: broken colonnades with the Game
// Animation Sample's traversable blocks, so the explorer can vault and mantle through them.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HWWorldGen.h"
#include "HWOpenWorld.generated.h"

class UProceduralMeshComponent;
class UHierarchicalInstancedStaticMeshComponent;
class UDirectionalLightComponent;
class USkyLightComponent;
class USkyAtmosphereComponent;
class UExponentialHeightFogComponent;
class UVolumetricCloudComponent;
class UStaticMesh;

UCLASS()
class HELLWALKERRL_API AHWOpenWorld : public AActor
{
	GENERATED_BODY()

public:
	AHWOpenWorld();

	/** Generate everything for a seed. Call once, right after spawning. */
	void Build(int32 Seed);

	const FHWWorldGen& Gen() const { return *Generator; }
	/** The GASP traversal blocks, six per ruin in site order (vault, mantle, platform, step, vault, climb). */
	const TArray<TObjectPtr<AActor>>& GetTraversalBlocks() const { return RuinActors; }
	/** The ground under a world XY. */
	FVector GroundAt(const FVector2D& P) const;

	/** Stats for logs / tests. */
	int32 NumTriangles = 0;
	int32 NumScatter = 0;
	int32 NumTraversable = 0;
	double BuildSeconds = 0.0;

private:
	void BuildTerrain();
	void BuildScatter();
	/** The Fab environment packs, when present (HWDressing). False = none found: the greybox scatter runs instead. */
	bool BuildDressing();
	void BuildRuins();
	/** The Crossroads hamlet around the central bell (HWSettlement.cpp; the Desert City kit, when present). */
	void BuildSettlement();
	UHierarchicalInstancedStaticMeshComponent* MakeScatter(UStaticMesh* Mesh, const FLinearColor& Color, bool bCollide, float CullDistance, bool bTint = true);

	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TObjectPtr<UDirectionalLightComponent> Sun;
	UPROPERTY() TObjectPtr<USkyLightComponent> SkyLight;
	UPROPERTY() TObjectPtr<USkyAtmosphereComponent> Atmosphere;
	UPROPERTY() TObjectPtr<UExponentialHeightFogComponent> Fog;
	UPROPERTY() TObjectPtr<UVolumetricCloudComponent> Clouds;
	UPROPERTY() TArray<TObjectPtr<UProceduralMeshComponent>> Chunks;
	UPROPERTY() TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> Scatter;
	UPROPERTY() TArray<TObjectPtr<AActor>> RuinActors;

	TUniquePtr<FHWWorldGen> Generator;
};
