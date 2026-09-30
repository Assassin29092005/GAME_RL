#include "HWOpenWorld.h"

#include "HellwalkerRL.h"
#include "HWBuild.h"
#include "HWDressing.h"
#include "HWGlowMaterial.h"
#include "Async/ParallelFor.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr int32 ChunkCells = 48;
	const TCHAR* TraversableBlockPath = TEXT("/Game/Levels/LevelPrototyping/LevelBlock_Traversable.LevelBlock_Traversable_C");

	const FLinearColor RuinStone(0.11f, 0.105f, 0.1f);
	const FLinearColor RuinDark(0.05f, 0.048f, 0.05f);
}

AHWOpenWorld::AHWOpenWorld()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	// A low, smoky sun over ash hills — the kungfupunk dusk of the arena, scaled to a valley.
	Sun = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sun"));
	Sun->SetupAttachment(Root);
	Sun->SetMobility(EComponentMobility::Movable);
	Sun->SetRelativeRotation(FRotator(-24.f, 40.f, 0.f));
	Sun->Intensity = 7.f;
	Sun->LightColor = FColor(255, 196, 150);
	Sun->bAtmosphereSunLight = true;
	Sun->CastShadows = true;

	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(Root);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->bRealTimeCapture = true;
	SkyLight->SourceType = ESkyLightSourceType::SLS_CapturedScene;
	SkyLight->Intensity = 1.2f;

	Atmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("Atmosphere"));
	Atmosphere->SetupAttachment(Root);

	Fog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("Fog"));
	Fog->SetupAttachment(Root);

	Clouds = CreateDefaultSubobject<UVolumetricCloudComponent>(TEXT("Clouds"));
	Clouds->SetupAttachment(Root);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> CloudF(TEXT("/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst.m_SimpleVolumetricCloud_Inst"));
	if (CloudF.Succeeded()) { Clouds->SetMaterial(CloudF.Object); }
}

FVector AHWOpenWorld::GroundAt(const FVector2D& P) const
{
	return FVector(P.X, P.Y, Generator.IsValid() ? Generator->Height(P.X, P.Y) : 0.0);
}

void AHWOpenWorld::Build(int32 Seed)
{
	const double T0 = FPlatformTime::Seconds();
	Generator = MakeUnique<FHWWorldGen>(Seed);
	BuildTerrain();
	if (!BuildDressing()) { BuildScatter(); }
	BuildRuins();
	BuildSettlement();

	Fog->SetFogDensity(0.006f);
	Fog->SetFogHeightFalloff(0.004f);
	Fog->SetFogInscatteringColor(FLinearColor(0.16f, 0.09f, 0.1f));
	Fog->SetStartDistance(2500.f);
	SkyLight->RecaptureSky();

	BuildSeconds = FPlatformTime::Seconds() - T0;
	UE_LOG(LogHellwalkerRL, Display, TEXT("Open world (seed %d): %d terrain triangles, %d scattered, %d traversable blocks, built in %.2f s."),
		Seed, NumTriangles, NumScatter, NumTraversable, BuildSeconds);
}

