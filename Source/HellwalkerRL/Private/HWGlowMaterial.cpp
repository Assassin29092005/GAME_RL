// HellwalkerRL — the materials the presentation needs that no pack provides, built from code:
//   M_HWGlow            an unlit, additive rim glow with a "Color" parameter, usable on skeletal meshes. It carries the
//                       greybox's colour language onto real characters (killer red, guard blue, exposed yellow, parry
//                       flash) as an overlay, and it is the ghoststep afterimage.
//   M_HWTerrainLayered  the valley's four-layer ground (the Lighthouse pack's textures, weights in the vertex colour).
//   M_HWTerrain         the fallback ground (vertex colour + noise) when the pack is absent.
// The editor builds them on demand. A packaged game cannot compile shaders, so Tools\MakeMaps.bat also SAVES them as
// generated assets under /Game/HellwalkerRL/Materials (like the maps: written by code, never hand-edited), and at run
// time the saved asset is preferred.

#include "HWGlowMaterial.h"

#include "HellwalkerRL.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

#if WITH_EDITOR
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionFresnel.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionNoise.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionUtils.h"
#include "Engine/Texture2D.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#endif

namespace
{
	/** A generated material saved by HWSaveGeneratedMaterials (null if it was never saved). */
	UMaterialInterface* SavedMaterial(const TCHAR* Name)
	{
		const FString Package = FString(TEXT("/Game/HellwalkerRL/Materials/")) + Name;
		if (!FPackageName::DoesPackageExist(Package)) { return nullptr; }
		return LoadObject<UMaterialInterface>(nullptr, *(Package + TEXT(".") + Name));
	}
}

#if WITH_EDITOR
namespace
{
	UMaterial* BuildGlow(UObject* Outer, EObjectFlags Flags)
	{
		UMaterial* M = NewObject<UMaterial>(Outer, TEXT("M_HWGlow"), Flags);
		M->BlendMode = BLEND_Additive;
		M->SetShadingModel(MSM_Unlit);
		M->SetUsageByFlag(MATUSAGE_SkeletalMesh, true);
		M->SetUsageByFlag(MATUSAGE_Clothing, true); // Wukong's cloth sections carry the overlay too

		// Emissive = Color * (0.12 + Fresnel): a rim that reads on dark armour without flattening it.
		UMaterialExpressionVectorParameter* Color = NewObject<UMaterialExpressionVectorParameter>(M);
		Color->ParameterName = TEXT("Color");
		Color->DefaultValue = FLinearColor::Black;
		UMaterialExpressionFresnel* Rim = NewObject<UMaterialExpressionFresnel>(M);
		Rim->Exponent = 2.5f;
		Rim->BaseReflectFraction = 0.f;
		UMaterialExpressionAdd* Body = NewObject<UMaterialExpressionAdd>(M);
		Body->A.Expression = Rim;
		Body->ConstB = 0.12f;
		UMaterialExpressionMultiply* Out = NewObject<UMaterialExpressionMultiply>(M);
		Out->A.Expression = Color;
		Out->B.Expression = Body;
		for (UMaterialExpression* E : { static_cast<UMaterialExpression*>(Color), static_cast<UMaterialExpression*>(Rim),
			static_cast<UMaterialExpression*>(Body), static_cast<UMaterialExpression*>(Out) })
		{
			M->GetExpressionCollection().AddExpression(E);
		}
		M->GetEditorOnlyData()->EmissiveColor.Expression = Out;
		M->PostEditChange(); // compiles the shaders (cached in the DDC after the first run)
		return M;
	}
}
#endif

UMaterialInterface* HWGlowMaterial()
{
	static UMaterialInterface* Cached = nullptr;
	if (Cached != nullptr) { return Cached; }
	if (UMaterialInterface* Saved = SavedMaterial(TEXT("M_HWGlow")))
	{
		Saved->AddToRoot();
		Cached = Saved;
		return Cached;
	}
#if WITH_EDITOR
	UMaterial* M = BuildGlow(GetTransientPackage(), RF_NoFlags);
	M->AddToRoot();
	Cached = M;
#else
	UE_LOG(LogHellwalkerRL, Warning, TEXT("HWGlowMaterial: M_HWGlow was not saved (Tools\\MakeMaps.bat) — telegraph overlay and afterimages disabled."));
#endif
	return Cached;
}

