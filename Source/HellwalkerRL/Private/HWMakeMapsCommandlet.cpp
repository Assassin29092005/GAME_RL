#include "HWMakeMapsCommandlet.h"

#include "HellwalkerRL.h"
#include "HWBuild.h"
#include "HWGameMode.h"
#include "HWGlowMaterial.h"
#include "HWOpenWorldGameMode.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
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
#if WITH_EDITOR
	struct FMap { const TCHAR* Package; TSubclassOf<AGameModeBase> Mode; };
	const FMap Maps[] = {
		{ TEXT("/Game/HellwalkerRL/Maps/L_Hellwalker"), AHWOpenWorldGameMode::StaticClass() },
		{ TEXT("/Game/HellwalkerRL/Maps/L_Arena"), AHWGameMode::StaticClass() },
	};
	// -UsageOnly: only the material steps below (the maps are tracked in git; re-saving them changes nothing but their bytes).
	const bool bUsageOnly = FParse::Param(*Params, TEXT("UsageOnly"));
	int32 Failed = 0;
	for (const FMap& M : Maps)
	{
		if (bUsageOnly) { break; }
		// An existing map (the editor may already have it loaded as its startup map) is updated in place; otherwise it is
		// created. Either way it holds nothing but the game-mode override.
		const FString ShortName = FPackageName::GetShortName(M.Package);
		UWorld* World = FPackageName::DoesPackageExist(M.Package)
			? LoadObject<UWorld>(nullptr, *(FString(M.Package) + TEXT(".") + ShortName)) : nullptr;
		const bool bCreated = World == nullptr;
		UPackage* Package = bCreated ? CreatePackage(M.Package) : World->GetOutermost();
		if (bCreated)
		{
			World = UWorld::CreateWorld(EWorldType::Inactive, false, FName(*ShortName), Package, false);
			World->SetFlags(RF_Public | RF_Standalone);
		}
		World->GetWorldSettings()->DefaultGameMode = M.Mode;
		const FString File = FPackageName::LongPackageNameToFilename(M.Package, FPackageName::GetMapPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		const bool bSaved = UPackage::SavePackage(Package, World, *File, Args);
		UE_LOG(LogHellwalkerRL, Display, TEXT("HWMakeMaps: %s (%s) -> %s: %s"), M.Package, *GetNameSafe(M.Mode.Get()), *File, bSaved ? TEXT("saved") : TEXT("FAILED"));
		Failed += bSaved ? 0 : 1;
		if (bCreated) { World->DestroyWorld(false); }
	}
	// The code-built materials, saved as assets: a packaged game cannot compile shaders at run time.
	if (!bUsageOnly) { Failed += HWSaveGeneratedMaterials(); }
	// The pack materials the dressing instances: their instanced shaders must be cooked (EnsureInstancedUsage is editor-only).
	Failed += HWBuild::SaveInstancedUsageForDressing();
	// The keepers' materials: skeletal (and cloth) usage, likewise needed cooked.
	Failed += HWBuild::SaveSkeletalUsageForCasts();
	return Failed;
#else
	(void)Params;
	UE_LOG(LogHellwalkerRL, Error, TEXT("HWMakeMaps needs an editor build."));
	return 1;
#endif
}
