#include "HWCharacterBase.h"

#include "HWAnimInstance.h"
#include "HWAnimTypes.h"
#include "HWCombatComponent.h"
#include "HWDuelSubsystem.h"
#include "HWBuild.h"
#include "HWGlowMaterial.h"
#include "HWSlashFX.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Reference-pose component-space transform of a bone. */
	FTransform RefComponentSpace(const FReferenceSkeleton& Ref, int32 Bone)
	{
		FTransform T = FTransform::Identity;
		while (Bone != INDEX_NONE)
		{
			T = T * Ref.GetRefBonePose()[Bone];
			Bone = Ref.GetParentIndex(Bone);
		}
		return T;
	}

	/** A hand's grip axes in hand-bone space, from the rig's own finger bones. */
	struct FHWGrip
	{
		FVector Knuckles = FVector(10.f, 0.f, 0.f);
		FVector Fingers = FVector::ForwardVector;
		FVector Thumb = FVector::UpVector;
	};

	FHWGrip MeasureGrip(const USkeletalMesh& Mesh, const TCHAR* Side)
	{
		FHWGrip G;
		const FReferenceSkeleton& Ref = Mesh.GetRefSkeleton();
		const int32 Hand = Ref.FindBoneIndex(FName(FString::Printf(TEXT("hand_%s"), Side)));
		const int32 Middle = Ref.FindBoneIndex(FName(FString::Printf(TEXT("middle_01_%s"), Side)));
		const int32 Thumb = Ref.FindBoneIndex(FName(FString::Printf(TEXT("thumb_01_%s"), Side)));
		if (Hand == INDEX_NONE || Middle == INDEX_NONE || Thumb == INDEX_NONE) { return G; }
		const FTransform H = RefComponentSpace(Ref, Hand);
		G.Knuckles = H.InverseTransformPosition(RefComponentSpace(Ref, Middle).GetLocation());
		G.Fingers = G.Knuckles.GetSafeNormal();
		const FVector T = H.InverseTransformPosition(RefComponentSpace(Ref, Thumb).GetLocation());
		G.Thumb = (T - FVector::DotProduct(T, G.Fingers) * G.Fingers).GetSafeNormal();
		return G;
	}

	float HWEaseOut(float U) { const float C = FMath::Clamp(U, 0.f, 1.f); return 1.f - (1.f - C) * (1.f - C); }
	float HWEaseIn(float U) { const float C = FMath::Clamp(U, 0.f, 1.f); return C * C; }
}

