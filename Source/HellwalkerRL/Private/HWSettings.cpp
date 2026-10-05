#include "HWSettings.h"

#include "HellwalkerRL.h"
#include "AudioDevice.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/App.h"
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
			{ EKeys::One, TEXT("1 and 2 pick the mode (title, arena)") },
			{ EKeys::Two, TEXT("1 and 2 pick the mode (title, arena)") },
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
	case EHWBind::Map:      return EKeys::M;
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
	case EHWBind::Map:      return EKeys::Gamepad_DPad_Left;  // the arena's tier key, never mapped while exploring
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
	case EHWBind::Map:      return TEXT("D-pad left");
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
	case EHWBind::Map:      return TEXT("The valley map");
	default:                return TEXT("?");
	}
}

EHWBindScope FHWBindingTable::Scope(EHWBind Action)
{
	switch (Action)
	{
	case EHWBind::Interact:
	case EHWBind::Map:      return EHWBindScope::Explore;
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

bool FHWBindingTable::Fits(EHWBind Action, const FKey& Key) const
{
	FString Why;
	if (!IsBindable(Key, Scope(Action), Why)) { return false; }
	for (int32 J = 0; J < Num; ++J)
	{
		if (J != static_cast<int32>(Action) && Keys[J] == Key && Overlap(Scope(Action), Scope(static_cast<EHWBind>(J)))) { return false; }
	}
	return true;
}

void FHWBindingTable::FromArray(const TArray<FHWKeyBinding>& In)
{
	FHWBindingTable Next;
	bool bSaved[Num] = {};
	for (const FHWKeyBinding& B : In)
	{
		const int32 I = static_cast<int32>(B.Action);
		if (I >= 0 && I < Num && B.Key.IsValid())
		{
			Next.Keys[I] = B.Key;
			bSaved[I] = true;
		}
	}
	// Migration: an action the save does not know (added since) keeps its default unless the player's keys took it.
	static const FKey Fallbacks[] = { EKeys::M, EKeys::K, EKeys::L, EKeys::J, EKeys::U, EKeys::I, EKeys::O, EKeys::G, EKeys::H,
		EKeys::T, EKeys::Y, EKeys::B, EKeys::V, EKeys::X, EKeys::Z, EKeys::F2, EKeys::F4, EKeys::F5, EKeys::F6, EKeys::F7, EKeys::F8 };
	for (int32 I = 0; I < Num; ++I)
	{
		const EHWBind A = static_cast<EHWBind>(I);
		if (bSaved[I] || Next.Fits(A, Next.Keys[I])) { continue; }
		for (const FKey& K : Fallbacks)
		{
			if (Next.Fits(A, K))
			{
				UE_LOG(LogHellwalkerRL, Log, TEXT("Settings: the saved bindings predate %s and hold its key %s; it takes %s."), *ActionLabel(A),
					*KeyLabel(Next.Keys[I]), *KeyLabel(K));
				Next.Keys[I] = K;
				break;
			}
		}
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
	// The retired Adaptive difficulty (and anything unknown) plays Normal; an unknown assist is the default.
	if (static_cast<uint8>(Difficulty) >= static_cast<uint8>(EHWDifficulty::Adaptive)) { Difficulty = EHWDifficulty::Normal; }
	if (static_cast<uint8>(ParryAssist) > static_cast<uint8>(EHWParryAssist::Off)) { ParryAssist = EHWParryAssist::RingSlow; }
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
		bool bKnown = false;
		for (int32 I = 0; I < NumMenuDifficulties; ++I)
		{
			const EHWDifficulty D = static_cast<EHWDifficulty>(I);
			if (Forced.Equals(DifficultyName(D), ESearchCase::IgnoreCase)) { Data.Difficulty = D; bPersist = false; bKnown = true; }
		}
		if (!bKnown && Forced.Equals(TEXT("Adaptive"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogHellwalkerRL, Warning, TEXT("Settings: -HWDifficulty=Adaptive is retired (Adaptive AI now learns at every difficulty): playing Normal."));
			Data.Difficulty = EHWDifficulty::Normal;
			bPersist = false;
		}
	}
	if (FParse::Value(FCommandLine::Get(), TEXT("-HWParryAssist="), Forced))
	{
		// Like -HWDifficulty: this session only, and it also holds in scripted launches (which otherwise play without it).
		EHWParryAssist A = EHWParryAssist::RingSlow;
		if (ParseParryAssist(Forced, A))
		{
			Data.ParryAssist = A;
			bParryAssistOverride = true;
			bPersist = false;
		}
		else
		{
			UE_LOG(LogHellwalkerRL, Warning, TEXT("Settings: -HWParryAssist=%s is not RingSlow, Ring or Off: ignored."), *Forced);
		}
	}
	ApplyAll();
	ChooseFirstLaunchGraphics();
	UE_LOG(LogHellwalkerRL, Log, TEXT("Settings: difficulty %s, parry assist %s%s, HUD %.0f%%, READ hold %.1f s, master %.0f%%%s."), *DifficultyName(Data.Difficulty),
		*ParryAssistName(Data.ParryAssist), bParryAssistOverride ? TEXT(" (-HWParryAssist)") : TEXT(""), Data.HudScale * 100.f, Data.ReadHoldSeconds,
		Data.MasterVolume * 100.f, bPersist ? TEXT("") : TEXT(" (not saved: -HWNoSave / a command-line override)"));
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

FHWDifficultyPreset UHWSettingsSubsystem::PresetFor(EHWDifficulty D)
{
	// Every difficulty is softer than the network trained (skill 1, damage x1); even Hellwalker starts at skill 0.3 and
	// only reaches full strength once the insight ramp says it knows you.
	FHWDifficultyPreset P;
	switch (D)
	{
	case EHWDifficulty::Easy:
		P.SkillLo = 0.f;   P.SkillHi = 0.4f;  P.KeeperDamageScale = 0.6f;  P.AssistSlowScale = 0.4f;  P.bIncomingGlow = true;
		break;
	case EHWDifficulty::Hard:
		P.SkillLo = 0.15f; P.SkillHi = 0.85f; P.KeeperDamageScale = 0.85f; P.AssistSlowScale = 0.75f; P.bIncomingGlow = false;
		break;
	case EHWDifficulty::Hellwalker:
		P.SkillLo = 0.3f;  P.SkillHi = 1.f;   P.KeeperDamageScale = 0.9f;  P.AssistSlowScale = 0.85f; P.bIncomingGlow = false;
		break;
	default: // Normal, and the retired Adaptive
		P.SkillLo = 0.f;   P.SkillHi = 0.7f;  P.KeeperDamageScale = 0.75f; P.AssistSlowScale = 0.6f;  P.bIncomingGlow = false;
		break;
	}
	return P;
}

FString UHWSettingsSubsystem::DifficultyName(EHWDifficulty D)
{
	switch (D)
	{
	case EHWDifficulty::Easy:     return TEXT("Easy");
	case EHWDifficulty::Normal:   return TEXT("Normal");
	case EHWDifficulty::Hard:     return TEXT("Hard");
	case EHWDifficulty::Adaptive: return TEXT("Adaptive"); // retired: never offered, sanitised to Normal on load
	default:                      return TEXT("Hellwalker");
	}
}

FString UHWSettingsSubsystem::DifficultyBlurb(EHWDifficulty D)
{
	const FHWDifficultyPreset P = PresetFor(D);
	const int32 Softer = FMath::RoundToInt32((1.f - P.KeeperDamageScale) * 100.f);
	const int32 Speed = FMath::RoundToInt32(P.AssistSlowScale * 100.f);
	const TCHAR* How = nullptr;
	switch (D)
	{
	case EHWDifficulty::Easy:       How = TEXT("In Adaptive AI it starts out guessing and never gets past a beginner's eye."); break;
	case EHWDifficulty::Hard:       How = TEXT("In Adaptive AI it starts out guessing, but from a sharper eye, and climbs higher as it learns you."); break;
	case EHWDifficulty::Hellwalker: How = TEXT("In Adaptive AI it starts out guessing, from its sharpest eye, and nears full strength once it knows you."); break;
	default:                        How = TEXT("In Adaptive AI it starts out guessing and sharpens as it learns you."); break;
	}
	return FString::Printf(TEXT("Its hits land %d%% softer.  %s  Parry assist: a red ring when a parry would land, slow motion to %d%% speed%s."), Softer, How, Speed,
		P.bIncomingGlow ? TEXT(", and its attacks glow as they come") : TEXT(""));
}

FHWAssistParams UHWSettingsSubsystem::AssistFor(EHWParryAssist A, EHWDifficulty D)
{
	FHWAssistParams Out;
	if (A == EHWParryAssist::Off) { return Out; } // no help at all
	const FHWDifficultyPreset P = PresetFor(D);
	Out.bRing = true;
	Out.bIncomingGlow = P.bIncomingGlow;
	Out.SlowScale = A == EHWParryAssist::RingSlow ? FMath::Clamp(P.AssistSlowScale, 0.05f, 1.f) : 1.f;
	return Out;
}

FString UHWSettingsSubsystem::ParryAssistName(EHWParryAssist A)
{
	switch (A)
	{
	case EHWParryAssist::Ring: return TEXT("Ring only");
	case EHWParryAssist::Off:  return TEXT("Off");
	default:                   return TEXT("Ring + slow motion");
	}
}

FString UHWSettingsSubsystem::ParryAssistBlurb(EHWParryAssist A)
{
	switch (A)
	{
	case EHWParryAssist::Ring: return TEXT("A red ring lights on the keeper's weapon exactly while a parry would land.  Grey: too late to act.");
	case EHWParryAssist::Off:  return TEXT("No help: read the swing yourself.");
	default:                   return TEXT("A red ring lights on the keeper's weapon exactly while a parry would land, and the fight slows down while it is lit "
		"(how much: the difficulty).  Grey: too late to act.");
	}
}

FString UHWSettingsSubsystem::AssistTelemetryName(const FHWAssistParams& P)
{
	if (!P.bRing) { return TEXT("off"); }
	return P.SlowScale < 1.f ? TEXT("ring+slowmo") : TEXT("ring");
}

bool UHWSettingsSubsystem::ParseParryAssist(const FString& S, EHWParryAssist& Out)
{
	const FString L = S.TrimStartAndEnd().ToLower();
	if (L == TEXT("ringslow") || L == TEXT("ring+slowmo") || L == TEXT("slow")) { Out = EHWParryAssist::RingSlow; return true; }
	if (L == TEXT("ring")) { Out = EHWParryAssist::Ring; return true; }
	if (L == TEXT("off") || L == TEXT("none")) { Out = EHWParryAssist::Off; return true; }
	return false;
}

void UHWSettingsSubsystem::SetDifficulty(EHWDifficulty D) { Data.Difficulty = D; Changed(); }
void UHWSettingsSubsystem::SetParryAssist(EHWParryAssist A) { Data.ParryAssist = A; Changed(); }
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
	static const TArray<float> L = { 60.f, 120.f, 0.f }; // never below 60; 0 = unlimited (the default)
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

namespace
{
	/** The scalability groups' levels (the Graphics tab sets them all at once). */
	TArray<int32> QualityGroups(const UGameUserSettings& GUS)
	{
		return { GUS.GetViewDistanceQuality(), GUS.GetShadowQuality(), GUS.GetGlobalIlluminationQuality(), GUS.GetReflectionQuality(),
			GUS.GetAntiAliasingQuality(), GUS.GetTextureQuality(), GUS.GetVisualEffectQuality(), GUS.GetPostProcessingQuality(),
			GUS.GetFoliageQuality(), GUS.GetShadingQuality(), GUS.GetLandscapeQuality() };
	}

	/**
	 * The level every group is at, or -1 (custom). Not GetOverallScalabilityLevel: it also wants the resolution quality to
	 * match the level, and the saved settings keep that at 0, so after a restart every level read "Custom".
	 */
	int32 UniformQuality(const UGameUserSettings& GUS)
	{
		const TArray<int32> Levels = QualityGroups(GUS);
		for (const int32 L : Levels) { if (L != Levels[0]) { return -1; } }
		return Levels[0];
	}
}

void UHWSettingsSubsystem::LoadGraphics()
{
	Resolutions.Reset();
	UKismetSystemLibrary::GetSupportedFullscreenResolutions(Resolutions);
	UGameUserSettings* GUS = GEngine != nullptr ? GEngine->GetGameUserSettings() : nullptr;
	if (GUS == nullptr) { return; }
	FHWGraphicsChoice G;
	G.Quality = FMath::Clamp(UniformQuality(*GUS), -1, 4); // -1 = custom (hand-set scalability groups)
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

void UHWSettingsSubsystem::ChooseFirstLaunchGraphics()
{
	// The engine's default is Epic on every PC, and a laptop or a 4 GB card at Epic (Lumen, virtual shadow maps, Nanite)
	// crawled. Measure the hardware once (GameUserSettings.ini keeps the result, so this runs on the first launch only).
	// The frame rate stays uncapped. Never in tools and tests.
	UGameUserSettings* GUS = GEngine != nullptr ? GEngine->GetGameUserSettings() : nullptr;
	if (GUS != nullptr && GUS->GetFrameRateLimit() > 0.f && GUS->GetFrameRateLimit() < FrameLimits()[0])
	{
		// Never below 60 fps (the Graphics tab's lowest limit): an older version offered 30.
		GUS->SetFrameRateLimit(FrameLimits()[0]);
		GUS->ApplyNonResolutionSettings();
		GUS->SaveSettings();
	}
	if (GUS == nullptr || GUS->GetLastCPUBenchmarkResult() >= 0.f) { return; }
	if (GIsEditor || !FApp::CanEverRender() || FApp::IsUnattended() || FApp::IsBenchmarking()) { return; }
	// Only from the engine's default (every group at Epic): a lower quality picked in the Graphics tab by an earlier version
	// is kept.
	if (UniformQuality(*GUS) != 3) { return; }
	GUS->RunHardwareBenchmark();
	// The Graphics tab offers one level for everything: snap the benchmark's per-group levels to their rounded mean.
	int32 Sum = 0;
	const TArray<int32> Bench = QualityGroups(*GUS);
	for (const int32 G : Bench) { Sum += G; }
	GUS->SetOverallScalabilityLevel(FMath::Clamp(FMath::RoundToInt32(static_cast<float>(Sum) / Bench.Num()), 0, 3));
	GUS->ApplySettings(false);
	GUS->SaveSettings();
	UE_LOG(LogHellwalkerRL, Log, TEXT("Graphics: first launch on this PC, benchmark CPU %.0f GPU %.0f -> quality %s."),
		GUS->GetLastCPUBenchmarkResult(), GUS->GetLastGPUBenchmarkResult(), *QualityName(UniformQuality(*GUS)));
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
