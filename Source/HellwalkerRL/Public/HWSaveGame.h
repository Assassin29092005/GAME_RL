// Hellwalker — open world progress (one slot). The playstyle model is NOT saved: the Warden forgets you
// when you quit (PLAN: the model is per session), which keeps every session's read honest.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "HWTypesUE.h"
#include "HWSaveGame.generated.h"

UCLASS()
class HELLWALKERRL_API UHWSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	static constexpr const TCHAR* SlotName = TEXT("HellwalkerRL_World");
	static constexpr int32 Version = 1;
	static constexpr int32 Days = 66;       // the retired 66 Days mode's lives (DaysLeft): kept so old saves and their readers load

	/** Null when the slot is empty or from another version. A retired 66 Days walk loads as Adaptive AI (Hellwalker). */
	static UHWSaveGame* LoadOrNull();
	static UHWSaveGame* NewGame(EHWPlayMode Mode);
	static void Erase();
	bool Write();

	bool IsShrineCleared(int32 Index) const { return (ShrinesCleared & (1 << Index)) != 0; }
	bool IsBellLit(int32 Index) const { return (BellsLit & (1 << Index)) != 0; }

	UPROPERTY() int32 SaveVersion = Version;
	UPROPERTY() EHWPlayMode Mode = EHWPlayMode::Hellwalker;
	UPROPERTY() int32 CheckpointBell = 0;
	UPROPERTY() int32 BellsLit = 1;         // bit per bell; the first is lit when you arrive
	UPROPERTY() int32 ShrinesCleared = 0;   // bit per shrine
	UPROPERTY() int32 DaysLeft = Days;      // the retired 66 Days only: no longer counted down
	UPROPERTY() int32 Deaths = 0;
	UPROPERTY() float PlaySeconds = 0.f;
	UPROPERTY() bool bFinished = false;
};