AHWCharacterBase::AHWCharacterBase()
{
	PrimaryActorTick.bCanEverTick = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeF(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereF(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderF(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeF(TEXT("/Engine/BasicShapes/Cone.Cone"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialF(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	CubeMesh = CubeF.Object;
	SphereMesh = SphereF.Object;
	CylinderMesh = CylinderF.Object;
	ConeMesh = ConeF.Object;
	BaseMaterial = MaterialF.Object;

	Combat = CreateDefaultSubobject<UHWCombatComponent>(TEXT("Combat"));

	VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
	VisualRoot->SetupAttachment(GetCapsuleComponent());
	ArmPivot = CreateDefaultSubobject<USceneComponent>(TEXT("ArmPivot"));
	ArmPivot->SetupAttachment(VisualRoot);
	BladeSlide = CreateDefaultSubobject<USceneComponent>(TEXT("BladeSlide"));
	BladeSlide->SetupAttachment(ArmPivot);

	// Phantom-trail pool for the ghoststep (Soul leaves a trail behind him — theme research §Sha-Chi & Ghoststep).
	for (int32 I = 0; I < 6; ++I)
	{
		UStaticMeshComponent* G = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Ghost%d"), I));
		G->SetupAttachment(GetCapsuleComponent());
		G->SetUsingAbsoluteLocation(true);
		G->SetUsingAbsoluteRotation(true);
		G->SetUsingAbsoluteScale(true);
		G->SetStaticMesh(CylinderMesh);
		G->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		G->SetGenerateOverlapEvents(false);
		G->SetCastShadow(false);
		G->SetVisibility(false);
		Ghosts.Add(G);
		GhostLife.Add(0.f);
	}

	UCharacterMovementComponent* CMC = GetCharacterMovement();
	CMC->BrakingDecelerationWalking = 2400.f;
	CMC->MaxAcceleration = 3000.f;
	CMC->GravityScale = 1.5f;

	// C2: the character's own mesh component carries the cast. It must never collide: hit detection is
	// the capsule sweep the B0 simulator validated, and a physics asset would widen the hurtbox.
	USkeletalMeshComponent* Body = GetMesh();
	Body->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetGenerateOverlapEvents(false);
	Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
}

UStaticMeshComponent* AHWCharacterBase::MakePart(const TCHAR* PartName, UStaticMesh* InMesh, USceneComponent* Parent,
	const FVector& Location, const FVector& Scale, const FRotator& Rotation)
{
	UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(PartName);
	C->SetupAttachment(Parent);
	C->SetStaticMesh(InMesh);
	C->SetRelativeLocation(Location);
	C->SetRelativeScale3D(Scale);
	C->SetRelativeRotation(Rotation);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetGenerateOverlapEvents(false);
	C->SetCanEverAffectNavigation(false);
	return C;
}

UMaterialInstanceDynamic* AHWCharacterBase::Tint(UStaticMeshComponent* Part, const FLinearColor& Color)
{
	if (Part == nullptr || BaseMaterial == nullptr) { return nullptr; }
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	MID->SetVectorParameterValue(TEXT("Color"), Color);
	MID->SetScalarParameterValue(TEXT("Roughness"), 0.55f);
	Part->SetMaterial(0, MID);
	return MID;
}

void AHWCharacterBase::BeginPlay()
{
	Super::BeginPlay();
	BodyMID = Tint(Torso, BodyColor);
	HeadMID = Tint(Head, HeadColor);
	AccentMID = Tint(Accent, AccentColor);
	BladeMID = Tint(Blade, BladeColor);
	BladeAltMID = Tint(BladeAlt, BladeColor);
	for (UStaticMeshComponent* G : Ghosts)
	{
		GhostMIDs.Add(Tint(G, GhostColor));
	}
	SetupSkeletal();
}

void AHWCharacterBase::SetupSkeletal()
{
	FName CastName = HWCastForSide(Combat != nullptr ? Combat->GetSide() : HW::ESide::Player);
	if (CastName.IsNone()) { return; }
	if (!CastOverride.IsNone()) { CastName = CastOverride; }
	AnimSet = UHWAnimSet::Load(this, CastName);
	if (AnimSet == nullptr) { return; }
	EmissiveMaterial = HWGlowMaterial();

	USkeletalMeshComponent* Body = GetMesh();
	Body->SetSkeletalMeshAsset(AnimSet->Mesh);
	Body->SetRelativeLocation(FVector(0.f, 0.f, -GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()));
	Body->SetRelativeRotation(FRotator(0.f, AnimSet->MeshYaw, 0.f));
	Body->SetRelativeScale3D(FVector(AnimSet->MeshScale));
	Body->SetAnimInstanceClass(UHWAnimInstance::StaticClass());
	Body->PrimaryComponentTick.AddPrerequisite(this, PrimaryActorTick); // the driver sets the frame first
	AnimInstance = Cast<UHWAnimInstance>(Body->GetAnimInstance());
	if (AnimInstance != nullptr) { AnimInstance->Set = AnimSet; }
	if (EmissiveMaterial != nullptr)
	{
		OverlayMID = UMaterialInstanceDynamic::Create(EmissiveMaterial, this);
		OverlayMID->SetVectorParameterValue(TEXT("Color"), FLinearColor::Black);
		Body->SetOverlayMaterial(OverlayMID);
	}
	// A retargeted look (the Golem): the driver keeps animating - frame-locked, measured - but is not drawn;
	// the look is posed from it every frame by the Game Animation Sample's ABP_GenericRetarget.
	if (AnimSet->LookMesh != nullptr)
	{
		if (UClass* Retarget = LoadClass<UAnimInstance>(nullptr, TEXT("/Game/Blueprints/RetargetedCharacters/ABP_GenericRetarget.ABP_GenericRetarget_C")))
		{
			USkeletalMeshComponent* Look = NewObject<USkeletalMeshComponent>(this, TEXT("CastLook"));
			Look->SetSkeletalMeshAsset(AnimSet->LookMesh);
			Look->ComponentTags.Add(AnimSet->LookRetargetTag);
			Look->SetupAttachment(Body);
			Look->SetUsingAbsoluteScale(true);
			Look->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Look->SetAnimInstanceClass(Retarget);
			Look->RegisterComponent();
			Look->SetWorldScale3D(FVector(AnimSet->LookScale));
			Look->PrimaryComponentTick.AddPrerequisite(Body, Body->PrimaryComponentTick);
			Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			Body->SetVisibility(false);
			if (OverlayMID != nullptr) { Look->SetOverlayMaterial(OverlayMID); }
			CastLook = Look;
		}
	}

	// The greybox body goes; the ghoststep trail becomes afterimages of the real pose.
	TArray<USceneComponent*> Parts;
	VisualRoot->GetChildrenComponents(true, Parts);
	for (USceneComponent* P : Parts) { P->SetVisibility(false); }
	for (int32 I = 0; I < Ghosts.Num(); ++I)
	{
		UPoseableMeshComponent* G = NewObject<UPoseableMeshComponent>(this, *FString::Printf(TEXT("GhostMesh%d"), I));
		G->SetSkinnedAssetAndUpdate(AnimSet->Mesh);
		G->SetUsingAbsoluteLocation(true);
		G->SetUsingAbsoluteRotation(true);
		G->SetUsingAbsoluteScale(true);
		G->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		G->SetCastShadow(false);
		G->SetVisibility(false);
		G->SetupAttachment(GetCapsuleComponent());
		G->RegisterComponent();
		UMaterialInstanceDynamic* MID = EmissiveMaterial != nullptr ? UMaterialInstanceDynamic::Create(EmissiveMaterial, this) : nullptr;
		if (MID != nullptr)
		{
			MID->SetVectorParameterValue(TEXT("Color"), GhostColor);
			for (int32 M = 0; M < G->GetNumMaterials(); ++M) { G->SetMaterial(M, MID); }
		}
		GhostMeshes.Add(G);
		GhostMeshMIDs.Add(MID);
	}

	if (AnimSet->bHandBlades)
	{
		// The grip is measured from the rig, not guessed: the twin blades run out of the fists along the
		// fingers (the Fighter pack's strikes are punches, so every punch reads as a stab), the glaive runs
		// through the fist along the thumb axis. Tunable live with hw.Blade.
		const FHWGrip R = MeasureGrip(*AnimSet->Mesh, TEXT("r"));
		const FHWGrip L = MeasureGrip(*AnimSet->Mesh, TEXT("l"));
		HandWeaponOffsets = {
			FTransform(FRotationMatrix::MakeFromXZ(R.Fingers, R.Thumb).Rotator(), R.Knuckles * 0.6f),
			FTransform(FRotationMatrix::MakeFromXZ(L.Fingers, L.Thumb).Rotator(), L.Knuckles * 0.6f),
			FTransform(FRotationMatrix::MakeFromXZ(R.Thumb, R.Fingers).Rotator(), R.Knuckles * 0.5f) };
		HandWeapons.Add(MakeHandWeapon(TEXT("HandBladeR"), TEXT("hand_r"), FVector(0.7f, 0.04f, 0.09f), HandWeaponOffsets[0]));
		HandWeapons.Add(MakeHandWeapon(TEXT("HandBladeL"), TEXT("hand_l"), FVector(0.7f, 0.04f, 0.09f), HandWeaponOffsets[1]));
		HandWeapons.Add(MakeHandWeapon(TEXT("HandGlaive"), TEXT("hand_r"), FVector(2.2f, 0.05f, 0.1f), HandWeaponOffsets[2]));
		for (int32 I = 0; I < HandWeapons.Num(); ++I) { SetHandWeaponOffset(I, HandWeaponOffsets[I]); }
	}
	if (UNiagaraSystem* TrailSystem = HWLoadNiagara(AnimSet->TrailSystem))
	{
		auto MakeTrail = [this, TrailSystem](USceneComponent* Parent, FName Socket)
		{
			UNiagaraComponent* T = NewObject<UNiagaraComponent>(this);
			T->SetAsset(TrailSystem);
			T->SetAutoActivate(false);
			T->SetupAttachment(Parent, Socket);
			T->SetUsingAbsoluteScale(true);
			T->RegisterComponent();
			const FLinearColor C = AnimSet->TrailColor * 3.f;
			T->SetVariableVec3(TEXT("User.Color_"), FVector(C.R, C.G, C.B));
			T->SetVariableFloat(TEXT("User.Trail Width"), AnimSet->TrailWidth);
			T->SetVariableFloat(TEXT("User.Slash Width"), AnimSet->TrailWidth);
			Trails.Add(T);
		};
		if (HandWeapons.Num() > 0)
		{
			for (UStaticMeshComponent* W : HandWeapons) { MakeTrail(W, NAME_None); }
		}
		else
		{
			for (const FName& S : AnimSet->TrailSockets)
			{
				if (GetMesh()->DoesSocketExist(S)) { MakeTrail(GetMesh(), S); }
			}
		}
	}
	AnimDriver.Reset();
}

UStaticMeshComponent* AHWCharacterBase::MakeHandWeapon(const TCHAR* PartName, FName Socket, const FVector& Scale, const FTransform& Offset)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, PartName);
	C->SetStaticMesh(CubeMesh);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetGenerateOverlapEvents(false);
	C->SetupAttachment(GetMesh(), Socket);
	C->RegisterComponent();
	C->SetRelativeTransform(Offset);
	C->SetWorldScale3D(Scale); // the cube is 100 cm: size it in world units whatever the mesh scale
	C->SetMaterial(0, BladeMID != nullptr ? static_cast<UMaterialInterface*>(BladeMID) : BaseMaterial.Get());

	// The Thornblade pack: the cube stays as the blade's frame (not drawn) and the sword is drawn on it,
	// pommel behind the fist, blade along the frame's +X (placed by SetHandWeaponOffset).
	UStaticMeshComponent* Look = nullptr;
	if (UStaticMesh* Sword = HWBuild::OptionalMesh(TEXT("/Game/Sword/Sword/SM_Sword.SM_Sword")))
	{
		C->SetStaticMesh(nullptr);
		Look = NewObject<UStaticMeshComponent>(this, *(FString(PartName) + TEXT("Look")));
		Look->SetStaticMesh(Sword);
		Look->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Look->SetGenerateOverlapEvents(false);
		Look->SetupAttachment(C);
		Look->SetUsingAbsoluteScale(true); // the frame is scaled non-uniformly; the sword keeps its proportions
		Look->RegisterComponent();
		if (BladeGlowMID == nullptr)
		{
			if (UMaterialInterface* Glow = HWGlowMaterial()) { BladeGlowMID = UMaterialInstanceDynamic::Create(Glow, this); }
		}
		if (BladeGlowMID != nullptr) { Look->SetOverlayMaterial(BladeGlowMID); }
	}
	HandWeaponLooks.Add(Look);
	return C;
}

void AHWCharacterBase::SetHandWeaponOffset(int32 Index, const FTransform& Offset)
{
	if (!HandWeapons.IsValidIndex(Index) || HandWeapons[Index] == nullptr) { return; }
	if (HandWeaponOffsets.IsValidIndex(Index)) { HandWeaponOffsets[Index] = Offset; }
	UStaticMeshComponent* C = HandWeapons[Index];
	const FVector World = C->GetComponentScale();
	// The grip sits in the fist: push the cube most of its half-length along its own +X (in hand space).
	const float MeshScale = FMath::Max(AnimSet != nullptr ? AnimSet->MeshScale : 1.f, 0.01f);
	const float Half = static_cast<float>(World.X) * 50.f / MeshScale;
	C->SetRelativeRotation(Offset.GetRotation());
	C->SetRelativeLocation(Offset.GetLocation() + Offset.GetRotation().RotateVector(FVector(Half * 0.8f, 0.f, 0.f)));
	C->SetWorldScale3D(World);

	if (HandWeaponLooks.IsValidIndex(Index) && HandWeaponLooks[Index] != nullptr)
	{
		// The sword is sized from the frame: pommel to tip a little longer than the frame's blade (the grip is in
		// the fist), capped so the glaive becomes a greatsword rather than a pole. Its pivot is the pommel and it
		// points +Z; the frame's centre is 0.8 half-lengths ahead of the fist, in frame units of World.X cm.
		UStaticMeshComponent* Look = HandWeaponLooks[Index];
		const float SwordLength = FMath::Max(static_cast<float>(Look->GetStaticMesh()->GetBoundingBox().GetSize().Z), 1.f);
		const float FrameX = FMath::Max(static_cast<float>(World.X), 0.001f);
		const float Length = FMath::Min(FrameX * 100.f * 1.15f, 190.f);
		const float Back = 0.11f * Length; // half the grip behind the knuckles
		Look->SetRelativeLocation(FVector(-(40.f + Back / FrameX), 0.f, 0.f));
		Look->SetRelativeRotation(FRotationMatrix::MakeFromZX(FVector::ForwardVector, FVector::UpVector).Rotator());
		Look->SetWorldScale3D(FVector(Length / SwordLength));
	}
}

UHWDuelSubsystem* AHWCharacterBase::GetDuel() const
{
	const UWorld* World = GetWorld();
	return World != nullptr ? World->GetSubsystem<UHWDuelSubsystem>() : nullptr;
}

FVector AHWCharacterBase::GetBladeTip() const
{
	if (IsSkeletal())
	{
		for (int32 I = HandWeapons.Num() - 1; I >= 0; --I)
		{
			const UStaticMeshComponent* W = HandWeapons[I];
			if (W != nullptr && W->IsVisible())
			{
				return W->GetComponentLocation() + W->GetForwardVector() * (W->GetComponentScale().X * 50.f);
			}
		}
		const USkeletalMeshComponent* Body = GetMesh();
		if (Body != nullptr && !AnimSet->TrailSocket.IsNone() && Body->DoesSocketExist(AnimSet->TrailSocket))
		{
			return Body->GetSocketLocation(AnimSet->TrailSocket);
		}
		return GetActorLocation();
	}
	const UStaticMeshComponent* B = (BladeAlt != nullptr && BladeAlt->IsVisible()) ? BladeAlt.Get() : Blade.Get();
	if (B == nullptr) { return GetActorLocation(); }
	const float Half = B->GetComponentScale().X * 50.f;
	return B->GetComponentLocation() + B->GetForwardVector() * Half;
}

void AHWCharacterBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TimeAlive += DeltaSeconds;

	for (int32 I = 0; I < Ghosts.Num(); ++I)
	{
		if (GhostLife[I] <= 0.f) { continue; }
		GhostLife[I] -= DeltaSeconds;
		UStaticMeshComponent* G = Ghosts[I];
		if (GhostLife[I] <= 0.f)
		{
			G->SetVisibility(false);
			if (GhostMeshes.IsValidIndex(I)) { GhostMeshes[I]->SetVisibility(false); }
			continue;
		}
		const float K = GhostLife[I] / 0.35f;
		if (GhostMeshes.IsValidIndex(I))
		{
			if (GhostMeshMIDs.IsValidIndex(I) && GhostMeshMIDs[I] != nullptr)
			{
				GhostMeshMIDs[I]->SetVectorParameterValue(TEXT("Color"), FMath::Lerp(FLinearColor::Black, GhostColor * 2.f, K * K));
			}
			continue;
		}
		G->SetWorldScale3D(Torso != nullptr ? Torso->GetComponentScale() * FMath::Lerp(0.6f, 1.f, K) : FVector(K));
		if (GhostMIDs.IsValidIndex(I) && GhostMIDs[I] != nullptr)
		{
			GhostMIDs[I]->SetVectorParameterValue(TEXT("Color"), FMath::Lerp(FLinearColor::Black, GhostColor, K));
		}
	}

	UHWDuelSubsystem* Duel = GetDuel();
	if (Duel == nullptr || Duel->GetEncounter() == nullptr || Combat == nullptr) { return; }
	const HW::FFighter& F = Duel->GetFighterState(Combat->GetSide());
	if (IsSkeletal())
	{
		TickSkeletal(F, *Duel, DeltaSeconds);
		UpdateGhosts(F);
		return;
	}
	ApplyPose(ComputePose(F, Duel->GetFrameAlpha()), DeltaSeconds);
	UpdateColors(F, DeltaSeconds);
	UpdateGhosts(F);

	if (BladeAlt != nullptr && Blade != nullptr)
	{
		const bool bAlt = F.Weapon == 1;
		Blade->SetVisibility(!bAlt);
		BladeAlt->SetVisibility(bAlt);
	}
}

FHWPose AHWCharacterBase::ComputePose(const HW::FFighter& F, float Alpha) const
{
	FPose Rest;
	Rest.Lean = FMath::Sin(TimeAlive * 2.1f) * 1.5f;

	auto Blend = [](const FPose& A, const FPose& B, float U)
	{
		FPose O;
		O.Pitch = FMath::Lerp(A.Pitch, B.Pitch, U);
		O.Yaw = FMath::Lerp(A.Yaw, B.Yaw, U);
		O.Roll = FMath::Lerp(A.Roll, B.Roll, U);
		O.Extend = FMath::Lerp(A.Extend, B.Extend, U);
		O.Lean = FMath::Lerp(A.Lean, B.Lean, U);
		O.Drop = FMath::Lerp(A.Drop, B.Drop, U);
		O.Twist = FMath::Lerp(A.Twist, B.Twist, U);
		O.Fall = FMath::Lerp(A.Fall, B.Fall, U);
		return O;
	};
	auto Make = [](float Pitch, float Yaw, float Roll, float Extend, float Lean, float Twist)
	{
		FPose P;
		P.Pitch = Pitch; P.Yaw = Yaw; P.Roll = Roll; P.Extend = Extend; P.Lean = Lean; P.Twist = Twist;
		return P;
	};
	const float Shake = FMath::Sin(TimeAlive * 55.f) * 3.f;

	switch (F.State)
	{
	case HW::EFighterState::Acting:
	{
		const HW::FMoveData& M = F.CurrentMove();
		const float T = static_cast<float>(F.T) + Alpha;
		switch (M.Kind)
		{
		case HW::EMoveKind::Attack:
		{
			FPose Wind;
			FPose Strike;
			const bool bThrust = (M.Symbol == HW::ESym::BKiller && M.Coverage == HW::ECoverage::Straight);
			if (M.Coverage == HW::ECoverage::Grab)
			{
				Wind = Make(30.f, -25.f, 0.f, -25.f, -5.f, -10.f);
				Strike = Make(-10.f, 0.f, 0.f, 60.f, 18.f, 5.f);
			}
			else if (bThrust)
			{
				Wind = Make(5.f, 0.f, 0.f, -55.f, -8.f, -15.f);
				Strike = Make(0.f, 0.f, 0.f, 85.f, 14.f, 5.f);
			}
			else if (M.Coverage == HW::ECoverage::SweepLeft)
			{
				// Covers the attacker's right = the defender's left: the blade ends on the right.
				Wind = Make(10.f, -120.f, 85.f, 0.f, -4.f, -28.f);
				Strike = Make(0.f, 118.f, 85.f, 10.f, 10.f, 28.f);
			}
			else if (M.Coverage == HW::ECoverage::SweepRight)
			{
				Wind = Make(10.f, 120.f, -85.f, 0.f, -4.f, 28.f);
				Strike = Make(0.f, -118.f, -85.f, 10.f, 10.f, -28.f);
			}
			else if (M.Symbol == HW::ESym::Light)
			{
				// Player light chain alternates its line so a string reads as a string.
				const bool bLeft = (F.ChainDepth % 2) == 1;
				Wind = Make(70.f, bLeft ? 70.f : -60.f, bLeft ? -40.f : 40.f, -5.f, -3.f, bLeft ? 12.f : -12.f);
				Strike = Make(-40.f, bLeft ? -50.f : 55.f, bLeft ? -40.f : 40.f, 12.f, 10.f, bLeft ? -12.f : 12.f);
			}
			else
			{
				// Overhead chop (fast / heavy / feints / delayed heavy).
				Wind = Make(118.f, 12.f, 0.f, -12.f, -7.f, -6.f);
				Strike = Make(-58.f, -4.f, 0.f, 12.f, 13.f, 4.f);
			}

			const float S = static_cast<float>(FMath::Max(M.Startup, 1));
			const float A = static_cast<float>(FMath::Max(M.Active, 1));
			const float R = static_cast<float>(FMath::Max(M.Recovery, 1));
			if (T < S)
			{
				float U = HWEaseOut(T / FMath::Min(S, 20.f)); // held wind-ups (delayed heavy) reach the top and hold
				FPose P = Blend(Rest, Wind, U);
				if (M.Symbol == HW::ESym::BFeint && M.FakeImpactFrame >= 0)
				{
					// The fake: a partial swing that "arrives" at the fake impact frame, then pulls back up.
					const float D = FMath::Abs(T - static_cast<float>(M.FakeImpactFrame));
					const float Bump = FMath::Clamp(1.f - D / 5.f, 0.f, 1.f);
					P = Blend(P, Strike, 0.55f * Bump);
				}
				if (M.FakeImpactFrame >= 0 && M.Symbol == HW::ESym::BHeavy && T > static_cast<float>(M.FakeImpactFrame))
				{
					P.Pitch += FMath::Sin(TimeAlive * 30.f) * 2.f; // the hold trembles
				}
				return P;
			}
			if (T < S + A)
			{
				return Blend(Wind, Strike, HWEaseOut((T - S) / A));
			}
			return Blend(Strike, Rest, HWEaseIn((T - S - A) / R));
		}
		case HW::EMoveKind::Parry:
			return Make(50.f, -40.f, 70.f, 0.f, -4.f, -10.f);
		case HW::EMoveKind::Guard:
			return Make(78.f, -15.f, 88.f, 0.f, -3.f, 0.f);
		case HW::EMoveKind::Counter:
			return Make(30.f, 0.f, 90.f, -15.f, -6.f, -20.f);
		case HW::EMoveKind::Step:
		{
			FPose P = Make(-60.f, 65.f, 0.f, 0.f, 0.f, 0.f);
			switch (M.Dir)
			{
			case HW::EDir::Forward: P.Lean = 20.f; break;
			case HW::EDir::Back:    P.Lean = -14.f; break;
			case HW::EDir::Left:    P.Twist = -22.f; P.Lean = 8.f; break;
			case HW::EDir::Right:   P.Twist = 22.f; P.Lean = 8.f; break;
			default: break;
			}
			return P;
		}
		case HW::EMoveKind::Locomotion:
		{
			FPose P = Rest;
			P.Lean = M.Dir == HW::EDir::Back ? -4.f : 7.f;
			return P;
		}
		case HW::EMoveKind::Switch:
		{
			FPose P = Rest;
			P.Roll = 360.f * FMath::Clamp(T / static_cast<float>(FMath::Max(M.Active, 1)), 0.f, 1.f);
			return P;
		}
		default:
			return Rest;
		}
	}
	case HW::EFighterState::Hitstun:
		return Make(20.f, 30.f, 0.f, 0.f, -18.f + Shake, 8.f);
	case HW::EFighterState::Blockstun:
		return Make(78.f, -15.f, 88.f, 0.f, -9.f + Shake * 0.5f, 0.f);
	case HW::EFighterState::Stagger:
		return Make(60.f, 75.f, 20.f, -10.f, -22.f + Shake, 15.f);
	case HW::EFighterState::GuardBroken:
	{
		FPose P = Make(-80.f, 20.f, 0.f, 0.f, 25.f, 0.f);
		P.Drop = 35.f;
		return P;
	}
	case HW::EFighterState::Dead:
	{
		FPose P = Make(-85.f, 10.f, 0.f, 0.f, 10.f, 0.f);
		P.Drop = 55.f;
		P.Fall = 80.f;
		return P;
	}
	default:
		if (F.bGuardHeld)
		{
			return Make(78.f, -15.f, 88.f, 0.f, -3.f, 0.f);
		}
		return Rest;
	}
}

void AHWCharacterBase::ApplyPose(const FPose& P, float DeltaSeconds)
{
	const float K = 1.f - FMath::Exp(-DeltaSeconds * 32.f); // snappy: a 4-frame active window must read
	CurrentPose.Pitch = FMath::Lerp(CurrentPose.Pitch, P.Pitch, K);
	CurrentPose.Yaw = FMath::Lerp(CurrentPose.Yaw, P.Yaw, K);
	CurrentPose.Roll = FMath::Lerp(CurrentPose.Roll, P.Roll, K);
	CurrentPose.Extend = FMath::Lerp(CurrentPose.Extend, P.Extend, K);
	CurrentPose.Lean = FMath::Lerp(CurrentPose.Lean, P.Lean, K);
	CurrentPose.Drop = FMath::Lerp(CurrentPose.Drop, P.Drop, K * 0.5f);
	CurrentPose.Twist = FMath::Lerp(CurrentPose.Twist, P.Twist, K);
	CurrentPose.Fall = FMath::Lerp(CurrentPose.Fall, P.Fall, K * 0.3f);

	if (ArmPivot != nullptr) { ArmPivot->SetRelativeRotation(FRotator(CurrentPose.Pitch, CurrentPose.Yaw, CurrentPose.Roll)); }
	if (BladeSlide != nullptr) { BladeSlide->SetRelativeLocation(FVector(CurrentPose.Extend, 0.f, 0.f)); }
	if (VisualRoot != nullptr)
	{
		VisualRoot->SetRelativeRotation(FRotator(-CurrentPose.Lean, CurrentPose.Twist, CurrentPose.Fall));
		VisualRoot->SetRelativeLocation(FVector(0.f, 0.f, -CurrentPose.Drop));
	}
}

void AHWCharacterBase::UpdateColors(const HW::FFighter& F, float DeltaSeconds)
{
	FLinearColor Body = BodyColor;
	FLinearColor BladeC = BladeColor;
	const float Pulse = 0.5f + 0.5f * FMath::Sin(TimeAlive * 26.f);

	if (F.State == HW::EFighterState::Acting)
	{
		const HW::FMoveData& M = F.CurrentMove();
		if (M.IsAttack() && M.bUnblockable && F.T < M.Startup + M.Active)
		{
			// Killer move: the red telegraph. Cannot be blocked or parried — step.
			Body = FMath::Lerp(BodyColor, FLinearColor(1.f, 0.03f, 0.02f), 0.55f + 0.45f * Pulse);
			BladeC = FLinearColor(1.f, 0.1f, 0.05f);
		}
		if (F.HasHyperArmor())
		{
			Body = FMath::Lerp(Body, FLinearColor(0.9f, 0.4f, 0.05f), 0.35f);
		}
		if (F.IsParryLive() || F.IsCounterLive())
		{
			BladeC = FLinearColor(0.6f, 0.9f, 1.f);
		}
		if (F.IsInvulnerable())
		{
			Body = FMath::Lerp(Body, GhostColor, 0.6f);
		}
	}
	if (F.IsGuarding())
	{
		BladeC = FLinearColor(0.2f, 0.45f, 1.f);
	}
	if (F.State == HW::EFighterState::GuardBroken)
	{
		Body = FMath::Lerp(BodyColor, FLinearColor(1.f, 0.85f, 0.1f), 0.4f + 0.4f * Pulse);
	}
	if (Combat != nullptr && Combat->FlashFrames > 0)
	{
		const float K = FMath::Clamp(static_cast<float>(Combat->FlashFrames) / 8.f, 0.f, 1.f);
		Body = FMath::Lerp(Body, Combat->FlashColor, K);
		BladeC = FMath::Lerp(BladeC, Combat->FlashColor, K);
	}
	if (BodyMID != nullptr) { BodyMID->SetVectorParameterValue(TEXT("Color"), Body); }
	if (BladeMID != nullptr) { BladeMID->SetVectorParameterValue(TEXT("Color"), BladeC); }
	if (BladeAltMID != nullptr) { BladeAltMID->SetVectorParameterValue(TEXT("Color"), BladeC); }
	// On the Thornblades the colour is a rim only: none at rest (the steel shows), the state colours glow through.
	if (BladeGlowMID != nullptr)
	{
		const float Away = FMath::Clamp((FMath::Abs(BladeC.R - BladeColor.R) + FMath::Abs(BladeC.G - BladeColor.G) + FMath::Abs(BladeC.B - BladeColor.B)) * 2.f, 0.f, 1.f);
		BladeGlowMID->SetVectorParameterValue(TEXT("Color"), BladeC * (1.2f * Away));
	}
	(void)DeltaSeconds;
}

void AHWCharacterBase::UpdateGhosts(const HW::FFighter& F)
{
	if (F.State != HW::EFighterState::Acting || F.CurrentMove().Kind != HW::EMoveKind::Step) { return; }
	UHWDuelSubsystem* Duel = GetDuel();
	const int32 Frame = Duel != nullptr ? Duel->GetDuelFrame() : 0;
	if (Frame - LastGhostFrame >= 4)
	{
		LastGhostFrame = Frame;
		SpawnGhost();
	}
}

void AHWCharacterBase::SpawnGhost()
{
	if (GhostMeshes.Num() > 0)
	{
		UPoseableMeshComponent* G = GhostMeshes[NextGhost];
		USkeletalMeshComponent* Body = GetMesh();
		G->SetWorldTransform(Body->GetComponentTransform());
		G->CopyPoseFromSkeletalComponent(Body);
		G->SetVisibility(true);
		GhostLife[NextGhost] = 0.35f;
		NextGhost = (NextGhost + 1) % GhostMeshes.Num();
		return;
	}
	if (Ghosts.Num() == 0 || Torso == nullptr) { return; }
	UStaticMeshComponent* G = Ghosts[NextGhost];
	G->SetWorldLocationAndRotation(Torso->GetComponentLocation(), Torso->GetComponentRotation());
	G->SetWorldScale3D(Torso->GetComponentScale());
	G->SetVisibility(true);
	GhostLife[NextGhost] = 0.35f;
	NextGhost = (NextGhost + 1) % Ghosts.Num();
}

void AHWCharacterBase::TickSkeletal(const HW::FFighter& F, const UHWDuelSubsystem& Duel, float DeltaSeconds)
{
	const bool bRunning = Duel.GetState() == EHWEncounterState::Running;
	FHWAnimInputs In;
	In.Fighter = &F;
	In.DuelFrame = Duel.GetDuelFrame();
	In.FrameAlpha = bRunning ? FMath::Clamp(Duel.GetFrameAlpha(), 0.f, 1.f) : 0.f;
	In.bDuelRunning = bRunning;
	In.DeltaSeconds = DeltaSeconds;
	const FVector Fwd = GetActorForwardVector();
	const FVector Right = GetActorRightVector();
	const FVector V = GetVelocity();
	In.LocalVelocity = FVector2D(FVector::DotProduct(V, Fwd), FVector::DotProduct(V, Right));
	if (const AHWCharacterBase* Other = Duel.GetFighterActor(HW::Opponent(Combat->GetSide())))
	{
		const FVector D = Other->GetActorLocation() - GetActorLocation();
		In.OpponentLocal = FVector2D(FVector::DotProduct(D, Fwd), FVector::DotProduct(D, Right));
	}
	AnimDriver.Tick(*AnimSet, In, AnimFrame);
	if (AnimInstance != nullptr) { AnimInstance->SetFrame(AnimFrame); }

	if (OverlayMID != nullptr) { OverlayMID->SetVectorParameterValue(TEXT("Color"), TelegraphColor(F)); }

	// Hand weapons: the twin blades or the glaive (C1), tinted like the greybox blade.
	if (HandWeapons.Num() == 3)
	{
		const bool bGlaive = F.Weapon == 1;
		HandWeapons[0]->SetVisibility(!bGlaive, true); // (propagated: the Thornblade drawn on each frame follows)
		HandWeapons[1]->SetVisibility(!bGlaive, true);
		HandWeapons[2]->SetVisibility(bGlaive, true);
		UpdateColors(F, DeltaSeconds);
	}

	// Trails over the swing: from just before impact to just after the active frames.
	bool bSwing = false;
	bool bKiller = false;
	if (F.State == HW::EFighterState::Acting)
	{
		const HW::FMoveData& M = F.CurrentMove();
		bSwing = M.IsAttack() && F.T >= M.Startup - 4 && F.T <= M.Startup + M.Active + 3;
		bKiller = M.bUnblockable;
	}
	for (int32 I = 0; I < Trails.Num(); ++I)
	{
		UNiagaraComponent* T = Trails[I];
		const bool bWant = bSwing && (!HandWeapons.IsValidIndex(I) || HandWeapons[I]->IsVisible());
		if (bWant && !T->IsActive())
		{
			const FLinearColor C = (bKiller ? FLinearColor(1.f, 0.05f, 0.02f) : AnimSet->TrailColor) * 3.f;
			T->SetVariableVec3(TEXT("User.Color_"), FVector(C.R, C.G, C.B));
			T->Activate(true);
		}
		else if (!bWant && T->IsActive())
		{
			T->Deactivate();
		}
	}
}

FLinearColor AHWCharacterBase::TelegraphColor(const HW::FFighter& F) const
{
	const float Pulse = 0.5f + 0.5f * FMath::Sin(TimeAlive * 26.f);
	FLinearColor C = FLinearColor::Black;
	auto Add = [&C](const FLinearColor& Tint, float K) { C += Tint * K; };
	if (F.State == HW::EFighterState::Acting)
	{
		const HW::FMoveData& M = F.CurrentMove();
		if (M.IsAttack() && M.bUnblockable && F.T < M.Startup + M.Active)
		{
			Add(FLinearColor(1.f, 0.04f, 0.02f), 0.3f + 0.3f * Pulse); // killer move: step, do not block
		}
		if (F.HasHyperArmor()) { Add(FLinearColor(1.f, 0.45f, 0.05f), 0.1f); }
		if (F.IsParryLive() || F.IsCounterLive()) { Add(FLinearColor(0.5f, 0.9f, 1.f), 0.15f); }
		if (F.IsInvulnerable()) { Add(GhostColor, 0.8f); }
	}
	if (F.IsGuarding()) { Add(FLinearColor(0.15f, 0.35f, 1.f), 0.08f); }
	if (F.State == HW::EFighterState::GuardBroken) { Add(FLinearColor(1.f, 0.8f, 0.1f), 0.15f + 0.15f * Pulse); }
	if (Combat != nullptr && Combat->FlashFrames > 0)
	{
		Add(Combat->FlashColor, 0.35f * FMath::Clamp(static_cast<float>(Combat->FlashFrames) / 8.f, 0.f, 1.f));
	}
	C.A = 1.f;
	return C;
}
