#include "HWSettings.h"
#include "HWCore/HWRLTypes.h"

#include "HellwalkerRL.h"
#include "AudioDevice.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

// =================================================================================================
// Key bindings
// =================================================================================================

namespace
{
	struct FReserved
	{
		FKey Key;
		const TCHAR* Why;
	};

	/** Never bindable: fixed movement, the menu's keys, the title / tier shortcuts, the console. */
	const TArray<FReserved>& ReservedAlways()
	{
		static const TArray<FReserved> R = {
			{ EKeys::W, TEXT("W A S D move") },
			{ EKeys::A, TEXT("W A S D move") },
			{ EKeys::S, TEXT("W A S D move") },
			{ EKeys::D, TEXT("W A S D move") },
			{ EKeys::Escape, TEXT("Esc is fixed: it opens the menu and cancels") },
			{ EKeys::Enter, TEXT("Enter is the menus' accept key") },
			{ EKeys::One, TEXT("1 picks Pathbreaker (title, arena)") },
			{ EKeys::Two, TEXT("2 picks Hellwalker (title, arena)") },
			{ EKeys::Three, TEXT("3 picks 66 Days (title)") },
			{ EKeys::Tilde, TEXT("the console key") },
			{ EKeys::MouseScrollUp, TEXT("the wheel scrolls the menus") },
			{ EKeys::MouseScrollDown, TEXT("the wheel scrolls the menus") },
		};
		return R;
	}

	/** The explorer's own keys (the Game Animation Sample's IMC_Sandbox): an explore-scope binding there would shadow them. */
	const TArray<FReserved>& ReservedExplore()
	{
		static const TArray<FReserved> R = {
			{ EKeys::SpaceBar, TEXT("Space is the explorer's jump") },
			{ EKeys::LeftShift, TEXT("Shift is the explorer's sprint") },
			{ EKeys::LeftControl, TEXT("Ctrl is the explorer's walk toggle") },
			{ EKeys::C, TEXT("C is the explorer's crouch") },
			{ EKeys::R, TEXT("R is the explorer's ragdoll") },
			{ EKeys::LeftMouseButton, TEXT("LMB is the explorer's takedown") },
			{ EKeys::RightMouseButton, TEXT("RMB is the explorer's aim") },
			{ EKeys::MiddleMouseButton, TEXT("MMB is the explorer's strafe") },
			{ EKeys::Up, TEXT("the arrows move the explorer") },
			{ EKeys::Down, TEXT("the arrows move the explorer") },
			{ EKeys::Left, TEXT("the arrows move the explorer") },
			{ EKeys::Right, TEXT("the arrows move the explorer") },
		};
		return R;
	}

	bool Overlap(EHWBindScope A, EHWBindScope B) { return (static_cast<uint8>(A) & static_cast<uint8>(B)) != 0; }
}

FKey FHWBindingTable::DefaultKey(EHWBind Action)
{
	switch (Action)
	{
	case EHWBind::Light:    return EKeys::LeftMouseButton;
	case EHWBind::Heavy:    return EKeys::E;
	case EHWBind::Parry:    return EKeys::Q;
	case EHWBind::Guard:    return EKeys::RightMouseButton;
	case EHWBind::Step:     return EKeys::SpaceBar;
	case EHWBind::Switch:   return EKeys::F;
	case EHWBind::LockOn:   return EKeys::Tab;
	case EHWBind::Restart:  return EKeys::R;
	case EHWBind::Help:     return EKeys::F1;
	case EHWBind::Debug:    return EKeys::F3;
	case EHWBind::Pause:    return EKeys::P;
	case EHWBind::Notebook: return EKeys::N;
	case EHWBind::Interact: return EKeys::E;
	default:                return EKeys::Invalid;
	}
}

