// Hellwalker — the Crossroads: a hamlet of mud-brick houses around the central bell, built from the Science
// Fiction Desert City kit (its adobe houses and domes read as an ash-valley village). Everything is placed from
// the world generator's data: the plaza is flat to 36 m, the roads stay open (no house within reach of a path),
// and the placement is a pure function of the seed like the rest of the valley.
//
//   ring 1 (23-31 m)   houses facing the bell; some smoke from their roofs (Niagara Examples' chimney smoke)
//   ring 2 (10-17 m)   market stalls (fabric on poles, a table, stools, crates) and fire pits (Niagara fire)
//   ring 3 (44-52 m)   great desert rocks framing the hamlet, on the slopes outside the plaza

#include "HWOpenWorld.h"

#include "HellwalkerRL.h"
#include "HWBuild.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

namespace
{
	const TCHAR* Kit = TEXT("/Game/Scifi_desert_city/Meshes/");

	UStaticMesh* KitMesh(const TCHAR* Rel) { return HWBuild::OptionalPackageMesh(*(FString(Kit) + Rel)); }

	FVector2D Dir(double Degrees) { return FVector2D(FMath::Cos(FMath::DegreesToRadians(Degrees)), FMath::Sin(FMath::DegreesToRadians(Degrees))); }

	/** Half the footprint's larger side (cm). */
	double HalfSize(const UStaticMesh& M) { const FVector E = M.GetBoundingBox().GetSize(); return 0.5 * FMath::Max(E.X, E.Y); }
}

