// Hellwalker — the open world, generated (no .umap, no landscape asset).
//
// A 2.4 km valley walled by mountains: rolling hills and ridges from noise, a flattened plaza at every
// site (bells, shrines, ruins, the spawn), and paths carved between them. Everything is a pure function of
// (seed, x, y), so the terrain mesh, the scatter, the site placement and the tests all agree, and the same
// seed is the same world on every machine.
//
// Units are centimetres, like the rest of Unreal.

#pragma once

#include "CoreMinimal.h"

enum class EHWSiteKind : uint8
{
	Spawn,
	Bell,     // checkpoint: ring it to rest here (PBZ's bells)
	Shrine,   // a boss duel
	Ruin      // traversal playground (vault / mantle / climb blocks)
};

struct FHWSite
{
	EHWSiteKind Kind = EHWSiteKind::Ruin;
	FName Id;
	FString Title;
	FVector2D Pos = FVector2D::ZeroVector;   // cm
	double Radius = 1500.0;                   // flattened plaza radius (cm)
	double Height = 0.0;                      // plaza height (cm), set by the generator
	int32 Index = -1;                         // bell / shrine index within its kind
};

/** A boss shrine: who waits there and how it fights. */
struct FHWShrineSpec
{
	FName Id;
	FString Title;          // "The Ninefold Warden"
	FString Epithet;        // shown under the name
	FName Cast;             // HWAnimCasts: "Sevarog", "Wukong"
	int32 Script = 0;       // HW::BossScript index (0 Warden, 1 Sage)
	float HealthScale = 1.f;
	bool bFinal = false;    // sealed until every other shrine is cleared
};

class HELLWALKERRL_API FHWWorldGen
{
public:
	static constexpr double HalfExtent = 120000.0;  // the world is [-HalfExtent, HalfExtent]^2
	static constexpr double Cell = 500.0;           // terrain grid spacing

	explicit FHWWorldGen(int32 InSeed = 1);

	/** Final ground height at (x, y), sites and paths applied. */
	double Height(double X, double Y) const;
	FVector Normal(double X, double Y) const;
	/** Ground albedo (linear) — height, slope, paths and plazas. */
	FLinearColor GroundColor(double X, double Y) const;
	/** Same, from an already-known height and normal (the terrain builder has both). */
	FLinearColor GroundColorFrom(double X, double Y, double H, const FVector& N) const;
	/** Ground layer weights for the textured material: R rock, G grass, B path dirt, A ash (sum 1). */
	FLinearColor GroundLayersFrom(double X, double Y, double H, const FVector& N) const;

	const TArray<FHWSite>& GetSites() const { return Sites; }
	const FHWSite* FindSite(FName Id) const;
	const TArray<FIntPoint>& GetLinks() const { return Links; }
	static const TArray<FHWShrineSpec>& Shrines();

	/** Distance (cm) to the nearest path centreline; optionally the path's height there. */
	double PathDistance(double X, double Y, double* OutPathHeight = nullptr) const;
	/** Distance (cm) past the edge of the nearest site plaza (negative inside). */
	double SiteDistance(double X, double Y) const;
	/** Free ground for scatter: off paths and plazas, walkable, inside the valley. */
	bool IsClear(double X, double Y, double Margin) const;
	/** Deterministic 0..1 hash for scatter decisions. */
	double Hash01(int32 A, int32 B, int32 Salt) const;

	int32 GetSeed() const { return Seed; }

private:
	double LowFrequency(double X, double Y) const;   // hills + ridges (what paths and plazas follow)
	double Raw(double X, double Y) const;            // + detail + the mountain wall
	void Layout();

	int32 Seed = 1;
	FVector2D NoiseOffset = FVector2D::ZeroVector;
	TArray<FHWSite> Sites;
	TArray<FIntPoint> Links;                         // site index pairs joined by a path
};
