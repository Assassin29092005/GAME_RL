#include "HWSaveGame.h"

#include "Kismet/GameplayStatics.h"

UHWSaveGame* UHWSaveGame::LoadOrNull()
{
	if (!UGameplayStatics::DoesSaveGameExist(SlotName, 0)) { return nullptr; }
	UHWSaveGame* S = Cast<UHWSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0));
	return (S != nullptr && S->SaveVersion == Version) ? S : nullptr;
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
