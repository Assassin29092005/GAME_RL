#include "HWBuild.h"

#include "HWDressing.h"
#if WITH_EDITOR
#include "Materials/Material.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#endif

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "HellwalkerRL.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "StaticMeshResources.h"
#include "GameFramework/Pawn.h"
#include "Components/SkeletalMeshComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "MoverComponent.h"
#include "DefaultMovementSet/InstantMovementEffects/BasicInstantMovementEffects.h"

namespace
{
	template <typename T>
	T* Rooted(const TCHAR* Path)
	{
		T* Object = LoadObject<T>(nullptr, Path);
		if (Object != nullptr) { Object->AddToRoot(); }
		return Object;
	}
}

UStaticMesh* HWBuild::Cube() { static UStaticMesh* M = Rooted<UStaticMesh>(TEXT("/Engine/BasicShapes/Cube.Cube")); return M; }
UStaticMesh* HWBuild::Cylinder() { static UStaticMesh* M = Rooted<UStaticMesh>(TEXT("/Engine/BasicShapes/Cylinder.Cylinder")); return M; }
UStaticMesh* HWBuild::Sphere() { static UStaticMesh* M = Rooted<UStaticMesh>(TEXT("/Engine/BasicShapes/Sphere.Sphere")); return M; }
UStaticMesh* HWBuild::Cone() { static UStaticMesh* M = Rooted<UStaticMesh>(TEXT("/Engine/BasicShapes/Cone.Cone")); return M; }

UMaterialInterface* HWBuild::ShapeMaterial()
{
	static UMaterialInterface* M = Rooted<UMaterialInterface>(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	return M;
}

UStaticMeshComponent* HWBuild::Part(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh, const FVector& Location,
	const FVector& Scale, const FRotator& Rotation, const FLinearColor& Color, bool bCollide, float Roughness)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Owner);
	C->SetStaticMesh(Mesh);
	C->SetMobility(EComponentMobility::Movable);
	C->SetupAttachment(Parent);
	C->SetRelativeLocation(Location);
	C->SetRelativeScale3D(Scale);
	C->SetRelativeRotation(Rotation);
	if (bCollide)
	{
		C->SetCollisionProfileName(TEXT("BlockAll"));
	}
	else
	{
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	C->SetGenerateOverlapEvents(false);
	C->RegisterComponent();
	if (UMaterialInterface* Base = ShapeMaterial())
	{
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, Owner);
		MID->SetVectorParameterValue(TEXT("Color"), Color);
		MID->SetScalarParameterValue(TEXT("Roughness"), Roughness);
		C->SetMaterial(0, MID);
	}
	return C;
}

void HWBuild::SurveyMeshes()
{
	const TArray<FString> Roots = {
		TEXT("/Game/ParagonProps/Monolith/Rocks/Meshes"), TEXT("/Game/ParagonProps/Agora/Rocks/Meshes"), TEXT("/Game/ParagonProps/Agora/Trees/Meshes"),
		TEXT("/Game/ParagonProps/Monolith/Ruins/Meshes"), TEXT("/Game/ParagonProps/Monolith/Dusk/Meshes"), TEXT("/Game/ParagonProps/Monolith/Dawn/Meshes"),
		TEXT("/Game/ParagonProps/Agora/Props/Meshes"), TEXT("/Game/KiteDemo/Environments"), TEXT("/Game/TheLightHouseOfNoReturn/Meshes"),
		TEXT("/Game/Scifi_desert_city/Meshes"), TEXT("/Game/Sword") };
	IAssetRegistry& Registry = FAssetRegistryModule::GetRegistry();
	Registry.ScanPathsSynchronous(Roots, true);
	FString R = TEXT("mesh | size X x Y x Z (cm) | pivot->bottom (cm) | pivot->centre XY (cm) | LOD0 tris | nanite\n");
	for (const FString& Root : Roots)
	{
		TArray<FAssetData> Assets;
		Registry.GetAssetsByPath(FName(*Root), Assets, true);
		Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.PackageName.LexicalLess(B.PackageName); });
		R += FString::Printf(TEXT("\n== %s\n"), *Root);
		for (const FAssetData& A : Assets)
		{
			if (A.AssetClassPath != UStaticMesh::StaticClass()->GetClassPathName()) { continue; }
			UStaticMesh* M = Cast<UStaticMesh>(A.GetAsset());
			if (M == nullptr) { continue; }
			const FBox B = M->GetBoundingBox();
			const FVector S = B.GetSize();
			const int32 Tris = M->GetRenderData() != nullptr && M->GetRenderData()->LODResources.Num() > 0 ? M->GetRenderData()->LODResources[0].GetNumTriangles() : -1;
#if WITH_EDITOR
			const int32 Nanite = M->IsNaniteEnabled() ? 1 : 0;
#else
			const int32 Nanite = -1; // the survey is an editor tool; a cooked mesh does not say
#endif
			R += FString::Printf(TEXT("%-44s %6.0f x %6.0f x %6.0f | %6.0f | %5.0f %5.0f | %7d | %d\n"), *A.AssetName.ToString(), S.X, S.Y, S.Z,
				-B.Min.Z, B.GetCenter().X, B.GetCenter().Y, Tris, Nanite);
		}
	}
	const FString Path = FPaths::ProjectSavedDir() / TEXT("HellwalkerRL") / TEXT("MeshSurvey.txt");
	FFileHelper::SaveStringToFile(R, *Path);
	UE_LOG(LogHellwalkerRL, Display, TEXT("Mesh survey -> %s"), *Path);
}

