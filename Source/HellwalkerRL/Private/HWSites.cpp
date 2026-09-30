#include "HWSites.h"

#include "HWBuild.h"
#include "HWGlowMaterial.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"

namespace
{
	const FLinearColor Stone(0.09f, 0.09f, 0.1f);
	const FLinearColor DarkStone(0.035f, 0.035f, 0.045f);
	const FLinearColor Iron(0.05f, 0.05f, 0.055f);
	const FLinearColor Crimson(0.45f, 0.02f, 0.02f);
	const FLinearColor Bronze(0.3f, 0.18f, 0.06f);
	const FLinearColor Wood(0.08f, 0.04f, 0.025f);

	UPointLightComponent* MakeLight(AActor* Owner, USceneComponent* Parent, const FVector& At, float Candelas, float Radius, const FColor& Color)
	{
		UPointLightComponent* L = NewObject<UPointLightComponent>(Owner);
		L->SetupAttachment(Parent);
		L->SetMobility(EComponentMobility::Movable);
		L->SetRelativeLocation(At);
		L->IntensityUnits = ELightUnits::Candelas;
		L->SetIntensity(Candelas);
		L->SetAttenuationRadius(Radius);
		L->SetLightColor(Color);
		L->SetCastShadows(false);
		L->RegisterComponent();
		return L;
	}
}

// =================================================================================================
// Bell

AHWBell::AHWBell()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

void AHWBell::Setup(const FHWSite& Site)
{
	BellIndex = Site.Index;
	Title = Site.Title;
	SetActorLocation(FVector(Site.Pos.X, Site.Pos.Y, Site.Height));

	// A stone dais, four lacquered posts, a sweeping roof, the bronze bell and a brazier.
	HWBuild::Part(this, Root, HWBuild::Cylinder(), FVector(0.f, 0.f, 0.f), FVector(6.f, 6.f, 0.4f), FRotator::ZeroRotator, Stone, true, 0.9f);
	for (int32 I = 0; I < 4; ++I)
	{
		const FVector P((I % 2 == 0 ? 1.f : -1.f) * 190.f, (I < 2 ? 1.f : -1.f) * 190.f, 0.f);
		HWBuild::Part(this, Root, HWBuild::Cylinder(), P + FVector(0.f, 0.f, 180.f), FVector(0.28f, 0.28f, 3.4f), FRotator::ZeroRotator, Crimson, true, 0.6f);
	}
	HWBuild::Part(this, Root, HWBuild::Cube(), FVector(0.f, 0.f, 355.f), FVector(5.2f, 5.2f, 0.25f), FRotator(0.f, 45.f, 0.f), Wood, false, 0.7f);
	HWBuild::Part(this, Root, HWBuild::Cone(), FVector(0.f, 0.f, 420.f), FVector(6.2f, 6.2f, 1.2f), FRotator(0.f, 45.f, 0.f), DarkStone, false, 0.8f);

	BellPivot = NewObject<USceneComponent>(this);
	BellPivot->SetupAttachment(Root);
	BellPivot->SetRelativeLocation(FVector(0.f, 0.f, 330.f));
	BellPivot->RegisterComponent();
	HWBuild::Part(this, BellPivot, HWBuild::Cylinder(), FVector(0.f, 0.f, -60.f), FVector(0.95f, 0.95f, 1.1f), FRotator::ZeroRotator, Bronze, false, 0.35f);
	HWBuild::Part(this, BellPivot, HWBuild::Cylinder(), FVector(0.f, 0.f, -112.f), FVector(1.15f, 1.15f, 0.12f), FRotator::ZeroRotator, Bronze, false, 0.35f);

	HWBuild::Part(this, Root, HWBuild::Cylinder(), FVector(260.f, 0.f, 50.f), FVector(0.6f, 0.6f, 0.9f), FRotator::ZeroRotator, Iron, true, 0.7f);
	Flame = HWBuild::Part(this, Root, HWBuild::Sphere(), FVector(260.f, 0.f, 110.f), FVector(0.45f, 0.45f, 0.6f), FRotator::ZeroRotator, FLinearColor::Black, false);
	if (UMaterialInterface* Glow = HWGlowMaterial()) { Flame->SetMaterial(0, UMaterialInstanceDynamic::Create(Glow, this)); }
	Light = MakeLight(this, Root, FVector(260.f, 0.f, 150.f), 4.f, 1600.f, FColor(255, 150, 70));
	Fire = HWBuild::Fire(this, Root, FVector(260.f, 0.f, 92.f), 70.f, true, false);
	SetLit(false);
}

