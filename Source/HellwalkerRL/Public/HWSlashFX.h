// Hellwalker — C2 presentation effects from the Slash Trail FX pack (SoftTofu): weapon trails over the
// active frames and a burst on every resolved contact. Loaded by path; absent pack = no effect.

#pragma once

#include "CoreMinimal.h"

class UNiagaraSystem;
class UWorld;

/** Load (and cache) a Niagara system by object path. Null when the pack is not in the project. */
UNiagaraSystem* HWLoadNiagara(const FString& Path);

/** A burst where a blow resolved (hit / block / parry): colour carries the outcome. */
void HWSpawnImpactFX(UWorld* World, const FVector& Where, const FLinearColor& Color, float Size);
