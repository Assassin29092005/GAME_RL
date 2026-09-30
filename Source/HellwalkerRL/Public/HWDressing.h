// Hellwalker — dressing the generated valley with the Fab environment packs (all optional, all by path).
//
// A set is a family of interchangeable meshes (firs, boulders, cliffs...) with a size range. Placement never
// trusts a pack's pivot or scale: every instance is fitted from the mesh's own bounds, bottom-centre on the
// ground, uniformly scaled to a size drawn from the set's range — so a Paragon rock and a lighthouse rock
// land the same way.
//
//   Lighthouse pack   firs, broken firs, grass, plants, cliffs, small stones
//   Paragon Agora / Monolith (+ KiteDemo)   Nordic rocks, boulders, cliffs, ferns, field grass

#pragma once

#include "CoreMinimal.h"

class UStaticMesh;

struct FHWDressSet
{
	FName Name;
	TArray<FString> Paths;
	float MinSize = 100.f;       // cm: the largest horizontal extent, or the height when bByHeight
	float MaxSize = 200.f;
	bool bByHeight = false;
	float Sink = 0.f;            // fraction of the fitted height pushed into the ground
	bool bCollide = true;
	float CullDistance = 60000.f;
	bool bAlignToSlope = false;
};

namespace HWDressing
{
	/** Firs, DeadFirs, Boulders, Cliffs, Stones, Grass, Ferns. */
	const TArray<FHWDressSet>& Sets();

	/** An instance transform: the mesh's bounds bottom-centre at Ground, rotated, uniformly sized to Size. */
	FTransform Fit(const UStaticMesh& Mesh, const FVector& Ground, const FQuat& Rotation, float Size, bool bByHeight, float Sink);
}