#if WITH_EDITOR
namespace
{
	UTexture* OptionalTexture(const TCHAR* Package)
	{
		const FString Pkg(Package);
		if (!FPackageName::DoesPackageExist(Pkg)) { return nullptr; }
		return LoadObject<UTexture>(nullptr, *(Pkg + TEXT(".") + FPaths::GetBaseFilename(Pkg)));
	}

	/** The layered ground: returns null when a texture is missing (the caller falls back). */
	UMaterial* BuildLayeredTerrain(UObject* Outer, EObjectFlags Flags)
	{
		struct FLayer { const TCHAR* B; const TCHAR* N; const TCHAR* ORM; float Tile; FLinearColor Tint; int32 Weight; };
		const TCHAR* T = TEXT("/Game/TheLightHouseOfNoReturn/Textures/");
		const FLayer Layers[] = {
			{ TEXT("T_Rock_Surface_01_B"), TEXT("T_Rock_Surface_01_N"), TEXT("T_Rock_Surface_01_ORM"), 600.f, FLinearColor(0.8f, 0.78f, 0.76f), 1 },
			{ TEXT("T_Dirt_With_Plants_01_B"), TEXT("T_Dirt_With_Plants_01_N"), TEXT("T_Dirt_With_Plants_01_ORM"), 450.f, FLinearColor(0.9f, 0.85f, 0.72f), 2 }, // dry scrub
			{ TEXT("T_Dirt_01_With_Stones_B"), TEXT("T_Dirt_01_With_Stones_N"), TEXT("T_Dirt_01_With_Stones_ORM"), 300.f, FLinearColor(0.95f, 0.9f, 0.85f), 3 },
			{ TEXT("T_Dirt_02_B"), TEXT("T_Dirt_02_N"), TEXT("T_Dirt_02_ORM"), 400.f, FLinearColor(0.7f, 0.68f, 0.68f), 4 },     // ash
		};
		UTexture* Tex[4][3] = {};
		for (int32 L = 0; L < 4; ++L)
		{
			Tex[L][0] = OptionalTexture(*(FString(T) + Layers[L].B));
			Tex[L][1] = OptionalTexture(*(FString(T) + Layers[L].N));
			Tex[L][2] = OptionalTexture(*(FString(T) + Layers[L].ORM));
			if (Tex[L][0] == nullptr || Tex[L][1] == nullptr || Tex[L][2] == nullptr) { return nullptr; }
		}

		UMaterial* M = NewObject<UMaterial>(Outer, TEXT("M_HWTerrainLayered"), Flags);
		M->SetShadingModel(MSM_DefaultLit);
		M->TwoSided = true;
		auto Add = [M](UMaterialExpression* E) { M->GetExpressionCollection().AddExpression(E); return E; };
		auto Mul = [&](UMaterialExpression* A, int32 OA, UMaterialExpression* B, int32 OB)
		{
			UMaterialExpressionMultiply* E = NewObject<UMaterialExpressionMultiply>(M);
			E->A.Expression = A; E->A.OutputIndex = OA;
			E->B.Expression = B; E->B.OutputIndex = OB;
			return static_cast<UMaterialExpression*>(Add(E));
		};
		auto Sum = [&](UMaterialExpression* A, UMaterialExpression* B)
		{
			UMaterialExpressionAdd* E = NewObject<UMaterialExpressionAdd>(M);
			E->A.Expression = A;
			E->B.Expression = B;
			return static_cast<UMaterialExpression*>(Add(E));
		};
		auto Const = [&](float V)
		{
			UMaterialExpressionConstant* E = NewObject<UMaterialExpressionConstant>(M);
			E->R = V;
			return static_cast<UMaterialExpression*>(Add(E));
		};

		UMaterialExpressionVertexColor* W = NewObject<UMaterialExpressionVertexColor>(M);
		Add(W);
		UMaterialExpressionWorldPosition* WP = NewObject<UMaterialExpressionWorldPosition>(M);
		Add(WP);
		UMaterialExpressionComponentMask* XY = NewObject<UMaterialExpressionComponentMask>(M);
		XY->Input.Expression = WP;
		XY->R = 1; XY->G = 1; XY->B = 0; XY->A = 0;
		Add(XY);

		UMaterialExpression* Albedo = nullptr;
		UMaterialExpression* Normal = nullptr;
		UMaterialExpression* Rough = nullptr;
		for (int32 L = 0; L < 4; ++L)
		{
			UMaterialExpression* UV = Mul(XY, 0, Const(1.f / Layers[L].Tile), 0);
			UMaterialExpressionTextureSample* S[3];
			for (int32 K = 0; K < 3; ++K)
			{
				S[K] = NewObject<UMaterialExpressionTextureSample>(M);
				S[K]->Texture = Tex[L][K];
				S[K]->SamplerType = MaterialExpressionUtils::GetSamplerTypeForTexture(Tex[L][K]);
				S[K]->SamplerSource = SSM_Wrap_WorldGroupSettings; // shared samplers: 12 textures, no limit trouble
				S[K]->Coordinates.Expression = UV;
				Add(S[K]);
			}
			UMaterialExpressionConstant3Vector* Tint = NewObject<UMaterialExpressionConstant3Vector>(M);
			Tint->Constant = Layers[L].Tint;
			Add(Tint);
			// weight = the vertex colour channel of this layer (outputs 1..4 = R G B A)
			UMaterialExpression* C = Mul(Mul(S[0], 0, Tint, 0), 0, W, Layers[L].Weight);
			UMaterialExpression* Nn = Mul(S[1], 0, W, Layers[L].Weight);
			UMaterialExpression* R = Mul(S[2], 2, W, Layers[L].Weight); // ORM: roughness in G
			Albedo = Albedo == nullptr ? C : Sum(Albedo, C);
			Normal = Normal == nullptr ? Nn : Sum(Normal, Nn);
			Rough = Rough == nullptr ? R : Sum(Rough, R);
		}
		// Large-scale variation so the tiling never reads.
		UMaterialExpressionNoise* Macro = NewObject<UMaterialExpressionNoise>(M);
		Macro->Scale = 0.0008f;
		Macro->Levels = 3;
		Macro->OutputMin = 0.7f;
		Macro->OutputMax = 1.2f;
		Macro->Quality = 1;
		Add(Macro);
		M->GetEditorOnlyData()->BaseColor.Expression = Mul(Albedo, 0, Macro, 0);
		M->GetEditorOnlyData()->Normal.Expression = Normal;
		// Never glossy: bare ground in an ash valley. Roughness = 0.6 + 0.4 * sampled.
		M->GetEditorOnlyData()->Roughness.Expression = Sum(Mul(Rough, 0, Const(0.4f), 0), Const(0.6f));
		M->PostEditChange();
		return M;
	}