void AHWOpenWorld::BuildTerrain()
{
	const FHWWorldGen& G = *Generator;
	const int32 N = FMath::RoundToInt32(2.0 * FHWWorldGen::HalfExtent / FHWWorldGen::Cell); // cells per side
	const int32 V = N + 1;
	auto Coord = [](int32 I) { return -FHWWorldGen::HalfExtent + static_cast<double>(I) * FHWWorldGen::Cell; };

	// One height grid for the whole valley, then normals and colours from it (in parallel: the generator is pure).
	TArray<double> H;
	H.SetNumUninitialized(V * V);
	ParallelFor(V, [&](int32 J) { for (int32 I = 0; I < V; ++I) { H[J * V + I] = G.Height(Coord(I), Coord(J)); } });
	auto HeightAt = [&](int32 I, int32 J) { return H[FMath::Clamp(J, 0, N) * V + FMath::Clamp(I, 0, N)]; };
	// The ground material decides what the vertex colour means: layer weights, or the albedo itself.
	bool bLayeredGround = false;
	UMaterialInterface* Ground = HWTerrainMaterial(&bLayeredGround);
	TArray<FVector> Normals;
	TArray<FLinearColor> Colors;
	Normals.SetNumUninitialized(V * V);
	Colors.SetNumUninitialized(V * V);
	ParallelFor(V, [&](int32 J)
	{
		for (int32 I = 0; I < V; ++I)
		{
			const FVector Nrm = FVector(HeightAt(I - 1, J) - HeightAt(I + 1, J), HeightAt(I, J - 1) - HeightAt(I, J + 1), 2.0 * FHWWorldGen::Cell).GetSafeNormal();
			Normals[J * V + I] = Nrm;
			Colors[J * V + I] = bLayeredGround ? G.GroundLayersFrom(Coord(I), Coord(J), H[J * V + I], Nrm)
				: G.GroundColorFrom(Coord(I), Coord(J), H[J * V + I], Nrm);
		}
	});

	const int32 Chunks1D = N / ChunkCells;
	for (int32 CY = 0; CY < Chunks1D; ++CY)
	{
		for (int32 CX = 0; CX < Chunks1D; ++CX)
		{
			TArray<FVector> Verts;
			TArray<int32> Tris;
			TArray<FVector> Nrm;
			TArray<FVector2D> UV;
			TArray<FLinearColor> Col;
			TArray<FProcMeshTangent> Tan;
			const int32 W = ChunkCells + 1;
			Verts.Reserve(W * W);
			for (int32 J = 0; J < W; ++J)
			{
				for (int32 I = 0; I < W; ++I)
				{
					const int32 GI = CX * ChunkCells + I;
					const int32 GJ = CY * ChunkCells + J;
					Verts.Add(FVector(Coord(GI), Coord(GJ), H[GJ * V + GI]));
					Nrm.Add(Normals[GJ * V + GI]);
					UV.Add(FVector2D(Coord(GI), Coord(GJ)) / 2000.0);
					Col.Add(Colors[GJ * V + GI]);
					// The material projects its textures on world XY: tangent = world X, flattened onto the surface.
					const FVector& Nv = Normals[GJ * V + GI];
					Tan.Add(FProcMeshTangent((FVector::XAxisVector - Nv * Nv.X).GetSafeNormal(), false));
				}
			}
			for (int32 J = 0; J < ChunkCells; ++J)
			{
				for (int32 I = 0; I < ChunkCells; ++I)
				{
					const int32 A = J * W + I;
					const int32 B = A + 1;
					const int32 C = A + W;
					const int32 D = C + 1;
					Tris.Append({ A, C, B, B, C, D });
				}
			}
			UProceduralMeshComponent* PMC = NewObject<UProceduralMeshComponent>(this);
			PMC->bUseAsyncCooking = false; // the explorer spawns on it right away
			PMC->SetupAttachment(Root);
			PMC->RegisterComponent();
			PMC->CreateMeshSection_LinearColor(0, Verts, Tris, Nrm, UV, Col, Tan, true);
			PMC->SetCollisionProfileName(TEXT("BlockAll"));
			if (Ground != nullptr) { PMC->SetMaterial(0, Ground); }
			Chunks.Add(PMC);
			NumTriangles += Tris.Num() / 3;
		}
	}
}

UHierarchicalInstancedStaticMeshComponent* AHWOpenWorld::MakeScatter(UStaticMesh* Mesh, const FLinearColor& Color, bool bCollide, float CullDistance, bool bTint)
{
	UHierarchicalInstancedStaticMeshComponent* C = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
	if (!bTint) { HWBuild::EnsureInstancedUsage(Mesh); } // pack meshes keep their own materials
	C->SetStaticMesh(Mesh);
	C->SetMobility(EComponentMobility::Movable); // attached to a movable root
	C->SetupAttachment(Root);
	C->SetCullDistances(static_cast<int32>(CullDistance * 0.8f), static_cast<int32>(CullDistance));
	if (bCollide) { C->SetCollisionProfileName(TEXT("BlockAll")); }
	else { C->SetCollisionEnabled(ECollisionEnabled::NoCollision); }
	C->RegisterComponent();
	UMaterialInterface* Base = bTint ? HWBuild::ShapeMaterial() : nullptr;
	if (Base != nullptr)
	{
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, this);
		MID->SetVectorParameterValue(TEXT("Color"), Color);
		MID->SetScalarParameterValue(TEXT("Roughness"), 0.9f);
		for (int32 I = 0; I < FMath::Max(1, Mesh->GetStaticMaterials().Num()); ++I) { C->SetMaterial(I, MID); }
	}
	Scatter.Add(C);
	return C;
}

