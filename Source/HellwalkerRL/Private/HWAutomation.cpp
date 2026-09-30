#include "HWAutomation.h"

#include "HellwalkerRL.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputAction.h"
#include "InputActionValue.h"

namespace
{
	struct FHeldInput
	{
		TWeakObjectPtr<UInputAction> Action;
		float Remaining = 0.f;
		bool bAxis = false;
		FVector2D Axis = FVector2D::ZeroVector;
	};
	TArray<FHeldInput> GHeld;
}

void FHWAutomation::Hold(UInputAction* Action, float Seconds, bool bAxis, const FVector2D& Axis)
{
	if (Action == nullptr) { return; }
	FHeldInput H;
	H.Action = Action;
	H.Remaining = FMath::Max(Seconds, 0.02f);
	H.bAxis = bAxis;
	H.Axis = Axis;
	GHeld.Add(H);
}

void FHWAutomation::Init()
{
	const TCHAR* Cmd = FCommandLine::Get();
	FParse::Value(Cmd, TEXT("HWShotAt="), ShotAt);
	FParse::Value(Cmd, TEXT("HWShotEvery="), ShotEvery);
	FParse::Value(Cmd, TEXT("HWShots="), ShotsWanted);
	FParse::Value(Cmd, TEXT("HWQuitAt="), QuitAt);
	FParse::Value(Cmd, TEXT("HWExec="), ExecCmds, false);
	FParse::Value(Cmd, TEXT("HWExecAt="), ExecAt);
	TArray<FString> Cmds;
	ExecCmds.TrimQuotes().ParseIntoArray(Cmds, TEXT("|"));
	FString Untimed;
	for (const FString& C : Cmds)
	{
		FString When;
		FString What;
		if (C.Split(TEXT(":"), &When, &What) && When.IsNumeric()) { Timed.Emplace(FCString::Atof(*When), What.TrimStartAndEnd()); }
		else { Untimed += (Untimed.IsEmpty() ? TEXT("") : TEXT("|")) + C; }
	}
	ExecCmds = Untimed;
}

void FHWAutomation::Tick(UWorld* World, float DeltaSeconds)
{
	PlaySeconds += DeltaSeconds;
	APlayerController* PC = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (!ExecCmds.IsEmpty() && PlaySeconds >= ExecAt)
	{
		TArray<FString> Cmds;
		ExecCmds.TrimQuotes().ParseIntoArray(Cmds, TEXT("|"));
		ExecCmds.Reset();
		if (PC != nullptr)
		{
			for (const FString& C : Cmds) { PC->ConsoleCommand(C.TrimStartAndEnd()); }
		}
	}
	for (int32 I = Timed.Num() - 1; I >= 0; --I)
	{
		if (PlaySeconds >= Timed[I].Key && PC != nullptr)
		{
			UE_LOG(LogHellwalkerRL, Log, TEXT("Automation %.1f s: %s"), PlaySeconds, *Timed[I].Value);
			PC->ConsoleCommand(Timed[I].Value);
			Timed.RemoveAt(I);
		}
	}
	if (GHeld.Num() > 0 && PC != nullptr && PC->GetLocalPlayer() != nullptr)
	{
		if (UEnhancedInputLocalPlayerSubsystem* Sub = PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			for (int32 I = GHeld.Num() - 1; I >= 0; --I)
			{
				FHeldInput& H = GHeld[I];
				if (UInputAction* A = H.Action.Get())
				{
					Sub->InjectInputForAction(A, H.bAxis ? FInputActionValue(H.Axis) : FInputActionValue(true), {}, {});
				}
				H.Remaining -= DeltaSeconds;
				if (H.Remaining <= 0.f || !H.Action.IsValid()) { GHeld.RemoveAt(I); }
			}
		}
	}
	if (ShotAt >= 0.f && PlaySeconds >= ShotAt && ShotsTaken < FMath::Max(ShotsWanted, 1))
	{
		++ShotsTaken;
		ShotAt = ShotEvery > 0.f ? ShotAt + ShotEvery : -1.f;
		FScreenshotRequest::RequestScreenshot(true);
		UE_LOG(LogHellwalkerRL, Log, TEXT("Screenshot requested at %.1f s."), PlaySeconds);
	}
	if (QuitAt >= 0.f && PlaySeconds >= QuitAt)
	{
		QuitAt = -1.f;
		if (PC != nullptr) { PC->ConsoleCommand(TEXT("quit")); }
	}
}
