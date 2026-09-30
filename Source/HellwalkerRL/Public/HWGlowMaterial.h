// HellwalkerRL — the code-built materials: the additive rim glow (skeletal-mesh capable, parameter "Color") and the ground.

#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;

/** The saved generated asset when present (packaged games), else built in the editor. Null only if neither. */
UMaterialInterface* HWGlowMaterial();

/**
 * The open world's ground. With the Lighthouse pack present: four world-projected texture layers (rock, grass,
 * path dirt, ash) blended by the vertex colour as layer weights (bOutLayered = true). Without it: the
 * vertex colour is the albedo (the generator's GroundColor), broken up by noise.
 */
UMaterialInterface* HWTerrainMaterial(bool* bOutLayered = nullptr);

#if WITH_EDITOR
/** Tools\MakeMaps.bat: save the code-built materials as /Game/HellwalkerRL/Materials assets (packaged games cannot
 *  compile shaders). Returns the number of failed saves. */
int32 HWSaveGeneratedMaterials();
#endif
