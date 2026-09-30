// Hellwalker — command-line automation shared by the game modes (headless demos, screenshots, E2E checks).
//   -HWShotAt=<s> -HWShotEvery=<s> -HWShots=<n>   screenshots
//   -HWExec="<cmd>[|<cmd>...]" -HWExecAt=<s>      console commands once; "<s>:<cmd>" runs that one at <s>
//   -HWQuitAt=<s>                                  quit
// Console, for scripted play checks (pair with "<s>:<cmd>"):
//   hw.Hold <IA_Name> <seconds> [x y]   press an Enhanced Input action as the player would (the explorer's own
//                                       /Game/Input/IA_* actions): a bool press, or a 2D axis value
//   hw.Where                            log the player pawn's position and speed

#pragma once

#include "CoreMinimal.h"

class UWorld;

struct HELLWALKERRL_API FHWAutomation
{
	void Init();
	void Tick(UWorld* World, float DeltaSeconds);
	void QuitNow() { QuitAt = PlaySeconds; }
	/** hw.Hold: inject Action (bool, or 2D when bAxis) every frame for Seconds. */
	static void Hold(class UInputAction* Action, float Seconds, bool bAxis, const FVector2D& Axis);

	float PlaySeconds = 0.f;
	float ShotAt = -1.f;
	float ShotEvery = -1.f;
	int32 ShotsWanted = 1;
	int32 ShotsTaken = 0;
	float QuitAt = -1.f;
	FString ExecCmds;
	float ExecAt = 1.f;
	TArray<TPair<float, FString>> Timed;
};
