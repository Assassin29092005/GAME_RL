#include "HWMakeMapsCommandlet.h"

#include "HellwalkerRL.h"
#include "HWGameMode.h"
#include "HWOpenWorldGameMode.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

UHWMakeMapsCommandlet::UHWMakeMapsCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UHWMakeMapsCommandlet::Main(const FString& Params)
{
	(void)Params;
#if WITH_EDITOR
	struct FMap { const TCHAR* Package; TSubclassOf<AGameModeBase> Mode; };
	const FMap Maps[] = {
		{ TEXT("/Game/HellwalkerRL/Maps/L_Hellwalker"), AHWOpenWorldGameMode::StaticClass() },
		{ TEXT("/Game/HellwalkerRL/Maps/L_Arena"), AHWGameMode::StaticClass() },
	};
	int32 Failed = 0;
	for (const FMap& M : Maps)
	{
		UPackage* Package = CreatePackage(M.Package);
		UWorld* World = UWorld::CreateWorld(EWorldType::Inactive, false, FName(*FPackageName::GetShortName(M.Package)), Package, false);
		World->SetFlags(RF_Public | RF_Standalone);
		World->GetWorldSettings()->DefaultGameMode = M.Mode;
		const FString File = FPackageName::LongPackageNameToFilename(M.Package, FPackageName::GetMapPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		const bool bSaved = UPackage::SavePackage(Package, World, *File, Args);
		UE_LOG(LogHellwalkerRL, Display, TEXT("HWMakeMaps: %s (%s) -> %s: %s"), M.Package, *GetNameSafe(M.Mode.Get()), *File, bSaved ? TEXT("saved") : TEXT("FAILED"));
		Failed += bSaved ? 0 : 1;
		World->DestroyWorld(false);
	}
	return Failed;
#else
	UE_LOG(LogHellwalkerRL, Error, TEXT("HWMakeMaps needs an editor build."));
	return 1;
#endif
}