FKey FHWBindingTable::GamepadKey(EHWBind Action)
{
	switch (Action)
	{
	case EHWBind::Light:    return EKeys::Gamepad_FaceButton_Left;
	case EHWBind::Heavy:    return EKeys::Gamepad_FaceButton_Top;
	case EHWBind::Parry:    return EKeys::Gamepad_RightShoulder;
	case EHWBind::Guard:    return EKeys::Gamepad_LeftShoulder;
	case EHWBind::Step:     return EKeys::Gamepad_FaceButton_Bottom;
	case EHWBind::Switch:   return EKeys::Gamepad_FaceButton_Right;
	case EHWBind::LockOn:   return EKeys::Gamepad_RightThumbstick;
	case EHWBind::Help:     return EKeys::Gamepad_Special_Left;
	case EHWBind::Debug:    return EKeys::Gamepad_DPad_Up;
	case EHWBind::Pause:    return EKeys::Gamepad_Special_Right;
	case EHWBind::Notebook: return EKeys::Gamepad_DPad_Down;
	case EHWBind::Interact: return EKeys::Gamepad_FaceButton_Top;
	default:                return EKeys::Invalid; // Restart: the pause menu
	}
}

FString FHWBindingTable::GamepadLabel(EHWBind Action)
{
	switch (Action)
	{
	case EHWBind::Light:    return TEXT("X");
	case EHWBind::Heavy:    return TEXT("Y");
	case EHWBind::Parry:    return TEXT("RB");
	case EHWBind::Guard:    return TEXT("LB (hold)");
	case EHWBind::Step:     return TEXT("A + stick");
	case EHWBind::Switch:   return TEXT("B");
	case EHWBind::LockOn:   return TEXT("R3");
	case EHWBind::Restart:  return TEXT("pause menu");
	case EHWBind::Help:     return TEXT("View");
	case EHWBind::Debug:    return TEXT("D-pad up");
	case EHWBind::Pause:    return TEXT("Start");
	case EHWBind::Notebook: return TEXT("D-pad down");
	case EHWBind::Interact: return TEXT("Y");
	default:                return TEXT("-");
	}
}

FString FHWBindingTable::ActionLabel(EHWBind Action)
{
	switch (Action)
	{
	case EHWBind::Light:    return TEXT("Light attack");
	case EHWBind::Heavy:    return TEXT("Heavy attack");
	case EHWBind::Parry:    return TEXT("Parry");
	case EHWBind::Guard:    return TEXT("Block (hold)");
	case EHWBind::Step:     return TEXT("Ghoststep");
	case EHWBind::Switch:   return TEXT("Switch weapon");
	case EHWBind::LockOn:   return TEXT("Lock-on");
	case EHWBind::Restart:  return TEXT("Fight again (arena)");
	case EHWBind::Help:     return TEXT("Controls panel");
	case EHWBind::Debug:    return TEXT("Debug overlay");
	case EHWBind::Pause:    return TEXT("Pause menu");
	case EHWBind::Notebook: return TEXT("The keeper's notebook");
	case EHWBind::Interact: return TEXT("Ring a bell / challenge");
	default:                return TEXT("?");
	}
}

EHWBindScope FHWBindingTable::Scope(EHWBind Action)
{
	switch (Action)
	{
	case EHWBind::Interact: return EHWBindScope::Explore;
	case EHWBind::Help:
	case EHWBind::Debug:
	case EHWBind::Pause:
	case EHWBind::Notebook: return EHWBindScope::Both;
	default:                return EHWBindScope::Duel;
	}
}

FString FHWBindingTable::KeyLabel(const FKey& Key)
{
	if (!Key.IsValid()) { return TEXT("-"); }
	if (Key == EKeys::LeftMouseButton) { return TEXT("LMB"); }
	if (Key == EKeys::RightMouseButton) { return TEXT("RMB"); }
	if (Key == EKeys::MiddleMouseButton) { return TEXT("MMB"); }
	if (Key == EKeys::ThumbMouseButton) { return TEXT("Mouse 4"); }
	if (Key == EKeys::ThumbMouseButton2) { return TEXT("Mouse 5"); }
	if (Key == EKeys::SpaceBar) { return TEXT("Space"); }
	if (Key == EKeys::LeftShift) { return TEXT("L-Shift"); }
	if (Key == EKeys::RightShift) { return TEXT("R-Shift"); }
	if (Key == EKeys::LeftControl) { return TEXT("L-Ctrl"); }
	if (Key == EKeys::RightControl) { return TEXT("R-Ctrl"); }
	if (Key == EKeys::LeftAlt) { return TEXT("L-Alt"); }
	if (Key == EKeys::RightAlt) { return TEXT("R-Alt"); }
	if (Key == EKeys::BackSpace) { return TEXT("Backspace"); }
	if (Key == EKeys::CapsLock) { return TEXT("Caps Lock"); }
	return Key.GetDisplayName(false).ToString();
}