	/** The fallback ground: the generator's vertex colour, broken up by noise. */
	UMaterial* BuildPlainTerrain(UObject* Outer, EObjectFlags Flags)
	{
		UMaterial* M = NewObject<UMaterial>(Outer, TEXT("M_HWTerrain"), Flags);
		M->SetShadingModel(MSM_DefaultLit);
		M->TwoSided = true; // procedural chunks: never lose the ground to a winding mistake

		// BaseColor = VertexColor * (coarse noise * fine noise): the generator paints, the noise breaks it up.
		UMaterialExpressionVertexColor* VC = NewObject<UMaterialExpressionVertexColor>(M);
		UMaterialExpressionNoise* Coarse = NewObject<UMaterialExpressionNoise>(M);
		Coarse->Scale = 0.0015f;
		Coarse->Levels = 4;
		Coarse->OutputMin = 0.65f;
		Coarse->OutputMax = 1.25f;
		Coarse->Quality = 1;
		UMaterialExpressionNoise* Fine = NewObject<UMaterialExpressionNoise>(M);
		Fine->Scale = 0.02f;
		Fine->Levels = 2;
		Fine->OutputMin = 0.8f;
		Fine->OutputMax = 1.15f;
		Fine->Quality = 1;
		UMaterialExpressionMultiply* Grain = NewObject<UMaterialExpressionMultiply>(M);
		Grain->A.Expression = Coarse;
		Grain->B.Expression = Fine;
		UMaterialExpressionMultiply* Albedo = NewObject<UMaterialExpressionMultiply>(M);
		Albedo->A.Expression = VC;
		Albedo->B.Expression = Grain;
		UMaterialExpressionConstant* Rough = NewObject<UMaterialExpressionConstant>(M);
		Rough->R = 0.92f;
		for (UMaterialExpression* E : { static_cast<UMaterialExpression*>(VC), static_cast<UMaterialExpression*>(Coarse),
			static_cast<UMaterialExpression*>(Fine), static_cast<UMaterialExpression*>(Grain), static_cast<UMaterialExpression*>(Albedo),
			static_cast<UMaterialExpression*>(Rough) })
		{
			M->GetExpressionCollection().AddExpression(E);
		}
		M->GetEditorOnlyData()->BaseColor.Expression = Albedo;
		M->GetEditorOnlyData()->Roughness.Expression = Rough;
		M->PostEditChange();
		return M;
	}
}
#endif