UNiagaraSystem* HWBuild::OptionalSystem(const TCHAR* Path)
{
	const FString P(Path);
	if (!FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(P))) { return nullptr; }
	return LoadObject<UNiagaraSystem>(nullptr, Path);
}

UNiagaraComponent* HWBuild::Effect(AActor* Owner, USceneComponent* Parent, const TCHAR* SystemPath, const FVector& At, float Scale, bool bActive)
{
	UNiagaraSystem* System = OptionalSystem(SystemPath);
	if (System == nullptr || Owner == nullptr) { return nullptr; }
	UNiagaraComponent* C = NewObject<UNiagaraComponent>(Owner);
	C->SetAsset(System);
	C->SetAutoActivate(false);
	C->SetupAttachment(Parent);
	C->SetRelativeLocation(At);
	C->SetRelativeScale3D(FVector(Scale));
	C->RegisterComponent();
	if (bActive) { C->Activate(true); }
	return C;
}

UNiagaraComponent* HWBuild::Fire(AActor* Owner, USceneComponent* Parent, const FVector& At, float Size, bool bLogs, bool bActive)
{
	UStaticMesh* Fuel = OptionalPackageMesh(bLogs ? TEXT("/Game/NiagaraExamples/Gallery/StaticMesh/SM_FireplaceLogs")
		: TEXT("/Game/NiagaraExamples/Gallery/StaticMesh/SM_TorchFuel"));
	if (Fuel == nullptr || OptionalSystem(HWFX::Fire) == nullptr) { return nullptr; }
	UStaticMeshComponent* Mesh = Fitted(Owner, Parent, Fuel, At, 0.f, FVector(Size, Size, 0.f), false);
	return Mesh != nullptr ? Effect(Owner, Mesh, HWFX::Fire, FVector::ZeroVector, 1.f, bActive) : nullptr;
}

void HWBuild::MeshEffect(const TCHAR* SystemPath, USkeletalMeshComponent* Mesh, const FLinearColor& Color)
{
	UNiagaraSystem* System = OptionalSystem(SystemPath);
	if (System == nullptr || Mesh == nullptr) { return; }
	UNiagaraComponent* C = UNiagaraFunctionLibrary::SpawnSystemAttached(System, Mesh, NAME_None, FVector::ZeroVector, FRotator::ZeroRotator,
		EAttachLocation::SnapToTarget, true, false);
	if (C == nullptr) { return; }
	// The mesh is a user parameter named SkeletalMesh: a skeletal-mesh data interface in some systems, an object
	// binding in others (Niagara Examples' teleports) - set whichever this system declares.
	bool bAsInterface = false;
	TArray<FNiagaraVariable> UserVars;
	C->GetOverrideParameters().GetParameters(UserVars);
	for (const FNiagaraVariable& V : UserVars)
	{
		if (V.GetName().ToString().EndsWith(TEXT("SkeletalMesh")) && V.GetType().IsDataInterface()) { bAsInterface = true; }
	}
	if (bAsInterface) { UNiagaraFunctionLibrary::OverrideSystemUserVariableSkeletalMeshComponent(C, TEXT("User.SkeletalMesh"), Mesh); }
	else { C->SetVariableObject(TEXT("User.SkeletalMesh"), Mesh); }
	C->SetVariableLinearColor(TEXT("User.Color"), Color);
	C->Activate(true);
}

void HWBuild::TeleportPawn(APawn* Pawn, const FVector& To, float Yaw)
{
	if (Pawn == nullptr) { return; }
	if (UMoverComponent* Mover = Pawn->FindComponentByClass<UMoverComponent>())
	{
		TSharedPtr<FTeleportEffect> Teleport = MakeShared<FTeleportEffect>();
		Teleport->TargetLocation = To;
		Teleport->bUseActorRotation = false;
		Teleport->TargetRotation = FRotator(0.f, Yaw, 0.f);
		Mover->QueueInstantMovementEffect(Teleport);
		return;
	}
	Pawn->TeleportTo(To, FRotator(0.f, Yaw, 0.f));
}

