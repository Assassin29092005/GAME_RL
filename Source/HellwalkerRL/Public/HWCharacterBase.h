// Hellwalker — shared greybox fighter.
//
// Phases A-B need no art (PLAN §4): each fighter is engine basic shapes on the capsule, posed every
// render frame from its core combat state. The pose IS the frame data — wind-up during startup, the
// strike across the active frames, the return during recovery — so what the player reads on screen is
// exactly what the rules will resolve (legibility, PLAN §2.4a). Colour carries the rest: the red
// killer-move telegraph, blue guard, orange hyper-armor, white parry flash, yellow exposure.
//
// C2: when the fighter's cast is in the project (HWAnimCasts.cpp), the same frame state drives a
// skeletal mesh instead (FHWAnimDriver -> UHWAnimInstance), the colours become an additive overlay on
// that mesh, and the ghoststep trail becomes pose-copied afterimages. No pack, no problem: greybox.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "HWAnimDriver.h"
#include "HWCore/HWFighter.h"
#include "HWCharacterBase.generated.h"

class UHWCombatComponent;
class UHWDuelSubsystem;
class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UHWAnimSet;
class UHWAnimInstance;
class UPoseableMeshComponent;
class UNiagaraComponent;

/** Pose target produced from the frame state. Angles in degrees on the shoulder pivot. */
struct FHWPose
{
	float Pitch = -35.f;
	float Yaw = 25.f;
	float Roll = 0.f;
	float Extend = 0.f;      // blade slide along the arm (thrusts)
	float Lean = 0.f;        // body pitch, positive = forward
	float Drop = 0.f;        // body lowered (exposed / dead)
	float Twist = 0.f;       // body yaw
	float Fall = 0.f;        // body roll (dead)
};

UCLASS(Abstract)
class HELLWALKERRL_API AHWCharacterBase : public ACharacter
{
	GENERATED_BODY()

public:
	AHWCharacterBase();

	/** Walking acceleration and braking of the duelists (cm/s²): effectively instant, as in the simulator (see the constructor). */
	static constexpr float DuelMoveAccel = 60000.f;

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UHWCombatComponent* GetCombat() const { return Combat; }
	UHWDuelSubsystem* GetDuel() const;

	/** Where the blade tip is right now (presentation: sparks, trails). */
	FVector GetBladeTip() const;
	/** The skeletal mesh actually drawn (a cast's retargeted look, else the driver). */
	USkeletalMeshComponent* GetDrawnMesh() const { return CastLook != nullptr ? CastLook.Get() : GetMesh(); }

	/** C2: the loaded cast, or null for the greybox fighter. */
	UHWAnimSet* GetAnimSet() const { return AnimSet; }
	bool IsSkeletal() const { return AnimSet != nullptr; }

	/** Open world: which cast depicts this fighter (set before BeginPlay; -HWGreybox still wins). */
	FName CastOverride;

	/** Tuning: where the hand weapons sit on the hands (console hw.Blade). */
	void SetHandWeaponOffset(int32 Index, const FTransform& Offset);

protected:
	using FPose = FHWPose;

	UStaticMeshComponent* MakePart(const TCHAR* PartName, UStaticMesh* InMesh, USceneComponent* Parent,
		const FVector& Location, const FVector& Scale, const FRotator& Rotation = FRotator::ZeroRotator);
	UMaterialInstanceDynamic* Tint(UStaticMeshComponent* Part, const FLinearColor& Color);

	virtual FPose ComputePose(const HW::FFighter& F, float Alpha) const;
	void ApplyPose(const FPose& P, float DeltaSeconds);
	void UpdateColors(const HW::FFighter& F, float DeltaSeconds);
	void UpdateGhosts(const HW::FFighter& F);
	void SpawnGhost();