bool AHWOpenWorld::BuildDressing()
{
	const FHWWorldGen& G = *Generator;
	struct FResolved
	{
		const FHWDressSet* Set = nullptr;
		TArray<UStaticMesh*> Meshes;
		TArray<UHierarchicalInstancedStaticMeshComponent*> Instances;
	};
	TMap<FName, FResolved> Sets;
	int32 Found = 0;
	for (const FHWDressSet& S : HWDressing::Sets())
	{
		FResolved& R = Sets.Add(S.Name);
		R.Set = &S;
		for (const FString& Path : S.Paths)
		{
			if (UStaticMesh* M = HWBuild::OptionalMesh(*Path))
			{
				R.Meshes.Add(M);
				R.Instances.Add(MakeScatter(M, FLinearColor::White, S.bCollide, S.CullDistance, false));
			}
		}
		Found += R.Meshes.Num();
	}
	if (Found == 0) { return false; }

	// One instance of a set at (X, Y): a mesh by hash, a size from the range, upright or on the slope.
	auto Place = [&](FName Name, int32 I, int32 J, double X, double Y, int32 Salt) -> bool
	{
		FResolved* R = Sets.Find(Name);
		if (R == nullptr || R->Meshes.Num() == 0) { return false; }
		const int32 K = FMath::Min(static_cast<int32>(G.Hash01(I, J, Salt) * R->Meshes.Num()), R->Meshes.Num() - 1);
		const FVector N = G.Normal(X, Y);
		const FQuat Yaw(FVector::UpVector, G.Hash01(I, J, Salt + 1) * 2.0 * UE_DOUBLE_PI);
		const FQuat Rot = R->Set->bAlignToSlope ? FQuat::FindBetweenNormals(FVector::UpVector, N) * Yaw : Yaw;
		const float Size = FMath::Lerp(R->Set->MinSize, R->Set->MaxSize, static_cast<float>(G.Hash01(I, J, Salt + 2)));
		R->Instances[K]->AddInstance(HWDressing::Fit(*R->Meshes[K], FVector(X, Y, G.Height(X, Y)), Rot, Size, R->Set->bByHeight, R->Set->Sink), true);
		++NumScatter;
		return true;
	};

	const double Limit = FHWWorldGen::HalfExtent * 0.97;
	// Pass 1 (9 m grid): the landscape — groves, dead groves on the ash heights, cliffs on the steeps, rocks in the open.
	{
		constexpr double Step = 900.0;
		const int32 N = FMath::FloorToInt32(2.0 * FHWWorldGen::HalfExtent / Step);
		for (int32 J = 0; J < N; ++J)
		{
			for (int32 I = 0; I < N; ++I)
			{
				const double X = -FHWWorldGen::HalfExtent + (I + G.Hash01(I, J, 2)) * Step;
				const double Y = -FHWWorldGen::HalfExtent + (J + G.Hash01(I, J, 3)) * Step;
				if (FMath::Abs(X) > Limit || FMath::Abs(Y) > Limit) { continue; }
				if (G.PathDistance(X, Y) < 900.0 || G.SiteDistance(X, Y) < 600.0) { continue; }
				const double R0 = G.Hash01(I, J, 1);
				const double Slope = 1.0 - G.Normal(X, Y).Z;
				const double H = G.Height(X, Y);
				const FVector2D Q(X, Y);
				const double Forest = FMath::PerlinNoise2D(Q / 26000.0 + FVector2D(41.7, 9.3));
				const double Dead = FMath::PerlinNoise2D(Q / 19000.0 + FVector2D(3.1, 57.2));
				if (Slope > 0.2)
				{
					if (R0 < 0.15) { Place(TEXT("Cliffs"), I, J, X, Y, 10); }
				}
				else if (Forest > 0.18 && Slope < 0.12)
				{
					if (R0 < 0.5) { Place(TEXT("Firs"), I, J, X, Y, 20); }
				}
				else if ((Dead > 0.2 || H > 6000.0) && R0 < 0.18)
				{
					Place(TEXT("DeadFirs"), I, J, X, Y, 30);
				}
				else if (R0 < 0.07)
				{
					Place(TEXT("Boulders"), I, J, X, Y, 40);
				}
				else if (R0 < 0.12)
				{
					Place(TEXT("Stones"), I, J, X, Y, 50);
				}
			}
		}
	}
	// Pass 2 (6 m grid): the near ground along the paths and around the plazas — grass and ferns in clumps of
	// four (one clump alone reads as nothing from a running camera), stones.
	{
		constexpr double Step = 600.0;
		const int32 N = FMath::FloorToInt32(2.0 * FHWWorldGen::HalfExtent / Step);
		for (int32 J = 0; J < N; ++J)
		{
			for (int32 I = 0; I < N; ++I)
			{
				const double X = -FHWWorldGen::HalfExtent + (I + G.Hash01(I, J, 102)) * Step;
				const double Y = -FHWWorldGen::HalfExtent + (J + G.Hash01(I, J, 103)) * Step;
				if (FMath::Abs(X) > Limit || FMath::Abs(Y) > Limit) { continue; }
				const double Pd = G.PathDistance(X, Y);
				const double Sd = G.SiteDistance(X, Y);
				if (Pd < 450.0 || Sd < 250.0 || (Pd > 7000.0 && Sd > 4500.0)) { continue; }
				const double R0 = G.Hash01(I, J, 101);
				if (1.0 - G.Normal(X, Y).Z > 0.18) { continue; }
				const double Forest = FMath::PerlinNoise2D(FVector2D(X, Y) / 26000.0 + FVector2D(41.7, 9.3));
				const double Density = (Pd < 2500.0 || Sd < 1500.0) ? 0.55 : 0.3;
				if (R0 < Density)
				{
					for (int32 K = 0; K < 4; ++K)
					{
						const double CX = X + (G.Hash01(I, J, 200 + K) - 0.5) * 380.0;
						const double CY = Y + (G.Hash01(I, J, 210 + K) - 0.5) * 380.0;
						if (G.PathDistance(CX, CY) < 400.0 || G.SiteDistance(CX, CY) < 200.0) { continue; }
						Place(Forest > 0.18 && K < 2 ? TEXT("Ferns") : TEXT("Grass"), I, J, CX, CY, 110 + 10 * K);
					}
				}
				else if (R0 < Density + 0.05) { Place(TEXT("Stones"), I, J, X, Y, 150); }
			}
		}
	}
	for (auto& Pair : Sets)
	{
		UE_LOG(LogHellwalkerRL, Display, TEXT("Dressing %s: %d meshes"), *Pair.Key.ToString(), Pair.Value.Meshes.Num());
	}
	return true;
}