void AHWBell::SetLit(bool bLit)
{
	bIsLit = bLit;
	if (Light != nullptr) { Light->SetIntensity(bLit ? 90.f : 3.f); }
	if (Fire != nullptr)
	{
		if (bLit) { Fire->Activate(true); }
		else { Fire->Deactivate(); }
		if (Flame != nullptr) { Flame->SetVisibility(!bLit); } // the ember glows only while it waits
	}
	if (Flame != nullptr)
	{
		if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Flame->GetMaterial(0)))
		{
			MID->SetVectorParameterValue(TEXT("Color"), bLit ? FLinearColor(6.f, 2.2f, 0.5f) : FLinearColor(0.3f, 0.08f, 0.03f));
		}
	}
}

void AHWBell::Ring()
{
	Swing = 1.f;
	SetLit(true);
}

FVector AHWBell::WakePoint() const
{
	return GetActorLocation() + FVector(-350.f, -150.f, 0.f);
}

void AHWBell::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Clock += DeltaSeconds;
	Swing = FMath::Max(0.f, Swing - DeltaSeconds * 0.35f);
	if (BellPivot != nullptr)
	{
		BellPivot->SetRelativeRotation(FRotator(0.f, 0.f, 18.f * Swing * FMath::Sin(Clock * 6.f)));
	}
	if (Light != nullptr && bIsLit)
	{
		Light->SetIntensity(90.f * (0.85f + 0.15f * FMath::Sin(Clock * 11.f) * FMath::Sin(Clock * 4.3f)));
	}
}

// =================================================================================================
// Shrine

AHWShrine::AHWShrine()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

FVector AHWShrine::Inward() const { return -FVector(Gate.X, Gate.Y, 0.0); }
float AHWShrine::ArenaYaw() const { return static_cast<float>(Inward().Rotation().Yaw); }
FVector AHWShrine::PlayerSpawn() const { return Center() - Inward() * 450.f + FVector(0.f, 0.f, 100.f); }
FVector AHWShrine::BossSpawn() const { return Center() + Inward() * 450.f + FVector(0.f, 0.f, 140.f); }
FVector AHWShrine::GatePoint() const { return Center() - Inward() * (WallRadius - 150.f); }
FVector AHWShrine::ExitPoint() const { return Center() - Inward() * (WallRadius + 500.f) + FVector(0.f, 0.f, 50.f); }

