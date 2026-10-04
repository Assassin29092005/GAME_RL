#include "HWSaveGame.h"

#include "HellwalkerRL.h"
#include "Kismet/GameplayStatics.h"

UHWSaveGame* UHWSaveGame::LoadOrNull()
{
	if (!UGameplayStatics::DoesSaveGameExist(SlotName, 0)) { return nullptr; }
	UHWSaveGame* S = Cast<UHWSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0));
	if (S == nullptr || S->SaveVersion != Version) { return nullptr; }
	if (S->Mode == EHWPlayMode::SixtySixDays)
	{
		// 66 Days is retired (1.4): its walk goes on as Adaptive AI (the same keepers), without the lives.
		UE_LOG(LogHellwalkerRL, Log, TEXT("Save: a 66 Days walk (%d days left) continues as Adaptive AI."), S->DaysLeft);
		S->Mode = EHWPlayMode::Hellwalker;
	}
	if (static_cast<uint8>(S->Mode) > static_cast<uint8>(EHWPlayMode::SixtySixDays)) { S->Mode = EHWPlayMode::Hellwalker; }
	return S;
}

UHWSaveGame* UHWSaveGame::NewGame(EHWPlayMode Mode)
{
	UHWSaveGame* S = Cast<UHWSaveGame>(UGameplayStatics::CreateSaveGameObject(UHWSaveGame::StaticClass()));
	S->Mode = Mode;
	return S;
}

void UHWSaveGame::Erase()
{
	if (UGameplayStatics::DoesSaveGameExist(SlotName, 0)) { UGameplayStatics::DeleteGameInSlot(SlotName, 0); }
}

bool UHWSaveGame::Write()
{
	return UGameplayStatics::SaveGameToSlot(this, SlotName, 0);
}