UMaterialInterface* HWTerrainMaterial(bool* bOutLayered)
{
	static UMaterialInterface* Cached = nullptr;
	static bool bLayered = false;
	if (Cached == nullptr)
	{
		if (UMaterialInterface* Saved = SavedMaterial(TEXT("M_HWTerrainLayered")))
		{
			Cached = Saved;
			bLayered = true;
		}
#if WITH_EDITOR
		else if (UMaterial* Layered = BuildLayeredTerrain(GetTransientPackage(), RF_NoFlags))
		{
			Cached = Layered;
			bLayered = true;
		}
#endif
		else if (UMaterialInterface* SavedPlain = SavedMaterial(TEXT("M_HWTerrain")))
		{
			Cached = SavedPlain;
		}
#if WITH_EDITOR
		else
		{
			Cached = BuildPlainTerrain(GetTransientPackage(), RF_NoFlags);
		}
#endif
		if (Cached != nullptr) { Cached->AddToRoot(); }
	}
	if (bOutLayered != nullptr) { *bOutLayered = bLayered; }
	return Cached;
}

#if WITH_EDITOR
int32 HWSaveGeneratedMaterials()
{
	struct FGen { const TCHAR* Name; UMaterial* (*Build)(UObject*, EObjectFlags); };
	const FGen Gens[] = {
		{ TEXT("M_HWGlow"), &BuildGlow },
		{ TEXT("M_HWTerrainLayered"), &BuildLayeredTerrain },
		{ TEXT("M_HWTerrain"), &BuildPlainTerrain },
	};
	int32 Failed = 0;
	for (const FGen& G : Gens)
	{
		const FString PackageName = FString(TEXT("/Game/HellwalkerRL/Materials/")) + G.Name;
		// Regenerating: the old material (possibly loaded already) steps aside so the new one takes its name.
		UPackage* Package = FPackageName::DoesPackageExist(PackageName) ? LoadPackage(nullptr, *PackageName, LOAD_None) : nullptr;
		if (Package == nullptr) { Package = CreatePackage(*PackageName); }
		if (UObject* Old = StaticFindObject(UMaterial::StaticClass(), Package, G.Name))
		{
			Old->Rename(*MakeUniqueObjectName(GetTransientPackage(), UMaterial::StaticClass()).ToString(), GetTransientPackage(),
				REN_DontCreateRedirectors | REN_NonTransactional);
		}
		UMaterial* M = G.Build(Package, RF_Public | RF_Standalone);
		if (M == nullptr)
		{
			UE_LOG(LogHellwalkerRL, Warning, TEXT("HWMakeMaps: %s not built (a pack texture is missing) — the game will use the fallback."), G.Name);
			continue;
		}
		const FString File = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		const bool bSaved = UPackage::SavePackage(Package, M, *File, Args);
		UE_LOG(LogHellwalkerRL, Display, TEXT("HWMakeMaps: %s -> %s: %s"), *PackageName, *File, bSaved ? TEXT("saved") : TEXT("FAILED"));
		Failed += bSaved ? 0 : 1;
	}
	return Failed;
}
#endif