void AHWShrine::Setup(const FHWSite& Site, const FHWShrineSpec& InSpec, const FVector2D& GateDir)
{
	ShrineIndex = Site.Index;
	Spec = InSpec;
	Gate = GateDir.GetSafeNormal();
	SetActorLocation(FVector(Site.Pos.X, Site.Pos.Y, Site.Height));
	const float GateYaw = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Gate.Y, Gate.X)));
	// The greybox is the shrine's body: floor and wall collision. With the pack present it is hidden and the
	// pack architecture is drawn over it - the duel floor and the walls behave exactly the same either way.
	TArray<UStaticMeshComponent*> Grey;
	auto G = [&Grey](UStaticMeshComponent* C) { Grey.Add(C); return C; };

	// The plaza: a dark slab, a bronze seal at the centre (the arena of the arena game).
	G(HWBuild::Part(this, Root, HWBuild::Cylinder(), FVector(0.f, 0.f, -45.f), FVector(WallRadius / 50.f + 1.f, WallRadius / 50.f + 1.f, 0.9f), FRotator::ZeroRotator, Stone, true, 0.85f));
	G(HWBuild::Part(this, Root, HWBuild::Cylinder(), FVector(0.f, 0.f, 1.f), FVector(9.6f, 9.6f, 0.02f), FRotator::ZeroRotator, Bronze, false, 0.5f));
	Seal = G(HWBuild::Part(this, Root, HWBuild::Cylinder(), FVector(0.f, 0.f, 2.f), FVector(9.f, 9.f, 0.02f), FRotator::ZeroRotator, DarkStone, false));
	if (UMaterialInterface* Glow = HWGlowMaterial())
	{
		SealMID = UMaterialInstanceDynamic::Create(Glow, this);
		HWBuild::Part(this, Root, HWBuild::Cylinder(), FVector(0.f, 0.f, 6.f), FVector(3.f, 3.f, 0.01f), FRotator::ZeroRotator, FLinearColor::Black, false)->SetMaterial(0, SealMID);
	}

	// An octagonal parapet with the gate side open, iron pillars with crimson bands, lanterns.
	const float Side = 2.f * WallRadius * FMath::Tan(FMath::DegreesToRadians(22.5f));
	for (int32 I = 0; I < 8; ++I)
	{
		const float A = GateYaw + 45.f * I;
		const FVector Dir(FMath::Cos(FMath::DegreesToRadians(A)), FMath::Sin(FMath::DegreesToRadians(A)), 0.f);
		if (I != 0)
		{
			G(HWBuild::Part(this, Root, HWBuild::Cube(), Dir * (WallRadius + 40.f) + FVector(0.f, 0.f, 80.f),
				FVector(Side / 100.f + 0.9f, 0.8f, 1.6f), FRotator(0.f, A + 90.f, 0.f), DarkStone, true, 0.9f));
		}
		const float C = A + 22.5f;
		const FVector Corner = FVector(FMath::Cos(FMath::DegreesToRadians(C)), FMath::Sin(FMath::DegreesToRadians(C)), 0.f)
			* (WallRadius / FMath::Cos(FMath::DegreesToRadians(22.5f)) + 60.f);
		G(HWBuild::Part(this, Root, HWBuild::Cylinder(), Corner + FVector(0.f, 0.f, 350.f), FVector(1.3f, 1.3f, 7.f), FRotator::ZeroRotator, Iron, true, 0.6f));
		G(HWBuild::Part(this, Root, HWBuild::Cylinder(), Corner + FVector(0.f, 0.f, 480.f), FVector(1.42f, 1.42f, 0.35f), FRotator::ZeroRotator, Crimson, false, 0.6f));
		G(HWBuild::Part(this, Root, HWBuild::Cube(), Corner + FVector(0.f, 0.f, 720.f), FVector(1.8f, 1.8f, 0.4f), FRotator(0.f, C, 0.f), Iron, false, 0.6f));
	}
	// The gate: two tall posts and a lintel framing the open side.
	const FVector GateMid = FVector(Gate.X, Gate.Y, 0.0) * (WallRadius + 40.f);
	const FVector Across = FVector(-Gate.Y, Gate.X, 0.0);
	for (int32 S = -1; S <= 1; S += 2)
	{
		G(HWBuild::Part(this, Root, HWBuild::Cylinder(), GateMid + Across * (S * 300.f) + FVector(0.f, 0.f, 330.f), FVector(0.9f, 0.9f, 6.6f), FRotator::ZeroRotator, Crimson, true, 0.6f));
	}
	G(HWBuild::Part(this, Root, HWBuild::Cube(), GateMid + FVector(0.f, 0.f, 680.f), FVector(1.f, 8.2f, 0.6f), FRotator(0.f, GateYaw, 0.f), DarkStone, false, 0.8f));

	TArray<FVector> LanternPoints;
	for (int32 I = 0; I < 4; ++I)
	{
		const float A = GateYaw + 45.f + 90.f * I;
		const FVector P = FVector(FMath::Cos(FMath::DegreesToRadians(A)), FMath::Sin(FMath::DegreesToRadians(A)), 0.f) * 1250.f;
		LanternPoints.Add(P);
		G(HWBuild::Part(this, Root, HWBuild::Cylinder(), P + FVector(0.f, 0.f, 110.f), FVector(0.22f, 0.22f, 2.2f), FRotator::ZeroRotator, Iron, true, 0.6f));
		G(HWBuild::Part(this, Root, HWBuild::Sphere(), P + FVector(0.f, 0.f, 240.f), FVector(0.6f, 0.6f, 0.75f), FRotator::ZeroRotator, FLinearColor(1.f, 0.35f, 0.08f), false, 0.4f));
		Lanterns.Add(MakeLight(this, Root, P + FVector(0.f, 0.f, 240.f), 60.f, 1800.f, FColor(255, 130, 60)));
	}

	bDressed = Dress(GateYaw, LanternPoints);
	// Smoke behind the shrine while its keeper lives (never over the plaza: it would cloud the fight), rising
	// above the walls so the shrine is found from across the valley (the last one tallest).
	const FVector Behind = -FVector(Gate.X, Gate.Y, 0.0) * (WallRadius + 450.f) + FVector(0.f, 0.f, 600.f);
	Plume = HWBuild::Effect(this, Root, HWFX::Plume, Behind, Spec.bFinal ? 9.f : 6.f, true);
	if (bDressed)
	{
		for (UStaticMeshComponent* C : Grey) { C->SetVisibility(false); }
	}

	// The final shrine's barrier: a wall of red light across the gate, solid until it opens.
	Barrier = HWBuild::Part(this, Root, HWBuild::Cube(), GateMid + FVector(0.f, 0.f, 300.f), FVector(0.3f, 6.2f, 6.f), FRotator(0.f, GateYaw, 0.f), FLinearColor::Black, true);
	if (UMaterialInterface* Glow = HWGlowMaterial())
	{
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Glow, this);
		MID->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.1f, 0.04f, 0.02f));
		Barrier->SetMaterial(0, MID);
	}
	SetState(false, Spec.bFinal);
}