void AHWOpenWorld::BuildSettlement()
{
	const FHWWorldGen& G = *Generator;
	const FHWSite* Site = G.FindSite(TEXT("Bell_Crossroads"));
	if (Site == nullptr) { return; }

	TArray<UStaticMesh*> Houses;
	for (const TCHAR* Rel : { TEXT("Houses/SM_house_01"), TEXT("Houses/SM_house_02"), TEXT("Houses/SM_house_03"), TEXT("Houses/SM_house_07"),
		TEXT("Houses/SM_house_08"), TEXT("Houses/SM_house_09"), TEXT("Houses/SM_house_10"), TEXT("Houses/SM_house_11"), TEXT("Houses/SM_house_14"),
		TEXT("Houses/SM_house_18"), TEXT("Houses/SM_house_19"), TEXT("Houses/SM_house_21"), TEXT("Round_buildings/SM_round_building_03"),
		TEXT("Round_buildings/SM_high_round_building_02") })
	{
		if (UStaticMesh* M = KitMesh(Rel)) { Houses.Add(M); }
	}
	if (Houses.Num() == 0) { return; } // the kit is optional

	const FVector C(Site->Pos.X, Site->Pos.Y, Site->Height);
	const double Flat = Site->Radius - 100.0;
	auto Place = [this](UStaticMesh* Mesh, const FVector& At, float Yaw, float Scale, bool bCollide)
	{
		if (Mesh == nullptr) { return static_cast<UStaticMeshComponent*>(nullptr); }
		return HWBuild::Fitted(this, Root, Mesh, At, Yaw, FVector(0.f, 0.f, static_cast<float>(Mesh->GetBoundingBox().GetSize().Z) * Scale), bCollide);
	};
	// The kit's houses have their door on the +Y face (the thumbnails' front; checked from the bell): turn it to the bell.
	auto FacingBell = [](double Angle) { return static_cast<float>(Angle + 90.0); };
	int32 NumHouses = 0;

	// Ring 1: houses.
	UStaticMesh* Chimney = KitMesh(TEXT("House_detail/SM_chimney"));
	UStaticMesh* Crates = KitMesh(TEXT("Crates/SM_crates_group_"));
	UStaticMesh* Props = KitMesh(TEXT("Small_props/SM_prop_group"));
	const int32 Slots = 13;
	for (int32 K = 0; K < Slots; ++K)
	{
		const double Angle = K * 360.0 / Slots + G.Hash01(K, 3, 700) * 8.0;
		UStaticMesh* Mesh = Houses[FMath::Min(static_cast<int32>(G.Hash01(K, 3, 701) * Houses.Num()), Houses.Num() - 1)];
		const double Half = HalfSize(*Mesh);
		const double Radius = FMath::Min(2350.0 + G.Hash01(K, 3, 702) * 500.0, Flat - Half);
		const FVector2D P = FVector2D(C.X, C.Y) + Dir(Angle) * Radius;
		if (G.PathDistance(P.X, P.Y) < Half + 450.0) { continue; } // the roads stay open
		Place(Mesh, FVector(P.X, P.Y, C.Z - 10.0), FacingBell(Angle), 1.f, true);
		++NumHouses;
		const FVector Top(P.X, P.Y, C.Z + Mesh->GetBoundingBox().GetSize().Z);
		if (G.Hash01(K, 3, 703) < 0.45)
		{
			// Smoke from the roof: a chimney where the kit has one, the smoke either way.
			const FVector2D Side = FVector2D(C.X, C.Y) + Dir(Angle) * (Radius + Half * 0.35) + Dir(Angle + 90.0) * (Half * 0.3);
			const double RoofZ = C.Z + Mesh->GetBoundingBox().GetSize().Z * 0.72;
			Place(Chimney, FVector(Side.X, Side.Y, RoofZ), FacingBell(Angle), 1.f, false);
			HWBuild::Effect(this, Root, HWFX::Chimney, FVector(Side.X, Side.Y, RoofZ + 180.0), 2.5f, true);
		}
		// Crates or oddments by the door.
		const FVector2D Door = FVector2D(C.X, C.Y) + Dir(Angle) * (Radius - Half - 150.0) + Dir(Angle + 90.0) * (Half * 0.6);
		Place(G.Hash01(K, 3, 704) < 0.5 ? Crates : Props, FVector(Door.X, Door.Y, C.Z), static_cast<float>(Angle + 90.0), 1.f, true);
		(void)Top;
	}

	// Ring 2: the market and its fires.
	UStaticMesh* Cloth = KitMesh(TEXT("Fabric/SM_fabric_cover_02"));
	UStaticMesh* Pole = KitMesh(TEXT("Fabric/SM_fabric_support"));
	UStaticMesh* Table = KitMesh(TEXT("Table/SM_table"));
	UStaticMesh* Stool = KitMesh(TEXT("Stool/SM_stool"));
	UStaticMesh* Stone = KitMesh(TEXT("Rocks/SM_small_rock_01"));
	for (int32 K = 0; K < 4; ++K)
	{
		const double Angle = 45.0 + 90.0 * K + G.Hash01(K, 4, 710) * 20.0;
		const FVector2D P = FVector2D(C.X, C.Y) + Dir(Angle) * 1450.0;
		if (G.PathDistance(P.X, P.Y) < 650.0) { continue; }
		const float Yaw = static_cast<float>(Angle + 90.0); // the stall runs along the ring
		const FQuat Q(FVector::UpVector, FMath::DegreesToRadians(Yaw));
		if (Cloth != nullptr) { HWBuild::Fitted(this, Root, Cloth, FVector(P.X, P.Y, C.Z + 300.0), Yaw, FVector(450.f, 800.f, 0.f), false); }
		for (int32 S = 0; S < 4; ++S)
		{
			const FVector Corner = Q.RotateVector(FVector((S & 1) ? 210.0 : -210.0, (S & 2) ? 390.0 : -390.0, 0.0));
			if (Pole != nullptr) { HWBuild::Fitted(this, Root, Pole, FVector(P.X, P.Y, C.Z) + Corner, Yaw, FVector(0.f, 0.f, 330.f), false); }
		}
		Place(Table, FVector(P.X, P.Y, C.Z), Yaw, 1.f, true);
		for (int32 S = -1; S <= 1; S += 2)
		{
			const FVector Seat = Q.RotateVector(FVector(110.0 * S, 60.0 * S, 0.0));
			Place(Stool, FVector(P.X, P.Y, C.Z) + Seat, Yaw, 1.f, true);
		}
		const FVector Beside = Q.RotateVector(FVector(0.0, 520.0, 0.0));
		Place(Crates, FVector(P.X, P.Y, C.Z) + Beside, Yaw, 1.f, true);
	}
	for (int32 K = 0; K < 3; ++K)
	{
		const double Angle = 120.0 * K + G.Hash01(K, 5, 720) * 30.0;
		const FVector2D P = FVector2D(C.X, C.Y) + Dir(Angle) * 1000.0;
		if (G.PathDistance(P.X, P.Y) < 350.0) { continue; }
		const FVector Pit(P.X, P.Y, C.Z);
		for (int32 S = 0; S < 7; ++S)
		{
			const FVector2D R = Dir(S * 360.0 / 7.0) * 85.0;
			Place(Stone, Pit + FVector(R.X, R.Y, -8.0), static_cast<float>(S * 51.0), 0.9f, false);
		}
		HWBuild::Fire(this, Root, Pit + FVector(0.0, 0.0, 2.0), 110.f, true, true);
		UPointLightComponent* L = NewObject<UPointLightComponent>(this);
		L->SetupAttachment(Root);
		L->SetMobility(EComponentMobility::Movable);
		L->SetRelativeLocation(Pit + FVector(0.0, 0.0, 120.0));
		L->IntensityUnits = ELightUnits::Candelas;
		L->SetIntensity(40.f);
		L->SetAttenuationRadius(1400.f);
		L->SetLightColor(FColor(255, 140, 60));
		L->SetCastShadows(false);
		L->RegisterComponent();
	}

	// Ring 3: great rocks on the slopes around it.
	UStaticMesh* Rocks[] = { KitMesh(TEXT("Rocks/SM_rock_03")), KitMesh(TEXT("Rocks/SM_rock_05")), KitMesh(TEXT("Rocks/SM_rock_04")) };
	for (int32 K = 0; K < 8; ++K)
	{
		UStaticMesh* Rock = Rocks[K % UE_ARRAY_COUNT(Rocks)];
		if (Rock == nullptr) { continue; }
		const double Angle = K * 45.0 + 20.0 + G.Hash01(K, 6, 730) * 15.0;
		const FVector2D P = FVector2D(C.X, C.Y) + Dir(Angle) * (4400.0 + G.Hash01(K, 6, 731) * 800.0);
		if (G.PathDistance(P.X, P.Y) < HalfSize(*Rock) + 500.0) { continue; }
		Place(Rock, FVector(P.X, P.Y, G.Height(P.X, P.Y) - 150.0), static_cast<float>(G.Hash01(K, 6, 732) * 360.0), 1.f, true);
	}
	UE_LOG(LogHellwalkerRL, Display, TEXT("The Crossroads: %d houses around the bell."), NumHouses);
}