bool FHWBindingTable::IsBindable(const FKey& Key, EHWBindScope InScope, FString& OutWhyNot)
{
	if (!Key.IsValid()) { OutWhyNot = TEXT("not a key"); return false; }
	if (Key.IsGamepadKey()) { OutWhyNot = TEXT("gamepad buttons are fixed"); return false; }
	if (Key.IsTouch() || Key.IsGesture() || Key.IsVirtual()) { OutWhyNot = TEXT("not a keyboard key or mouse button"); return false; }
	for (const FReserved& R : ReservedAlways())
	{
		if (R.Key == Key) { OutWhyNot = R.Why; return false; }
	}
	if (Key.IsAxis1D() || Key.IsAxis2D() || Key.IsAxis3D() || Key.IsAnalog())
	{
		OutWhyNot = TEXT("an axis cannot be a button");
		return false;
	}
	if (Overlap(InScope, EHWBindScope::Explore))
	{
		for (const FReserved& R : ReservedExplore())
		{
			if (R.Key == Key) { OutWhyNot = R.Why; return false; }
		}
	}
	return true;
}

void FHWBindingTable::ResetToDefaults()
{
	for (int32 I = 0; I < Num; ++I) { Keys[I] = DefaultKey(static_cast<EHWBind>(I)); }
}

bool FHWBindingTable::IsValid() const
{
	for (int32 I = 0; I < Num; ++I)
	{
		FString Why;
		if (!IsBindable(Keys[I], Scope(static_cast<EHWBind>(I)), Why)) { return false; }
		for (int32 J = I + 1; J < Num; ++J)
		{
			if (Keys[I] == Keys[J] && Overlap(Scope(static_cast<EHWBind>(I)), Scope(static_cast<EHWBind>(J)))) { return false; }
		}
	}
	return true;
}

bool FHWBindingTable::operator==(const FHWBindingTable& O) const
{
	for (int32 I = 0; I < Num; ++I)
	{
		if (Keys[I] != O.Keys[I]) { return false; }
	}
	return true;
}

FHWBindingTable::EResult FHWBindingTable::Rebind(EHWBind Action, const FKey& Key, FString& OutMessage)
{
	const int32 A = static_cast<int32>(Action);
	if (A < 0 || A >= Num) { OutMessage = TEXT("Unknown action."); return EResult::Refused; }
	const FString Name = ActionLabel(Action);
	if (Keys[A] == Key)
	{
		OutMessage = FString::Printf(TEXT("%s stays on %s."), *Name, *KeyLabel(Key));
		return EResult::Unchanged;
	}
	FString Why;
	if (!IsBindable(Key, Scope(Action), Why))
	{
		OutMessage = FString::Printf(TEXT("%s cannot be bound: %s."), *KeyLabel(Key), *Why);
		return EResult::Refused;
	}
	int32 Other = INDEX_NONE;
	for (int32 I = 0; I < Num; ++I)
	{
		if (I != A && Keys[I] == Key && Overlap(Scope(Action), Scope(static_cast<EHWBind>(I)))) { Other = I; break; }
	}
	FHWBindingTable Next = *this;
	Next.Keys[A] = Key;
	if (Other != INDEX_NONE) { Next.Keys[Other] = Keys[A]; }
	if (!Next.IsValid())
	{
		OutMessage = Other != INDEX_NONE
			? FString::Printf(TEXT("%s is %s's key, and %s cannot take %s in exchange."), *KeyLabel(Key), *ActionLabel(static_cast<EHWBind>(Other)),
				*ActionLabel(static_cast<EHWBind>(Other)), *KeyLabel(Keys[A]))
			: FString::Printf(TEXT("%s would clash with another binding."), *KeyLabel(Key));
		return EResult::Refused;
	}
	*this = Next;
	if (Other != INDEX_NONE)
	{
		OutMessage = FString::Printf(TEXT("%s: %s  -  swapped: %s is now on %s."), *Name, *KeyLabel(Key), *ActionLabel(static_cast<EHWBind>(Other)),
			*KeyLabel(Keys[Other]));
		return EResult::Swapped;
	}
	OutMessage = FString::Printf(TEXT("%s: %s."), *Name, *KeyLabel(Key));
	return EResult::Bound;
}

