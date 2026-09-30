// Hellwalker — C2: a native anim instance with no Animation Blueprint.
//
// The game thread decides WHAT to show (FHWAnimFrame: clips, times, weights — computed by
// FHWAnimDriver from the core's frame state); the worker thread samples and blends it. No state
// machine, no montages, no notifies: a clip's time is a pure function of the fighter's frame index,
// so animation can never drift from the rules, and hitstop freezes it for free.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "HWAnimTypes.h"
#include "HWAnimInstance.generated.h"

/** One clip sampled at one time. */
struct FHWAnimSample
{
	const UAnimSequence* Anim = nullptr;
	float Time = 0.f;
	float Weight = 0.f;
	bool bUpperBody = false;       // spine and above only (locomotion keeps the legs)
	bool bAdditive = false;        // accumulated onto everything below it
	uint8 AdditiveType = 0;
	EHWDrift Drift = EHWDrift::None;
	float Start = 0.f;
	float End = 0.f;
	FVector2f PelvisStart = FVector2f::ZeroVector;
	FVector2f PelvisEnd = FVector2f::ZeroVector;

	static FHWAnimSample Of(const FHWClip& C, float Time, float Weight, bool bUpper = false)
	{
		FHWAnimSample S;
		S.Anim = C.Anim;
		S.Time = Time;
		S.Weight = Weight;
		S.bUpperBody = bUpper;
		S.Drift = C.Drift;
		S.Start = C.Start;
		S.End = C.End;
		S.PelvisStart = C.PelvisStart;
		S.PelvisEnd = C.PelvisEnd;
		S.bAdditive = C.bAdditive;
		S.AdditiveType = C.AdditiveType;
		return S;
	}
};

/** What to show this frame: an n-way weighted base (locomotion), then layers blended over it in order. */
struct FHWAnimFrame
{
	TArray<FHWAnimSample, TInlineAllocator<10>> Base;
	TArray<FHWAnimSample, TInlineAllocator<3>> Layers;
};

struct FHWAnimProxy : public FAnimInstanceProxy
{
	FHWAnimProxy() = default;
	explicit FHWAnimProxy(UAnimInstance* InInstance) : FAnimInstanceProxy(InInstance) {}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

private:
	void CacheBoneData(const FBoneContainer& Bones);
	void SampleInto(FCompactPose& Pose, FBlendedCurve& Curve, UE::Anim::FStackAttributeContainer& Attributes, const FHWAnimSample& S) const;

	FHWAnimFrame Frame;
	TArray<float> UpperMask;
	FCompactPoseBoneIndex PelvisIndex = FCompactPoseBoneIndex(INDEX_NONE);
	uint16 CachedSerial = 0;
	bool bBonesCached = false;
};

UCLASS(Transient, NotBlueprintable)
class HELLWALKERRL_API UHWAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	void SetFrame(const FHWAnimFrame& InFrame) { Pending = InFrame; }

	/** The cast whose sequences the samples point at (keeps them alive). */
	UPROPERTY(Transient) TObjectPtr<UHWAnimSet> Set;

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FHWAnimProxy(this); }
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override { delete InProxy; }

private:
	friend struct FHWAnimProxy;
	FHWAnimFrame Pending;
};
