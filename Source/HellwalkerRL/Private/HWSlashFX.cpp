#include "HWSlashFX.h"

#include "HellwalkerRL.h"

#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Misc/PackageName.h"

namespace
{
	const TCHAR* HitSystemPath = TEXT("/Game/SlashTrail_SoftTofu/Niagara/Basic/NS_Hit_Basic_Once.NS_Hit_Basic_Once");
}

UNiagaraSystem* HWLoadNiagara(const FString& Path)
{
	static TMap<FString, UNiagaraSystem*> Cache;
	if (UNiagaraSystem** Found = Cache.Find(Path)) { return *Found; }
	UNiagaraSystem* System = nullptr;
	if (FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(Path)))
	{
		System = LoadObject<UNiagaraSystem>(nullptr, *Path);
		if (System != nullptr)
		{
			System->AddToRoot();
			TArray<FNiagaraVariable> Params;
			System->GetExposedParameters().GetUserParameters(Params);
			FString List;
			for (const FNiagaraVariable& V : Params) { List += FString::Printf(TEXT(" [%s : %s]"), *V.GetName().ToString(), *V.GetType().GetName()); }
			UE_LOG(LogHellwalkerRL, Display, TEXT("Niagara %s user parameters:%s"), *System->GetName(), *List);
		}
	}
	Cache.Add(Path, System);
	return System;
}

void HWWarmImpactFX(UWorld* World, const FVector& Near)
{
	// Load the burst now and play it once far under the floor: its first real use (the first heavy hit of a launch) loaded
	// it synchronously, and on a cold shader cache Niagara holds a system back until its pipelines are compiled.
	UNiagaraSystem* System = HWLoadNiagara(HitSystemPath);
	if (World == nullptr || System == nullptr) { return; }
	UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, System, Near - FVector(0.f, 0.f, 50000.f), FRotator::ZeroRotator,
		FVector(0.5f), true, true, ENCPoolMethod::AutoRelease);
}

void HWSpawnImpactFX(UWorld* World, const FVector& Where, const FLinearColor& Color, float Size)
{
	UNiagaraSystem* System = HWLoadNiagara(HitSystemPath);
	if (World == nullptr || System == nullptr) { return; }
	UNiagaraComponent* C = UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, System, Where, FRotator::ZeroRotator,
		FVector(FMath::Clamp(Size, 0.5f, 2.f)), true, true, ENCPoolMethod::AutoRelease);
	if (C != nullptr)
	{
		// Parameter names/types as the pack exposes them (logged on load).
		C->SetVariableVec3(TEXT("User.Color"), FVector(Color.R, Color.G, Color.B) * 4.0);
		C->SetVariableVec2(TEXT("User.Size_Flare"), FVector2D(170.0, 14.0) * Size); // the stock flare streaks across the arena
		C->SetVariableFloat(TEXT("User.Alpha_Flare"), 0.6f);
	}
}