void AHWOpenWorld::BuildScatter()
{
	const FHWWorldGen& G = *Generator;
	// Greybox scatter from engine shapes; the Fab environment packs replace these when present.
	UHierarchicalInstancedStaticMeshComponent* Boulders = MakeScatter(HWBuild::Sphere(), FLinearColor(0.06f, 0.055f, 0.05f), true, 60000.f);
	UHierarchicalInstancedStaticMeshComponent* Spires = MakeScatter(HWBuild::Cone(), FLinearColor(0.05f, 0.04f, 0.04f), true, 90000.f);
	UHierarchicalInstancedStaticMeshComponent* RedSpires = MakeScatter(HWBuild::Cone(), FLinearColor(0.12f, 0.03f, 0.025f), true, 90000.f);
	UHierarchicalInstancedStaticMeshComponent* Trunks = MakeScatter(HWBuild::Cylinder(), FLinearColor(0.045f, 0.032f, 0.025f), true, 60000.f);

	constexpr double Step = 900.0;
	const int32 N = FMath::FloorToInt32(2.0 * FHWWorldGen::HalfExtent / Step);
	for (int32 J = 0; J < N; ++J)
	{
		for (int32 I = 0; I < N; ++I)
		{
			const double R0 = G.Hash01(I, J, 1);
			const double X = -FHWWorldGen::HalfExtent + (I + G.Hash01(I, J, 2)) * Step;
			const double Y = -FHWWorldGen::HalfExtent + (J + G.Hash01(I, J, 3)) * Step;
			// Clustered density: noise decides where the stony fields and the dead groves are.
			const double Field = 0.5 + 0.5 * FMath::PerlinNoise2D(FVector2D(X, Y) / 30000.0 + FVector2D(17.3, 4.1));
			if (R0 > 0.05 + 0.22 * Field) { continue; }
			if (!G.IsClear(X, Y, 200.0)) { continue; }
			const double Z = G.Height(X, Y);
			const double K = G.Hash01(I, J, 4);
			const float Yaw = static_cast<float>(G.Hash01(I, J, 5) * 360.0);
			if (K < 0.55)
			{
				const FVector S(1.2 + 2.3 * G.Hash01(I, J, 6), 1.0 + 2.0 * G.Hash01(I, J, 7), 0.7 + 1.3 * G.Hash01(I, J, 8));
				Boulders->AddInstance(FTransform(FRotator(0.f, Yaw, 0.f), FVector(X, Y, Z - S.Z * 15.0), S));
			}
			else if (K < 0.8)
			{
				const double Height = 2.0 + 5.0 * G.Hash01(I, J, 9);
				const double Width = 0.5 + 0.8 * G.Hash01(I, J, 10);
				const FRotator Tilt(static_cast<float>(G.Hash01(I, J, 11) * 16.0 - 8.0), Yaw, 0.f);
				(G.Hash01(I, J, 12) < 0.3 ? RedSpires : Spires)->AddInstance(FTransform(Tilt, FVector(X, Y, Z + Height * 40.0), FVector(Width, Width, Height)));
			}
			else
			{
				const double Height = 3.0 + 4.0 * G.Hash01(I, J, 13);
				const FRotator Tilt(static_cast<float>(G.Hash01(I, J, 14) * 20.0 - 10.0), Yaw, 0.f);
				Trunks->AddInstance(FTransform(Tilt, FVector(X, Y, Z + Height * 45.0), FVector(0.3, 0.3, Height)));
			}
			++NumScatter;
		}
	}
}