TArray<FHWKeyBinding> FHWBindingTable::ToArray() const
{
	TArray<FHWKeyBinding> Out;
	for (int32 I = 0; I < Num; ++I)
	{
		FHWKeyBinding B;
		B.Action = static_cast<EHWBind>(I);
		B.Key = Keys[I];
		Out.Add(B);
	}
	return Out;
}

void FHWBindingTable::FromArray(const TArray<FHWKeyBinding>& In)
{
	FHWBindingTable Next;
	for (const FHWKeyBinding& B : In)
	{
		const int32 I = static_cast<int32>(B.Action);
		if (I >= 0 && I < Num && B.Key.IsValid()) { Next.Keys[I] = B.Key; }
	}
	if (Next.IsValid())
	{
		*this = Next;
	}
	else
	{
		UE_LOG(LogHellwalkerRL, Warning, TEXT("Settings: the saved key bindings clash; back to the defaults."));
		ResetToDefaults();
	}
}

// =================================================================================================
// Data and the save slot
// =================================================================================================

void FHWSettingsData::Sanitize()
{
	if (static_cast<uint8>(Difficulty) > static_cast<uint8>(EHWDifficulty::Adaptive)) { Difficulty = EHWDifficulty::Hellwalker; }
	auto Fix = [](float& V, float Lo, float Hi, float Default) { V = FMath::IsFinite(V) ? FMath::Clamp(V, Lo, Hi) : Default; };
	Fix(MouseSensitivity, MinSensitivity, MaxSensitivity, 1.f);
	Fix(MasterVolume, 0.f, 1.f, 1.f);
	Fix(MusicVolume, 0.f, 1.f, 1.f);
	Fix(EffectsVolume, 0.f, 1.f, 1.f);
	Fix(HudScale, MinHudScale, MaxHudScale, 1.f);
	Fix(ReadHoldSeconds, MinReadHold, MaxReadHold, 0.4f);
}

UHWSettingsSave* UHWSettingsSave::LoadOrNull(const FString& Slot)
{
	if (!UGameplayStatics::DoesSaveGameExist(Slot, 0)) { return nullptr; }
	UHWSettingsSave* S = Cast<UHWSettingsSave>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
	if (S == nullptr || S->SaveVersion > Version || S->SaveVersion < 1) { return nullptr; }
	S->Data.Sanitize();
	return S;
}

bool UHWSettingsSave::Write(const FString& Slot)
{
	SaveVersion = Version;
	return UGameplayStatics::SaveGameToSlot(this, Slot, 0);
}

// =================================================================================================
// The subsystem
// =================================================================================================

void UHWSettingsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bPersist = !FParse::Param(FCommandLine::Get(), TEXT("HWNoSave"));
	if (const UHWSettingsSave* Loaded = UHWSettingsSave::LoadOrNull())
	{
		Data = Loaded->Data;
		Bindings.FromArray(Data.Bindings);
	}
	Data.Bindings = Bindings.ToArray();
	FString Forced;
	if (FParse::Value(FCommandLine::Get(), TEXT("-HWDifficulty="), Forced))
	{
		// Scripted runs (parity, demos): a fixed difficulty, whatever the save holds; never written back.
		for (EHWDifficulty D : { EHWDifficulty::Easy, EHWDifficulty::Normal, EHWDifficulty::Hard, EHWDifficulty::Hellwalker, EHWDifficulty::Adaptive })
		{
			if (Forced.Equals(DifficultyName(D), ESearchCase::IgnoreCase)) { Data.Difficulty = D; bPersist = false; }
		}
	}
	ApplyAll();
	UE_LOG(LogHellwalkerRL, Log, TEXT("Settings: difficulty %s, HUD %.0f%%, READ hold %.1f s, master %.0f%%%s."), *DifficultyName(Data.Difficulty),
		Data.HudScale * 100.f, Data.ReadHoldSeconds, Data.MasterVolume * 100.f, bPersist ? TEXT("") : TEXT(" (not saved: -HWNoSave)"));
}

void UHWSettingsSubsystem::ApplyAll()
{
	ApplyAudio();
}

void UHWSettingsSubsystem::ApplyAudio()
{
	if (GEngine == nullptr) { return; }
	if (FAudioDevice* Device = GEngine->GetMainAudioDeviceRaw())
	{
		Device->SetTransientPrimaryVolume(Data.MasterVolume);
	}
}

