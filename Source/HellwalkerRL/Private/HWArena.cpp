#include "HWArena.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

AHWArena::AHWArena()
{
	PrimaryActorTick.bCanEverTick = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeF(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderF(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereF(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeF(TEXT("/Engine/BasicShapes/Cone.Cone"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialF(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	UStaticMesh* Cube = CubeF.Object;
	UStaticMesh* Cylinder = CylinderF.Object;
	UStaticMesh* Sphere = SphereF.Object;
	UStaticMesh* Cone = ConeF.Object;
	BaseMaterial = MaterialF.Object;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	RootComponent = Root;

	const FLinearColor Stone(0.07f, 0.075f, 0.085f);
	const FLinearColor DarkStone(0.035f, 0.035f, 0.045f);
	const FLinearColor Iron(0.05f, 0.05f, 0.055f);
	const FLinearColor Crimson(0.45f, 0.02f, 0.02f);
	const FLinearColor Bronze(0.3f, 0.18f, 0.06f);
	const FLinearColor Mountain(0.05f, 0.06f, 0.09f);

	// Floor: 40 m square, top at z = 0.
	AddPart(TEXT("Floor"), Cube, FVector(0.f, 0.f, -10.f), FVector(40.f, 40.f, 0.2f), FRotator::ZeroRotator, Stone, true);
	// The seal at the centre (bronze ring on a dark disc).
	AddPart(TEXT("SealDisc"), Cylinder, FVector(0.f, 0.f, 0.5f), FVector(9.f, 9.f, 0.01f), FRotator::ZeroRotator, DarkStone, false);
	AddPart(TEXT("SealRing"), Cylinder, FVector(0.f, 0.f, 0.3f), FVector(9.6f, 9.6f, 0.01f), FRotator::ZeroRotator, Bronze, false);

	// Octagonal parapet (it also keeps the duel in the ring).
	const float SideLength = 2.f * WallRadius * FMath::Tan(FMath::DegreesToRadians(22.5f));
	for (int32 I = 0; I < 8; ++I)
	{
		const float A = 45.f * static_cast<float>(I);
		const FVector Dir(FMath::Cos(FMath::DegreesToRadians(A)), FMath::Sin(FMath::DegreesToRadians(A)), 0.f);
		AddPart(FString::Printf(TEXT("Wall%d"), I), Cube, Dir * (WallRadius + 40.f) + FVector(0.f, 0.f, 80.f),
			FVector(SideLength / 100.f + 0.9f, 0.8f, 1.6f), FRotator(0.f, A + 90.f, 0.f), DarkStone, true);

		// Pillars at the corners, with a crimson band and an iron cap.
		const float C = A + 22.5f;
		const FVector Corner = FVector(FMath::Cos(FMath::DegreesToRadians(C)), FMath::Sin(FMath::DegreesToRadians(C)), 0.f)
			* (WallRadius / FMath::Cos(FMath::DegreesToRadians(22.5f)) + 60.f);
		AddPart(FString::Printf(TEXT("Pillar%d"), I), Cylinder, Corner + FVector(0.f, 0.f, 350.f), FVector(1.3f, 1.3f, 7.f), FRotator::ZeroRotator, Iron, true);
		AddPart(FString::Printf(TEXT("Band%d"), I), Cylinder, Corner + FVector(0.f, 0.f, 480.f), FVector(1.42f, 1.42f, 0.35f), FRotator::ZeroRotator, Crimson, false);
		AddPart(FString::Printf(TEXT("Cap%d"), I), Cube, Corner + FVector(0.f, 0.f, 720.f), FVector(1.8f, 1.8f, 0.4f), FRotator(0.f, C, 0.f), Iron, false);
	}

	// Lanterns on posts, between the fighters and the wall.
	for (int32 I = 0; I < 4; ++I)
	{
		const float A = 45.f + 90.f * static_cast<float>(I);
		const FVector P = FVector(FMath::Cos(FMath::DegreesToRadians(A)), FMath::Sin(FMath::DegreesToRadians(A)), 0.f) * 1250.f;
		AddPart(FString::Printf(TEXT("Post%d"), I), Cylinder, P + FVector(0.f, 0.f, 110.f), FVector(0.22f, 0.22f, 2.2f), FRotator::ZeroRotator, Iron, true);
		AddPart(FString::Printf(TEXT("Lantern%d"), I), Sphere, P + FVector(0.f, 0.f, 240.f), FVector(0.6f, 0.6f, 0.75f), FRotator::ZeroRotator,
			FLinearColor(1.f, 0.35f, 0.08f), false);
		UPointLightComponent* L = CreateDefaultSubobject<UPointLightComponent>(*FString::Printf(TEXT("LanternLight%d"), I));
		L->SetupAttachment(Root);
		L->SetMobility(EComponentMobility::Movable);
		L->SetRelativeLocation(P + FVector(0.f, 0.f, 240.f));
		L->IntensityUnits = ELightUnits::Candelas;
		L->Intensity = 60.f;
		L->AttenuationRadius = 1800.f;
		L->LightColor = FColor(255, 130, 60);
		L->bUseInverseSquaredFalloff = true;
		L->CastShadows = false;
		Lanterns.Add(L);
	}

	// Mountains on the horizon.
	for (int32 I = 0; I < 7; ++I)
	{
		const float A = 20.f + 51.f * static_cast<float>(I);
		const float R = 16000.f + 4000.f * static_cast<float>(I % 3);
		const FVector P = FVector(FMath::Cos(FMath::DegreesToRadians(A)), FMath::Sin(FMath::DegreesToRadians(A)), 0.f) * R;
		const float H = 14.f + 6.f * static_cast<float>((I * 5) % 4);
		AddPart(FString::Printf(TEXT("Mountain%d"), I), Cone, P + FVector(0.f, 0.f, H * 50.f - 300.f), FVector(70.f, 70.f, H), FRotator::ZeroRotator, Mountain, false);
	}

	// ---- light: a low crimson dusk.
	Sun = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sun"));
	Sun->SetupAttachment(Root);
	Sun->SetMobility(EComponentMobility::Movable);
	Sun->SetRelativeRotation(FRotator(-14.f, 35.f, 0.f));
	Sun->Intensity = 5.f;
	Sun->LightColor = FColor(255, 170, 120);
	Sun->bAtmosphereSunLight = true;
	Sun->CastShadows = true;

	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(Root);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->bRealTimeCapture = true;
	SkyLight->SourceType = ESkyLightSourceType::SLS_CapturedScene;
	SkyLight->Intensity = 1.1f;

	Atmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("Atmosphere"));
	Atmosphere->SetupAttachment(Root);

	Fog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("Fog"));
	Fog->SetupAttachment(Root);
}

UStaticMeshComponent* AHWArena::AddPart(const FString& PartName, UStaticMesh* InMesh, const FVector& Location, const FVector& Scale,
	const FRotator& Rotation, const FLinearColor& Color, bool bCollide)
{
	UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(*PartName);
	C->SetupAttachment(Root);
	C->SetMobility(EComponentMobility::Movable);
	C->SetStaticMesh(InMesh);
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
	Parts.Add(C);
	PartColors.Add(Color);
	return C;
}

void AHWArena::BeginPlay()
{
	Super::BeginPlay();
	for (int32 I = 0; I < Parts.Num(); ++I)
	{
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		MID->SetVectorParameterValue(TEXT("Color"), PartColors.IsValidIndex(I) ? PartColors[I] : FLinearColor::Gray);
		MID->SetScalarParameterValue(TEXT("Roughness"), 0.8f);
		Parts[I]->SetMaterial(0, MID);
	}
	if (Fog != nullptr)
	{
		Fog->SetFogDensity(0.015f);
		Fog->SetFogHeightFalloff(0.12f);
		Fog->SetFogInscatteringColor(FLinearColor(0.12f, 0.06f, 0.1f));
		Fog->SetStartDistance(1500.f);
	}
	if (SkyLight != nullptr) { SkyLight->RecaptureSky(); }
}