void AHWOpenWorld::BuildRuins()
{
	const FHWWorldGen& G = *Generator;
	UClass* BlockClass = LoadClass<AActor>(nullptr, TraversableBlockPath);
	UMaterialInterface* Base = HWBuild::ShapeMaterial();

	// The Monolith jungle-stone set, when present: a decorated floor, mossy columns, crumbled blocks, a great
	// ring arch, rubble, and stone drawn over the traversal blocks. The greybox stays as the (hidden) colliders.
	const FString RuinsDir(TEXT("/Game/ParagonProps/Monolith/Ruins/Meshes/"));
	const FString AgoraDir(TEXT("/Game/ParagonProps/Agora/Props/Meshes/"));
	auto RM = [&RuinsDir](const TCHAR* Name) { return HWBuild::OptionalPackageMesh(*(RuinsDir + Name)); };
	auto AM = [&AgoraDir](const TCHAR* Name) { return HWBuild::OptionalPackageMesh(*(AgoraDir + Name)); };
	UStaticMesh* DecoFloor = RM(TEXT("Ruins_DecoFloor"));
	UStaticMesh* Crumble = RM(TEXT("JunglePillarBlockCrumble01_A"));
	UStaticMesh* Rubble = RM(TEXT("JungleRubblePile_A"));
	UStaticMesh* WallBlock = RM(TEXT("JunglePillarBlock_01A"));
	UStaticMesh* Column = AM(TEXT("Floating_Pillar_B_Single"));
	UStaticMesh* RingArch = AM(TEXT("Jungle_Arch_1"));
	UStaticMesh* Remains[] = { AM(TEXT("RuinRemains1")), AM(TEXT("RuinRemains2")), AM(TEXT("RuinRemains3")), AM(TEXT("RuinRemains4")) };
	UStaticMesh* Lighthouse = HWBuild::OptionalPackageMesh(TEXT("/Game/TheLightHouseOfNoReturn/Meshes/SM_Light_House_01"));
	const bool bDress = DecoFloor != nullptr && Column != nullptr && Crumble != nullptr && WallBlock != nullptr;

	for (const FHWSite& S : G.GetSites())
	{
		if (S.Kind != EHWSiteKind::Ruin) { continue; }
		const FVector C(S.Pos.X, S.Pos.Y, S.Height);
		// A cracked floor and a broken colonnade.
		UStaticMeshComponent* Floor = HWBuild::Part(this, Root, HWBuild::Cylinder(), C + FVector(0.f, 0.f, -40.f), FVector(S.Radius / 50.0, S.Radius / 50.0, 0.8), FRotator::ZeroRotator, RuinDark, true, 0.9f);
		if (bDress)
		{
			Floor->SetVisibility(false);
			const float Yaw = static_cast<float>(G.Hash01(S.Index, 0, 24) * 90.0);
			HWBuild::Fitted(this, Root, DecoFloor, C + FVector(0.f, 0.f, 3.f), Yaw, FVector(S.Radius * 1.45, S.Radius * 1.45, 0.0), false, true);
		}
		for (int32 K = 0; K < 10; ++K)
		{
			const double A = K * 36.0 + G.Hash01(S.Index, K, 20) * 10.0;
			const FVector P = C + FVector(FMath::Cos(FMath::DegreesToRadians(A)), FMath::Sin(FMath::DegreesToRadians(A)), 0.0) * S.Radius * 0.85;
			const double Height = (G.Hash01(S.Index, K, 21) < 0.4) ? 120.0 + 120.0 * G.Hash01(S.Index, K, 22) : 380.0 + 260.0 * G.Hash01(S.Index, K, 23);
			UStaticMeshComponent* Pillar = HWBuild::Part(this, Root, HWBuild::Cylinder(), P + FVector(0.f, 0.f, Height * 0.5), FVector(0.9, 0.9, Height / 100.0), FRotator::ZeroRotator, RuinStone, true, 0.85f);
			if (bDress)
			{
				Pillar->SetVisibility(false);
				const float Yaw = static_cast<float>(G.Hash01(S.Index, K, 25) * 360.0);
				if (Height > 300.0) { HWBuild::Fitted(this, Root, Column, P - FVector(0.f, 0.f, 10.f), Yaw, FVector(100.0, 100.0, Height + 10.0), false); }
				else { HWBuild::Fitted(this, Root, Crumble, P - FVector(0.f, 0.f, 10.f), Yaw, FVector(140.0, 140.0, Height + 10.0), false); }
			}
		}
		if (bDress)
		{
			// Rubble and fallen remains outside the traversal ring, and a great ring arch at the edge.
			for (int32 K = 0; K < 4; ++K)
			{
				const double A = G.Hash01(S.Index, K, 30) * 360.0;
				const double Rad = S.Radius * (0.7 + 0.3 * G.Hash01(S.Index, K, 31));
				const FVector2D Q = FVector2D(S.Pos) + FVector2D(FMath::Cos(FMath::DegreesToRadians(A)), FMath::Sin(FMath::DegreesToRadians(A))) * Rad;
				UStaticMesh* Mesh = K == 0 ? Rubble : Remains[K % UE_ARRAY_COUNT(Remains)];
				HWBuild::Fitted(this, Root, Mesh, FVector(Q.X, Q.Y, G.Height(Q.X, Q.Y) - 15.0), static_cast<float>(A * 3.0), FVector(380.0 + 200.0 * G.Hash01(S.Index, K, 32), 0.0, 0.0), false);
			}
			if (RingArch != nullptr)
			{
				const double A = G.Hash01(S.Index, 0, 33) * 360.0;
				const FVector2D Dir(FMath::Cos(FMath::DegreesToRadians(A)), FMath::Sin(FMath::DegreesToRadians(A)));
				const FVector2D Q = FVector2D(S.Pos) + Dir * S.Radius * 1.15;
				HWBuild::Fitted(this, Root, RingArch, FVector(Q.X, Q.Y, G.Height(Q.X, Q.Y) - 60.0), static_cast<float>(A + 90.0), FVector(0.0, 0.0, 950.0), true);
			}
		}
		// The Western Watch keeps the old lighthouse: a landmark seen from across the valley. It stands on the
		// side of the ruin farthest from the paths.
		if (Lighthouse != nullptr && S.Id == FName(TEXT("Ruin_Watch")))
		{
			FVector2D Best = FVector2D(S.Pos);
			double BestClear = -1.0;
			for (int32 K = 0; K < 8; ++K)
			{
				const double A = K * 45.0;
				const FVector2D Q = FVector2D(S.Pos) + FVector2D(FMath::Cos(FMath::DegreesToRadians(A)), FMath::Sin(FMath::DegreesToRadians(A))) * (S.Radius + 1300.0);
				const double Clear = FMath::Min(G.PathDistance(Q.X, Q.Y), 4000.0) - 0.2 * FMath::Abs(G.Height(Q.X, Q.Y) - S.Height);
				if (Clear > BestClear) { BestClear = Clear; Best = Q; }
			}
			HWBuild::Fitted(this, Root, Lighthouse, FVector(Best.X, Best.Y, G.Height(Best.X, Best.Y) - 250.0), 0.f, FVector(0.0, 0.0, 4400.0), true);
		}

		// Traversal: low walls to hurdle and vault, chest-high walls and platforms to mantle, a wall to climb (the
		// Game Animation Sample's traversal set; its climb is for ledges about 2.5 m up).
		struct FBlock { double X, Y, SX, SY, SZ, Yaw; };
		const FBlock Blocks[] = {
			{ -350.0,  -80.0, 3.0, 0.4, 0.9, 0.0 },    // vault
			{  250.0, -300.0, 3.5, 0.6, 1.5, 30.0 },   // mantle
			{  100.0,  320.0, 2.5, 2.5, 2.0, 15.0 },   // high platform
			{ -420.0,  380.0, 2.0, 2.0, 1.2, 50.0 },   // step up
			{  520.0,  150.0, 3.0, 0.5, 1.0, 80.0 },   // vault
			{ -100.0, -620.0, 3.0, 2.0, 2.2, 0.0 },    // climb (2.5 m is past the sample's reach: it jumps instead)
		};
		for (int32 K = 0; K < UE_ARRAY_COUNT(Blocks); ++K)
		{
			const FBlock& B = Blocks[K];
			const FRotator Rot(0.f, static_cast<float>(B.Yaw + S.Index * 23.0), 0.f);
			const FVector Scale(B.SX, B.SY, B.SZ);
			const FVector Where = C + Rot.RotateVector(FVector(B.X, B.Y, 0.0));
			if (BlockClass != nullptr)
			{
				FActorSpawnParameters P;
				P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				AActor* Block = GetWorld()->SpawnActor<AActor>(BlockClass, FTransform(Rot, Where, Scale), P);
				if (Block == nullptr) { continue; }
				// The block's construction script drops the spawn scale (every block came out a 1 m cube); scale it
				// now. Its ledge splines hang under the mesh, so they scale with it and the traversal reads the
				// real obstacle height and depth.
				Block->SetActorScale3D(Scale);
				// Seat it: whatever the block mesh's pivot, its bottom goes on the ground at Where.
				FVector Origin;
				FVector Extent;
				Block->GetActorBounds(false, Origin, Extent);
				Block->AddActorWorldOffset(FVector(Where.X - Origin.X, Where.Y - Origin.Y, Where.Z - (Origin.Z - Extent.Z)));
				TArray<UStaticMeshComponent*> Meshes;
				Block->GetComponents(Meshes);
				bool bSkinned = false;
				for (UStaticMeshComponent* M : Meshes)
				{
					if (bDress && !bSkinned && M->GetStaticMesh() != nullptr)
					{
						// Stone over the block, fitted to its exact box; the block itself keeps only its collision
						// (and its ledge data, which is what the traversal reads).
						const FBox LB = M->GetStaticMesh()->GetBoundingBox();
						const FTransform T = M->GetComponentTransform();
						const FVector Size = LB.GetSize() * T.GetScale3D();
						const FVector Bottom = T.TransformPosition(FVector(LB.GetCenter().X, LB.GetCenter().Y, LB.Min.Z));
						HWBuild::Fitted(this, Root, WallBlock, Bottom, static_cast<float>(T.Rotator().Yaw), Size, false);
						bSkinned = true;
					}
					if (bDress)
					{
						M->SetVisibility(false);
						continue;
					}
					UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, this);
					MID->SetVectorParameterValue(TEXT("Color"), RuinStone);
					MID->SetScalarParameterValue(TEXT("Roughness"), 0.85f);
					M->SetMaterial(0, MID);
				}
				if (bDress)
				{
					// The size labels too (text renders): only the collision and the ledge data remain.
					TArray<UPrimitiveComponent*> Prims;
					Block->GetComponents(Prims);
					for (UPrimitiveComponent* Prim : Prims) { Prim->SetVisibility(false); }
				}
				RuinActors.Add(Block);
				++NumTraversable;
			}
			else
			{
				HWBuild::Part(this, Root, HWBuild::Cube(), Where + FVector(0.f, 0.f, B.SZ * 50.0), Scale, Rot, RuinStone, true, 0.85f);
			}
		}
	}
}
