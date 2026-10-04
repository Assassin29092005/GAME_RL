// Hellwalker — C2 animation: casts, clips and the frame lock.
//
// The rules never read animation (PLAN §5.2): a fighter's combat state is the core's integer frame
// cursor, and this layer only DEPICTS it. Every clip is sampled at a time computed from the move's
// frame index, piecewise-linearly, so the clip's contact pose lands exactly on the frame the rules
// resolve the hit (FakeImpactFrame too: a feint visibly "arrives" where the bait is). What the player
// reads is what will resolve — legibility, PLAN §2.4(a) — at any clip speed, through hitstop, forever.
//
// A "cast" is a mesh plus a clip for every move and presentation role. Casts are authored in C++
// (HWAnimCasts.cpp) against Fab packs by path; a missing pack simply means the greybox fighter.
// Clip marks (contact, apex) are measured from the clip itself when not authored — the frame the
// weapon moves fastest is the frame it lands.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "HWCore/HWMoves.h"
#include "HWAnimTypes.generated.h"

class UAnimSequence;
class USkeletalMesh;

/** How a clip's horizontal travel is removed so the mesh stays on the capsule (gameplay moves the capsule). */
UENUM()
enum class EHWDrift : uint8
{
	None,    // keep the pelvis path (in-place clips)
	Linear,  // remove the net start->end pelvis travel, keep the sway (attacks)
	Full     // pin the pelvis over the capsule (steps: the root-motion source does the travelling)
};

/** Presentation roles that are not moves. */
enum class EHWAnimRole : uint8
{
	Idle, MoveF, MoveB, MoveL, MoveR,                       // locomotion
	GuardIdle, GuardMoveF, GuardMoveB, GuardMoveL, GuardMoveR, // locomotion with the guard up
	BlockHit, GuardBreak,
	HitF, HitB, HitL, HitR, Stagger,
	StunStart, StunLoop, StunEnd,
	Death,
	GuardStance, CounterStance,
	Count
};
inline constexpr int32 HWNumAnimRoles = static_cast<int32>(EHWAnimRole::Count);

/** Authoring: one clip + its marks, in seconds of the clip. Negative marks are measured from the clip. */
struct FHWClipSpec
{
	FString Path;
	float Start = 0.f;
	float End = -1.f;
	float Contact = -1.f;
	float Apex = -1.f;
	float NominalSpeed = 0.f;   // locomotion: cm/s depicted when the clip has no root motion
	EHWDrift Drift = EHWDrift::Linear;
	bool bLoop = false;
	bool IsSet() const { return !Path.IsEmpty(); }
};

/** Authoring: a fighter's look. */
struct FHWCastSpec
{
	FName Name;
	FString MeshPath;
	float Height = 180.f;             // standing height the mesh is scaled to (cm)
	float MeshYaw = -90.f;            // mesh forward (+Y for UE rigs) onto the actor's +X
	TArray<FName> TipPoints;          // bones / sockets whose speed marks a clip's contact
	FName TrailSocket;                // presentation: where sparks and trails come from
	bool bHandBlades = false;         // the player's twin blades / glaive are attached to the hands
	FString SurveyPath;               // -HWAnimSurvey measures every clip under here (AnimSurvey_<cast>.txt)
	FString TrailSystem;              // Niagara ribbon shown over the active frames (Slash Trail FX pack)
	FLinearColor TrailColor = FLinearColor::White;
	float TrailWidth = 60.f;
	TArray<FName> TrailSockets;       // where the trails hang (hand-blade casts use the blades instead)
	TArray<FName> TelegraphSockets;   // the weapon hand(s) the parry assist's ring is drawn round (AHWCharacterBase::GetTelegraphPoints)
	// Optional: a different body drawn over the (hidden) driver mesh by the Game Animation Sample's runtime
	// retargeting - the driver keeps the frame-locked clips and every measured mark; only the look changes.
	FString LookMeshPath;
	FName LookRetargetTag;            // ABP_GenericRetarget picks its retargeter from the look's first tag
	float LookHeight = 0.f;           // the look's standing height (cm)
	FHWClipSpec Roles[HWNumAnimRoles];
	FHWClipSpec Moves[HW::NumMoves];
};

