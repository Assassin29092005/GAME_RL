#include "HWDressing.h"

#include "Engine/StaticMesh.h"

namespace
{
	FString P(const TCHAR* PackagePath)
	{
		const FString Pkg(PackagePath);
		return Pkg + TEXT(".") + FPaths::GetBaseFilename(Pkg);
	}

	FHWDressSet Set(const TCHAR* Name, std::initializer_list<const TCHAR*> Packages, float MinSize, float MaxSize, bool bByHeight,
		float Sink, bool bCollide, float Cull, bool bAlign)
	{
		FHWDressSet S;
		S.Name = Name;
		for (const TCHAR* Pkg : Packages) { S.Paths.Add(P(Pkg)); }
		S.MinSize = MinSize;
		S.MaxSize = MaxSize;
		S.bByHeight = bByHeight;
		S.Sink = Sink;
		S.bCollide = bCollide;
		S.CullDistance = Cull;
		S.bAlignToSlope = bAlign;
		return S;
	}
}

const TArray<FHWDressSet>& HWDressing::Sets()
{
	static const TArray<FHWDressSet> All = {
		Set(TEXT("Firs"), {
			TEXT("/Game/TheLightHouseOfNoReturn/Meshes/Foliage/SM_Fir_Tree_01"), TEXT("/Game/TheLightHouseOfNoReturn/Meshes/Foliage/SM_Fir_Tree_02"),
			TEXT("/Game/TheLightHouseOfNoReturn/Meshes/Foliage/SM_Fir_Tree_03"), TEXT("/Game/TheLightHouseOfNoReturn/Meshes/Foliage/SM_Fir_Tree_04"),
			TEXT("/Game/TheLightHouseOfNoReturn/Meshes/Foliage/SM_Fir_Tree_05"), TEXT("/Game/TheLightHouseOfNoReturn/Meshes/Foliage/SM_Fir_Tree_06"),
			TEXT("/Game/TheLightHouseOfNoReturn/Meshes/Foliage/SM_Conifer_01") },
			900.f, 2100.f, true, 0.02f, true, 150000.f, false),
		Set(TEXT("DeadFirs"), {
			TEXT("/Game/TheLightHouseOfNoReturn/Meshes/Foliage/SM_Fir_Tree_Broken_01"), TEXT("/Game/TheLightHouseOfNoReturn/Meshes/Foliage/SM_Fir_Tree_Broken_02") },
			500.f, 1200.f, true, 0.02f, true, 120000.f, false),
		Set(TEXT("Boulders"), {
			TEXT("/Game/ParagonProps/Monolith/Rocks/Meshes/SM_RockNordic_01"), TEXT("/Game/ParagonProps/Monolith/Rocks/Meshes/SM_RockNordic_02"),
			TEXT("/Game/ParagonProps/Monolith/Rocks/Meshes/SM_RockNordic_03"), TEXT("/Game/ParagonProps/Monolith/Rocks/Meshes/SM_RockNordic_04"),
			TEXT("/Game/ParagonProps/Monolith/Rocks/Meshes/SM_RockNordic_05"), TEXT("/Game/ParagonProps/Monolith/Rocks/Meshes/SM_RockNordic_06"),
			TEXT("/Game/ParagonProps/Agora/Rocks/Meshes/SM_LargePlainsBoulder002"), TEXT("/Game/ParagonProps/Agora/Rocks/Meshes/SM_Agelsjon_Rock_Boulder_1"),
			TEXT("/Game/KiteDemo/Environments/Rocks/River_Rock_01/SM_River_Rock_01"), TEXT("/Game/KiteDemo/Environments/Rocks/Mountain_RockFace_002/SM_MountainRock") },
			250.f, 900.f, false, 0.22f, true, 90000.f, true),
		Set(TEXT("Cliffs"), {
			TEXT("/Game/TheLightHouseOfNoReturn/Meshes/SM_Cliff_01"), TEXT("/Game/TheLightHouseOfNoReturn/Meshes/SM_Cliff_02"),
			TEXT("/Game/TheLightHouseOfNoReturn/Meshes/SM_Cliff_03"), TEXT("/Game/TheLightHouseOfNoReturn/Meshes/SM_Cliff_04"),
			TEXT("/Game/TheLightHouseOfNoReturn/Meshes/SM_Cliff_05"), TEXT("/Game/KiteDemo/Environments/Cliffs/Cliff01/SM_Cliff01") },
			// Upright and sunk deep: outcrops. (Laid along the slope, cliff plates read as flakes stuck on the hill.)
			1400.f, 3200.f, false, 0.42f, true, 200000.f, false),
		Set(TEXT("Stones"), {
			TEXT("/Game/TheLightHouseOfNoReturn/Meshes/SM_Small_Rock_01"), TEXT("/Game/TheLightHouseOfNoReturn/Meshes/SM_Small_Rock_02"),
			TEXT("/Game/TheLightHouseOfNoReturn/Meshes/SM_Small_Rock_03"), TEXT("/Game/TheLightHouseOfNoReturn/Meshes/SM_Stone_Set_01"),
			TEXT("/Game/TheLightHouseOfNoReturn/Meshes/SM_Stone_Set_02"), TEXT("/Game/ParagonProps/Monolith/Rocks/Meshes/SM_RockNordic_Small_01"),
			TEXT("/Game/ParagonProps/Monolith/Rocks/Meshes/SM_RockNordic_Small_02") },
			60.f, 220.f, false, 0.2f, false, 25000.f, true),
		Set(TEXT("Grass"), {
			TEXT("/Game/TheLightHouseOfNoReturn/Meshes/Foliage/SM_Grass_Set_01"), TEXT("/Game/TheLightHouseOfNoReturn/Meshes/Foliage/SM_Grass_Set_02"),
			TEXT("/Game/TheLightHouseOfNoReturn/Meshes/Foliage/SM_Grass_Set_03"), TEXT("/Game/TheLightHouseOfNoReturn/Meshes/Foliage/SM_Grass_Set_04"),
			TEXT("/Game/TheLightHouseOfNoReturn/Meshes/Foliage/SM_Grass_Set_05"), TEXT("/Game/TheLightHouseOfNoReturn/Meshes/Foliage/SM_Grass_Set_06"),
			TEXT("/Game/KiteDemo/Environments/Foliage/Grass/FieldGrass/SM_FieldGrass_01") },
			90.f, 220.f, false, 0.f, false, 11000.f, true),
		Set(TEXT("Ferns"), {
			TEXT("/Game/KiteDemo/Environments/Foliage/Ferns/SM_Fern_01"), TEXT("/Game/KiteDemo/Environments/Foliage/Ferns/SM_Fern_02"),
			TEXT("/Game/KiteDemo/Environments/Foliage/Ferns/SM_Fern_03"), TEXT("/Game/TheLightHouseOfNoReturn/Meshes/Foliage/SM_Plant_Set_01"),
			TEXT("/Game/TheLightHouseOfNoReturn/Meshes/Foliage/SM_Plant_Set_02") },
			80.f, 200.f, false, 0.f, false, 12000.f, true),
	};
	return All;
}

FTransform HWDressing::Fit(const UStaticMesh& Mesh, const FVector& Ground, const FQuat& Rotation, float Size, bool bByHeight, float Sink)
{
	const FBox B = Mesh.GetBoundingBox();
	const FVector Extent = B.GetSize();
	const double Basis = bByHeight ? Extent.Z : FMath::Max(Extent.X, Extent.Y);
	const double S = Basis > 1.0 ? Size / Basis : 1.0;
	const FVector Center = B.GetCenter();
	// Bottom-centre of the bounds to the ground point; then sink along the (possibly tilted) up axis.
	const FVector LocalPivotToBottomCentre = FVector(-Center.X, -Center.Y, -B.Min.Z) * S;
	const FVector Up = Rotation.GetUpVector();
	const FVector Location = Ground + Rotation.RotateVector(LocalPivotToBottomCentre) - Up * (Sink * Extent.Z * S);
	return FTransform(Rotation, Location, FVector(S));
}