	// ---- C2 skeletal presentation
	void SetupSkeletal();
	void TickSkeletal(const HW::FFighter& F, const UHWDuelSubsystem& Duel, float DeltaSeconds);
	/** Additive telegraph colour for the overlay (the greybox's colour language, on a real mesh). */
	FLinearColor TelegraphColor(const HW::FFighter& F) const;
	UStaticMeshComponent* MakeHandWeapon(const TCHAR* PartName, FName Socket, const FVector& Scale, const FTransform& Offset);

	UPROPERTY(VisibleAnywhere, Category = "Hellwalker") TObjectPtr<USceneComponent> VisualRoot;
	UPROPERTY(VisibleAnywhere, Category = "Hellwalker") TObjectPtr<UStaticMeshComponent> Torso;
	UPROPERTY(VisibleAnywhere, Category = "Hellwalker") TObjectPtr<UStaticMeshComponent> Head;
	UPROPERTY(VisibleAnywhere, Category = "Hellwalker") TObjectPtr<UStaticMeshComponent> Accent;
	UPROPERTY(VisibleAnywhere, Category = "Hellwalker") TObjectPtr<USceneComponent> ArmPivot;
	UPROPERTY(VisibleAnywhere, Category = "Hellwalker") TObjectPtr<USceneComponent> BladeSlide;
	UPROPERTY(VisibleAnywhere, Category = "Hellwalker") TObjectPtr<UStaticMeshComponent> Blade;
	UPROPERTY(VisibleAnywhere, Category = "Hellwalker") TObjectPtr<UStaticMeshComponent> BladeAlt;
	UPROPERTY(VisibleAnywhere, Category = "Hellwalker") TObjectPtr<UHWCombatComponent> Combat;

	UPROPERTY() TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> SphereMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> CylinderMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> ConeMesh;
	UPROPERTY() TObjectPtr<UMaterialInterface> BaseMaterial;

	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> BodyMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> HeadMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> AccentMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> BladeMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> BladeAltMID;

	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Ghosts;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> GhostMIDs;
	TArray<float> GhostLife;
	int32 NextGhost = 0;
	int32 LastGhostFrame = -100;

	FLinearColor BodyColor = FLinearColor(0.05f, 0.06f, 0.1f);
	FLinearColor HeadColor = FLinearColor(0.1f, 0.08f, 0.07f);
	FLinearColor AccentColor = FLinearColor(0.5f, 0.04f, 0.03f);
	FLinearColor BladeColor = FLinearColor(0.75f, 0.78f, 0.82f);
	FLinearColor GhostColor = FLinearColor(0.08f, 0.12f, 0.3f);

	UPROPERTY() TObjectPtr<UHWAnimSet> AnimSet;
	UPROPERTY() TObjectPtr<UHWAnimInstance> AnimInstance;
	UPROPERTY() TObjectPtr<UMaterialInterface> EmissiveMaterial;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> OverlayMID;
	UPROPERTY() TArray<TObjectPtr<UPoseableMeshComponent>> GhostMeshes;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> GhostMeshMIDs;
	/** Player: [0] right twin blade, [1] left twin blade, [2] glaive. */
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> HandWeapons;
	/**
	 * With the Thornblade pack: the sword drawn on each hand weapon (same index; null without the pack). The
	 * hand weapon itself is then an invisible frame - its transform is still the blade (tip, trail, hw.Blade).
	 */
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> HandWeaponLooks;
	/** A cast's retargeted body (the Golem), drawn instead of the driver mesh. */
	UPROPERTY() TObjectPtr<USkeletalMeshComponent> CastLook;
	/** The telegraph colour on the Thornblades: an additive rim over the sword's own material. */
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> BladeGlowMID;
	TArray<FTransform> HandWeaponOffsets;
	/** Slash trails (Slash Trail FX pack): on over each attack's active frames, red for killer moves. */
	UPROPERTY() TArray<TObjectPtr<UNiagaraComponent>> Trails;
	FHWAnimDriver AnimDriver;
	FHWAnimFrame AnimFrame;

	FPose CurrentPose;
	float BladeLength = 110.f;
	float TimeAlive = 0.f;
};
