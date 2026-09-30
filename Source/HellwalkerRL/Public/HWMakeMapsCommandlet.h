// Hellwalker — the project's maps, written by code (-run=HWMakeMaps; Tools\MakeMaps.bat).
//
// A map here holds nothing but World Settings with a game-mode override: the valley, the arena, lights and
// actors are all built in C++ at play. So the maps are regenerated, never hand-edited, and opening either one in
// the editor and pressing Play gives the game.
//
//   /Game/HellwalkerRL/Maps/L_Hellwalker   the open world (AHWOpenWorldGameMode) — the default map
//   /Game/HellwalkerRL/Maps/L_Arena        the arena duel on its own (AHWGameMode) — B4 testers, demos

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "HWMakeMapsCommandlet.generated.h"

UCLASS()
class UHWMakeMapsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UHWMakeMapsCommandlet();
	virtual int32 Main(const FString& Params) override;
};
