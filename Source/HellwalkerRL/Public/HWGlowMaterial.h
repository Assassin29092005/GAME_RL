// Hellwalker — the additive rim-glow material (built from code, skeletal-mesh capable). Parameter "Color".

#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;

/** Null outside editor builds. */
UMaterialInterface* HWGlowMaterial();

/**
 * The open world's ground. With the Lighthouse pack present: four world-projected texture layers (rock, grass,
 * path dirt, ash) blended by the vertex colour as layer weights (bOutLayered = true). Without it: the
 * vertex colour is the albedo (the generator's GroundColor), broken up by noise.
 */
UMaterialInterface* HWTerrainMaterial(bool* bOutLayered = nullptr);