/** Every authored cast ("Soul", "Sevarog", "Wukong"). */
const FHWCastSpec* HWFindCastSpec(FName Name);
TArray<FName> HWCastNames();

/** Which cast depicts a side: Soul for the player; -HWBoss=<cast> (default Sevarog); -HWGreybox = none. */
FName HWCastForSide(HW::ESide Side);

/** A resolved clip: loaded sequence + measured marks. */
USTRUCT()
struct FHWClip
{
	GENERATED_BODY()

	UPROPERTY() TObjectPtr<UAnimSequence> Anim;
	UPROPERTY() float Start = 0.f;
	UPROPERTY() float End = 0.f;
	UPROPERTY() float Contact = -1.f;      // attack clips: the landing pose
	UPROPERTY() float Apex = -1.f;         // attack clips: top of the wind-up (feints and holds use it)
	UPROPERTY() float RootSpeed = 0.f;     // locomotion: cm/s this clip depicts (measured or nominal)
	UPROPERTY() EHWDrift Drift = EHWDrift::Linear;
	UPROPERTY() bool bLoop = false;
	UPROPERTY() bool bAdditive = false;    // an additive clip (Paragon hit reacts, aim poses): layered, never a pose
	UPROPERTY() uint8 AdditiveType = 0;    // EAdditiveAnimationType
	UPROPERTY() FVector2f PelvisStart = FVector2f::ZeroVector;
	UPROPERTY() FVector2f PelvisEnd = FVector2f::ZeroVector;
	UPROPERTY() float ContactSpeed = 0.f;  // report: tip speed at contact (cm/s)
	UPROPERTY() float SweepSide = 0.f;     // report: + the tip crosses to the attacker's right at contact

	bool IsValid() const { return Anim != nullptr && End > Start; }
	float Duration() const { return End - Start; }
};

/** A loaded cast. Owns the sequences (GC) for the anim instance that samples them. */
UCLASS(Transient)
class HELLWALKERRL_API UHWAnimSet : public UObject
{
	GENERATED_BODY()

public:
	/** Load and measure a cast. Null when its mesh is not in the project (greybox fallback). */
	static UHWAnimSet* Load(UObject* Outer, FName CastName);

	/** Measure every clip under the cast's survey path, as if each were an attack — for choosing clips. */
	static void Survey(FName CastName);

	const FHWClip& Role(EHWAnimRole R) const { return Roles[static_cast<int32>(R)]; }
	const FHWClip& Move(HW::EMoveId Id) const { return Moves[static_cast<int32>(Id)]; }

	UPROPERTY() TObjectPtr<USkeletalMesh> Mesh;
	/** The retargeted body over the driver (null: the driver itself is drawn). */
	UPROPERTY() TObjectPtr<USkeletalMesh> LookMesh;
	FName LookRetargetTag;
	float LookScale = 1.f;
	UPROPERTY() TArray<FHWClip> Roles;
	UPROPERTY() TArray<FHWClip> Moves;

	FName CastName;
	float MeshScale = 1.f;
	float MeshYaw = -90.f;
	bool bHandBlades = false;
	FName TrailSocket;
	FString TrailSystem;
	FLinearColor TrailColor = FLinearColor::White;
	float TrailWidth = 60.f;
	TArray<FName> TrailSockets;
	TArray<FName> TelegraphSockets;
	int32 MissingClips = 0;
	/** One line per clip: marks, measured contact, sweep side, root speed (Saved/Hellwalker/AnimReport_<cast>.txt). */
	FString Report;
};

/**
 * THE frame lock. Clip time for a move at (fractional) move frame T:
 *   attacks   (0,Start) -> (Impact,Contact) -> (Total,End)
 *   holds     the delayed heavy reads like the ordinary heavy, then holds at the apex until its real impact
 *   feints    the fake swing half-arrives at FakeImpactFrame, pulls back to the apex, strikes at Impact
 *   stances   loop
 *   others    (0,Start) -> (Total,End)
 */
float HWWarpMoveTime(const HW::FMoveData& M, const FHWClip& C, float T);