bool UHWSettingsSubsystem::Save()
{
	Data.Bindings = Bindings.ToArray();
	if (!bPersist) { return false; }
	UHWSettingsSave* S = Cast<UHWSettingsSave>(UGameplayStatics::CreateSaveGameObject(UHWSettingsSave::StaticClass()));
	if (S == nullptr) { return false; }
	S->Data = Data;
	return S->Write();
}

void UHWSettingsSubsystem::Changed()
{
	Data.Sanitize();
	Save();
}

void UHWSettingsSubsystem::KeeperSkillFor(EHWDifficulty D, float& OutSkill, float& OutTemperature, bool& bOutAdaptive)
{
	bOutAdaptive = false;
	switch (D)
	{
	case EHWDifficulty::Easy:     OutSkill = 0.f;   OutTemperature = 1.f; break;
	case EHWDifficulty::Normal:   OutSkill = 0.4f;  OutTemperature = 0.6f; break;
	case EHWDifficulty::Hard:     OutSkill = 0.75f; OutTemperature = 0.f; break;
	case EHWDifficulty::Adaptive: OutSkill = 0.6f;  OutTemperature = 0.f; bOutAdaptive = true; break;
	default:                      OutSkill = 1.f;   OutTemperature = 0.f; break;
	}
}

int32 UHWSettingsSubsystem::KeeperSwingGapFor(EHWDifficulty D)
{
	// The same network at skill 0 still out-hits the script; Easy also spaces its attacks out (a breath for you).
	return D == EHWDifficulty::Easy ? HW::RL::EasySwingGap : 0;
}

void UHWSettingsSubsystem::GetKeeperSkill(float& OutSkill, float& OutTemperature, bool& bOutAdaptive) const
{
	KeeperSkillFor(Data.Difficulty, OutSkill, OutTemperature, bOutAdaptive);
}

FString UHWSettingsSubsystem::DifficultyName(EHWDifficulty D)
{
	switch (D)
	{
	case EHWDifficulty::Easy:     return TEXT("Easy");
	case EHWDifficulty::Normal:   return TEXT("Normal");
	case EHWDifficulty::Hard:     return TEXT("Hard");
	case EHWDifficulty::Adaptive: return TEXT("Adaptive");
	default:                      return TEXT("Hellwalker");
	}
}

FString UHWSettingsSubsystem::DifficultyBlurb(EHWDifficulty D)
{
	switch (D)
	{
	case EHWDifficulty::Easy:     return TEXT("It sees you later, strikes once at a time, sometimes guesses, and gives you a breath between attacks.");
	case EHWDifficulty::Normal:   return TEXT("It sees you a little late and keeps its strings short.");
	case EHWDifficulty::Hard:     return TEXT("Quick eyes and long strings, a step short of full strength.");
	case EHWDifficulty::Adaptive: return TEXT("It adjusts its strength between fights to keep them close.");
	default:                      return TEXT("Full strength - the tier the tests grade.");
	}
}

void UHWSettingsSubsystem::SetDifficulty(EHWDifficulty D) { Data.Difficulty = D; Changed(); }
void UHWSettingsSubsystem::SetMouseSensitivity(float V) { Data.MouseSensitivity = V; Changed(); }
void UHWSettingsSubsystem::SetInvertY(bool b) { Data.bInvertY = b; Changed(); }
void UHWSettingsSubsystem::SetMasterVolume(float V) { Data.MasterVolume = V; Changed(); ApplyAudio(); }
void UHWSettingsSubsystem::SetMusicVolume(float V) { Data.MusicVolume = V; Changed(); }
void UHWSettingsSubsystem::SetEffectsVolume(float V) { Data.EffectsVolume = V; Changed(); }
void UHWSettingsSubsystem::SetHudScale(float V) { Data.HudScale = V; Changed(); }
void UHWSettingsSubsystem::SetReadHoldSeconds(float V) { Data.ReadHoldSeconds = V; Changed(); }
void UHWSettingsSubsystem::SetCameraShake(bool b) { Data.bCameraShake = b; Changed(); }
void UHWSettingsSubsystem::SetTutorialSeen(bool b) { Data.bTutorialSeen = b; Changed(); }