void HWBuild::EnsureInstancedUsage(UStaticMesh* Mesh)
{
#if WITH_EDITOR
	if (Mesh == nullptr) { return; }
	for (const FStaticMaterial& Slot : Mesh->GetStaticMaterials())
	{
		UMaterial* Base = Slot.MaterialInterface != nullptr ? Slot.MaterialInterface->GetMaterial() : nullptr;
		if (Base != nullptr && !Base->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes))
		{
			Base->SetUsageByFlag(MATUSAGE_InstancedStaticMeshes, true);
			Base->PostEditChange();
			UE_LOG(LogHellwalkerRL, Log, TEXT("Instanced usage set on %s (for %s)."), *Base->GetName(), *Mesh->GetName());
		}
	}
#else
	(void)Mesh;
#endif
}

#if WITH_EDITOR
int32 HWBuild::SaveInstancedUsageForDressing()
{
	int32 Marked = 0;
	int32 Failed = 0;
	TSet<UMaterial*> Seen;
	for (const FHWDressSet& Set : HWDressing::Sets())
	{
		for (const FString& Path : Set.Paths)
		{
			UStaticMesh* Mesh = OptionalMesh(*Path);
			if (Mesh == nullptr) { continue; }
			for (const FStaticMaterial& Slot : Mesh->GetStaticMaterials())
			{
				UMaterial* Base = Slot.MaterialInterface != nullptr ? Slot.MaterialInterface->GetMaterial() : nullptr;
				if (Base == nullptr || Seen.Contains(Base)) { continue; }
				Seen.Add(Base);
				if (Base->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes)) { continue; }
				Base->SetUsageByFlag(MATUSAGE_InstancedStaticMeshes, true);
				Base->PostEditChange();
				UPackage* Package = Base->GetOutermost();
				const FString File = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
				FSavePackageArgs Args;
				Args.TopLevelFlags = RF_Public | RF_Standalone;
				Args.SaveFlags = SAVE_NoError;
				const bool bSaved = UPackage::SavePackage(Package, nullptr, *File, Args);
				UE_LOG(LogHellwalkerRL, Display, TEXT("HWMakeMaps: instanced usage on %s (for %s): %s"), *Base->GetName(), *Mesh->GetName(),
					bSaved ? TEXT("saved") : TEXT("FAILED"));
				Marked += bSaved ? 1 : 0;
				Failed += bSaved ? 0 : 1;
			}
		}
	}
	UE_LOG(LogHellwalkerRL, Display, TEXT("HWMakeMaps: %d dressing materials checked, %d newly marked for instancing."), Seen.Num(), Marked);
	return Failed;
}
#endif

UStaticMesh* HWBuild::OptionalPackageMesh(const TCHAR* Package)
{
	const FString Pkg(Package);
	return OptionalMesh(*(Pkg + TEXT(".") + FPaths::GetBaseFilename(Pkg)));
}

UStaticMeshComponent* HWBuild::Fitted(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh, const FVector& At, float Yaw,
	const FVector& Size, bool bCollide, bool bTopAnchor)
{
	if (Mesh == nullptr) { return nullptr; }
	const FBox B = Mesh->GetBoundingBox();
	const FVector Extent = B.GetSize();
	FVector S(0.f);
	float Sum = 0.f;
	int32 Given = 0;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (Size[Axis] > 0.f && Extent[Axis] > 1.0)
		{
			S[Axis] = Size[Axis] / Extent[Axis];
			Sum += S[Axis];
			++Given;
		}
	}
	const float Mean = Given > 0 ? Sum / Given : 1.f;
	for (int32 Axis = 0; Axis < 3; ++Axis) { if (S[Axis] <= 0.f) { S[Axis] = Mean; } }
	const FQuat Q(FVector::UpVector, FMath::DegreesToRadians(Yaw));
	const FVector Anchor(B.GetCenter().X, B.GetCenter().Y, bTopAnchor ? B.Max.Z : B.Min.Z);
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Owner);
	C->SetStaticMesh(Mesh);
	C->SetMobility(EComponentMobility::Movable);
	C->SetupAttachment(Parent);
	C->SetRelativeTransform(FTransform(Q, At - Q.RotateVector(Anchor * S), S));
	if (bCollide) { C->SetCollisionProfileName(TEXT("BlockAll")); }
	else { C->SetCollisionEnabled(ECollisionEnabled::NoCollision); }
	C->SetGenerateOverlapEvents(false);
	C->RegisterComponent();
	return C;
}

UStaticMesh* HWBuild::OptionalMesh(const TCHAR* Path)
{
	const FString P(Path);
	if (!FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(P))) { return nullptr; }
	return LoadObject<UStaticMesh>(nullptr, Path);
}