bool AHWShrine::Dress(float GateYaw, const TArray<FVector>& LanternPoints)
{
	// Paragon Monolith, the "Evil" (Dusk) side: a dark inhibitor disc for the plaza, gate-wall slabs, barbican
	// towers at the corners, the Evil gate over the opening, spiked sconces for the lanterns, and two broken
	// statues keeping the approach. The final shrine gets the full barbicans and two tall keeps at its gate.
	const FString Dusk(TEXT("/Game/ParagonProps/Monolith/Dusk/Meshes/"));
	auto M = [&Dusk](const TCHAR* Name) { return HWBuild::OptionalPackageMesh(*(Dusk + Name)); };
	UStaticMesh* Floor = M(TEXT("SternInhibitor4"));
	UStaticMesh* Centre = M(TEXT("SternInhibitorCenter"));
	UStaticMesh* Wall = M(TEXT("Evil_Gate_Wall_Str8_B"));
	UStaticMesh* Tower = M(Spec.bFinal ? TEXT("Evil_Barbican_Full_A") : TEXT("Evil_Barbican_Full_Small_A"));
	UStaticMesh* GateArch = M(TEXT("Evil_Gate_A"));
	UStaticMesh* Sconce = M(TEXT("Evil_Inhibitor_Gate_Sconce_A"));
	UStaticMesh* Keep = M(TEXT("Evil_Barbican_Full_A_Tall"));
	UStaticMesh* Statue = HWBuild::OptionalPackageMesh(TEXT("/Game/ParagonProps/Monolith/Ruins/Meshes/MonoStatue_Damaged"));
	if (Floor == nullptr || Wall == nullptr || GateArch == nullptr || Tower == nullptr) { return false; }

	// The floor's top sits a hair above the (hidden) collision slab; the seal glow floats just above the centre.
	const float Span = 2.f * WallRadius + 160.f;
	HWBuild::Fitted(this, Root, Floor, FVector(0.f, 0.f, 2.f), GateYaw, FVector(Span, Span, 0.f), false, true);
	HWBuild::Fitted(this, Root, Centre, FVector(0.f, 0.f, 4.f), GateYaw, FVector(980.f, 980.f, 0.f), false, true);

	const float Side = 2.f * WallRadius * FMath::Tan(FMath::DegreesToRadians(22.5f));
	for (int32 I = 1; I < 8; ++I)
	{
		const float A = GateYaw + 45.f * I;
		HWBuild::Fitted(this, Root, Wall, FRotator(0.f, A, 0.f).Vector() * (WallRadius + 70.f), A + 90.f, FVector(Side + 80.f, 150.f, 520.f), true);
	}
	for (int32 I = 0; I < 8; ++I)
	{
		const float C = GateYaw + 45.f * I + 22.5f;
		const FVector Corner = FRotator(0.f, C, 0.f).Vector() * (WallRadius / FMath::Cos(FMath::DegreesToRadians(22.5f)) + 70.f);
		HWBuild::Fitted(this, Root, Tower, Corner - FVector(0.f, 0.f, 30.f), C, FVector(0.f, 0.f, Spec.bFinal ? 1250.f : 950.f), true);
	}

	const FVector Out(Gate.X, Gate.Y, 0.0);
	const FVector Across(-Gate.Y, Gate.X, 0.0);
	HWBuild::Fitted(this, Root, GateArch, Out * (WallRadius + 70.f) - FVector(0.f, 0.f, 20.f), GateYaw + 90.f, FVector(Side + 180.f, 0.f, 0.f), false);
	if (Statue != nullptr)
	{
		for (int32 S = -1; S <= 1; S += 2)
		{
			HWBuild::Fitted(this, Root, Statue, Out * (WallRadius + 520.f) + Across * (S * 700.f) - FVector(0.f, 0.f, 40.f), GateYaw, FVector(0.f, 0.f, 650.f), true);
		}
	}
	if (Spec.bFinal && Keep != nullptr)
	{
		for (int32 S = -1; S <= 1; S += 2)
		{
			HWBuild::Fitted(this, Root, Keep, Out * (WallRadius + 350.f) + Across * (S * 1150.f) - FVector(0.f, 0.f, 50.f), GateYaw, FVector(0.f, 0.f, 2400.f), true);
		}
	}

	// The lanterns become spiked sconces with a flame at the tip; the lights move up to the flames.
	UMaterialInterface* Glow = HWGlowMaterial();
	for (int32 I = 0; I < LanternPoints.Num(); ++I)
	{
		const FVector P = LanternPoints[I];
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(P.Y, P.X));
		HWBuild::Fitted(this, Root, Sconce, P, Yaw + 90.f, FVector(0.f, 0.f, 420.f), true);
		if (Glow != nullptr)
		{
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Glow, this);
			UStaticMeshComponent* Orb = HWBuild::Part(this, Root, HWBuild::Sphere(), P + FVector(0.f, 0.f, 440.f), FVector(0.32f, 0.32f, 0.5f), FRotator::ZeroRotator, FLinearColor::Black, false);
			Orb->SetMaterial(0, MID);
			FlameMIDs.Add(MID);
			FlameOrbs.Add(Orb);
		}
		if (UNiagaraComponent* F = HWBuild::Fire(this, Root, P + FVector(0.f, 0.f, 405.f), 36.f, false, true)) { SconceFires.Add(F); }
		if (Lanterns.IsValidIndex(I)) { Lanterns[I]->SetRelativeLocation(P + FVector(0.f, 0.f, 440.f)); }
	}
	return true;
}