FHWBindingTable::EResult UHWSettingsSubsystem::Rebind(EHWBind Action, const FKey& Key, FString& OutMessage)
{
	const FHWBindingTable::EResult R = Bindings.Rebind(Action, Key, OutMessage);
	if (R == FHWBindingTable::EResult::Bound || R == FHWBindingTable::EResult::Swapped)
	{
		Changed();
		OnBindingsChanged.Broadcast();
	}
	UE_LOG(LogHellwalkerRL, Log, TEXT("Rebind: %s"), *OutMessage);
	return R;
}

void UHWSettingsSubsystem::ResetBindings()
{
	Bindings.ResetToDefaults();
	Changed();
	OnBindingsChanged.Broadcast();
	UE_LOG(LogHellwalkerRL, Log, TEXT("Key bindings reset to the defaults."));
}

// ---- graphics ------------------------------------------------------------------------------------

const TArray<float>& UHWSettingsSubsystem::FrameLimits()
{
	static const TArray<float> L = { 30.f, 60.f, 120.f, 0.f };
	return L;
}

FString UHWSettingsSubsystem::QualityName(int32 Level)
{
	switch (Level)
	{
	case 0:  return TEXT("Low");
	case 1:  return TEXT("Medium");
	case 2:  return TEXT("High");
	case 3:  return TEXT("Epic");
	case 4:  return TEXT("Cinematic");
	default: return TEXT("Custom");
	}
}

void UHWSettingsSubsystem::LoadGraphics()
{
	Resolutions.Reset();
	UKismetSystemLibrary::GetSupportedFullscreenResolutions(Resolutions);
	UGameUserSettings* GUS = GEngine != nullptr ? GEngine->GetGameUserSettings() : nullptr;
	if (GUS == nullptr) { return; }
	FHWGraphicsChoice G;
	G.Quality = FMath::Clamp(GUS->GetOverallScalabilityLevel(), -1, 4); // -1 = custom (hand-set scalability groups)
	G.Resolution = GUS->GetScreenResolution();
	G.WindowMode = static_cast<uint8>(GUS->GetFullscreenMode());
	G.bVSync = GUS->IsVSyncEnabled();
	const float Limit = GUS->GetFrameRateLimit();
	G.FrameLimit = FrameLimits().Num() - 1;
	for (int32 I = 0; I < FrameLimits().Num(); ++I)
	{
		if (FMath::IsNearlyEqual(FrameLimits()[I], Limit, 0.5f)) { G.FrameLimit = I; }
	}
	Resolutions.AddUnique(G.Resolution);
	Resolutions.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.X != B.X ? A.X < B.X : A.Y < B.Y; });
	Graphics = G;
	GraphicsApplied = G;
}

bool UHWSettingsSubsystem::HasUnappliedGraphics() const
{
	return Graphics.Quality != GraphicsApplied.Quality || Graphics.Resolution != GraphicsApplied.Resolution || Graphics.WindowMode != GraphicsApplied.WindowMode
		|| Graphics.bVSync != GraphicsApplied.bVSync || Graphics.FrameLimit != GraphicsApplied.FrameLimit;
}

void UHWSettingsSubsystem::ApplyGraphics()
{
	UGameUserSettings* GUS = GEngine != nullptr ? GEngine->GetGameUserSettings() : nullptr;
	if (GUS == nullptr) { return; }
	if (Graphics.Quality >= 0) { GUS->SetOverallScalabilityLevel(FMath::Clamp(Graphics.Quality, 0, 4)); }
	GUS->SetScreenResolution(Graphics.Resolution);
	GUS->SetFullscreenMode(static_cast<EWindowMode::Type>(FMath::Clamp<int32>(Graphics.WindowMode, 0, 2)));
	GUS->SetVSyncEnabled(Graphics.bVSync);
	GUS->SetFrameRateLimit(FrameLimits()[FMath::Clamp(Graphics.FrameLimit, 0, FrameLimits().Num() - 1)]);
	GUS->ApplySettings(false);
	GUS->SaveSettings();
	GraphicsApplied = Graphics;
	UE_LOG(LogHellwalkerRL, Log, TEXT("Graphics applied: quality %d, %dx%d, window mode %d, vsync %d, limit %.0f."), Graphics.Quality,
		Graphics.Resolution.X, Graphics.Resolution.Y, Graphics.WindowMode, Graphics.bVSync ? 1 : 0, FrameLimits()[Graphics.FrameLimit]);
}
