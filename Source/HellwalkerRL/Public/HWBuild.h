// Hellwalker — runtime building helpers shared by the open world's actors (everything is built in code).

#pragma once

#include "CoreMinimal.h"

class AActor;
class UStaticMesh;
class UStaticMeshComponent;
class USceneComponent;
class UMaterialInterface;

class UNiagaraSystem;
class UNiagaraComponent;
class USkeletalMeshComponent;
class APawn;

namespace HWBuild
{
	/** Engine basic shapes (always present). */
	UStaticMesh* Cube();
	UStaticMesh* Cylinder();
	UStaticMesh* Sphere();
	UStaticMesh* Cone();
	UMaterialInterface* ShapeMaterial();

	/** A tinted, registered static-mesh part (runtime; not a default subobject). */
	UStaticMeshComponent* Part(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh, const FVector& Location,
		const FVector& Scale, const FRotator& Rotation, const FLinearColor& Color, bool bCollide, float Roughness = 0.8f);

	/**
	 * Move a pawn instantly. Mover pawns (the Game Animation Sample's Mover characters) take a queued teleport
	 * effect - their simulation would undo a plain actor teleport; everything else uses TeleportTo.
	 */
	void TeleportPawn(APawn* Pawn, const FVector& To, float Yaw);

	/** A Niagara system by object path if its package exists (the effect packs are optional). */
	UNiagaraSystem* OptionalSystem(const TCHAR* Path);
	/** A looping effect on Parent at At (relative), inactive unless bActive. Null without the pack. */
	UNiagaraComponent* Effect(AActor* Owner, USceneComponent* Parent, const TCHAR* SystemPath, const FVector& At, float Scale, bool bActive = true);
	/**
	 * A fire: Niagara Examples' NS_Fire burns the surface of the mesh it is attached to, so it gets its own fuel -
	 * the pack's fireplace logs (a pit, a brazier) or torch fuel (a sconce) - sized to Size cm across, bottom at At.
	 * Returns the fire (null without the pack); the fuel stays when the fire is out.
	 */
	UNiagaraComponent* Fire(AActor* Owner, USceneComponent* Parent, const FVector& At, float Size, bool bLogs, bool bActive = true);
	/** A one-shot effect that samples a skeletal mesh (teleport-ins, bursts): attached to it, coloured. */
	void MeshEffect(const TCHAR* SystemPath, USkeletalMeshComponent* Mesh, const FLinearColor& Color);

	/** Load a static mesh by object path if its package exists (Fab packs are optional). */
	UStaticMesh* OptionalMesh(const TCHAR* Path);
	/**
	 * Pack meshes drawn as instances need their materials flagged for instancing; a game run does not add a missing
	 * flag (it draws the default material). Editor builds: set it and recompile in memory (never saved).
	 */
	void EnsureInstancedUsage(UStaticMesh* Mesh);
#if WITH_EDITOR
	/**
	 * Tools\MakeMaps.bat: set (and SAVE) the instanced-static-mesh usage on the base materials of every dressing mesh.
	 * EnsureInstancedUsage does it at play time in the editor; a packaged game cannot compile shaders, so the cooked
	 * materials must already carry the flag. Returns the number of material packages that failed to save.
	 */
	int32 SaveInstancedUsageForDressing();
#endif
	/** Same, from a package path ("/Game/.../SM_X": the object is the package's base name). */
	UStaticMesh* OptionalPackageMesh(const TCHAR* Package);

	/**
	 * A pack mesh placed by its own bounds, whatever its pivot: the bounds' bottom-centre (or top-centre, with
	 * bTopAnchor) lands on At, in Parent's space, turned by Yaw. Size sets the bounds' extent per axis (cm, mesh
	 * axes); an axis left at 0 takes the mean scale of the others (so FVector(0, 0, 800) = uniform, 8 m tall).
	 * Keeps the mesh's own materials. Null when Mesh is null.
	 */
	UStaticMeshComponent* Fitted(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh, const FVector& At, float Yaw,
		const FVector& Size, bool bCollide, bool bTopAnchor = false);

	/** -HWMeshSurvey: every static mesh under the environment packs, with bounds and pivot (MeshSurvey.txt). */
	void SurveyMeshes();
}

/** The Niagara Examples systems the world uses (all optional). */
namespace HWFX
{
	inline const TCHAR* Fire = TEXT("/Game/NiagaraExamples/FX_Misc/NS_Fire.NS_Fire");
	inline const TCHAR* Chimney = TEXT("/Game/NiagaraExamples/FX_Smoke/NS_Chimney_Smoke.NS_Chimney_Smoke");
	inline const TCHAR* Plume = TEXT("/Game/NiagaraExamples/FX_Smoke/NS_Smoke_Plume.NS_Smoke_Plume");
	inline const TCHAR* TeleportIn = TEXT("/Game/NiagaraExamples/FX_Player/NS_Player_Teleport_In.NS_Player_Teleport_In");
	inline const TCHAR* MeshBurst = TEXT("/Game/NiagaraExamples/FX_Misc/NS_SkeletalMeshTris_Burst.NS_SkeletalMeshTris_Burst");
}