void AHWShrine::SetState(bool bInCleared, bool bInSealed)
{
	bCleared = bInCleared;
	bSealed = bInSealed && !bInCleared;
	if (SealMID != nullptr)
	{
		SealMID->SetVectorParameterValue(TEXT("Color"), bCleared ? FLinearColor(0.1f, 0.25f, 0.5f) : FLinearColor(0.7f, 0.1f, 0.02f));
	}
	for (UPointLightComponent* L : Lanterns)
	{
		L->SetLightColor(bCleared ? FColor(140, 180, 255) : FColor(255, 130, 60));
	}
	for (UMaterialInstanceDynamic* MID : FlameMIDs)
	{
		MID->SetVectorParameterValue(TEXT("Color"), bCleared ? FLinearColor(0.6f, 1.2f, 3.f) : FLinearColor(5.f, 1.6f, 0.35f));
	}
	// With real fire: the fire burns while the keeper lives; the pale orb is the broken seal's light.
	for (UNiagaraComponent* F : SconceFires)
	{
		if (bCleared) { F->Deactivate(); }
		else { F->Activate(true); }
	}
	if (SconceFires.Num() > 0)
	{
		for (UStaticMeshComponent* Orb : FlameOrbs) { Orb->SetVisibility(bCleared); }
	}
	if (Plume != nullptr)
	{
		if (bCleared) { Plume->Deactivate(); }
		else { Plume->Activate(true); }
	}
	if (Barrier != nullptr)
	{
		Barrier->SetVisibility(bSealed);
		Barrier->SetCollisionEnabled(bSealed ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	}
}
