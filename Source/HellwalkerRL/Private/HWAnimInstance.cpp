#include "HWAnimInstance.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "AnimationRuntime.h"

namespace
{
	/** A = lerp(A, B, W) per bone, optionally masked. */
	void BlendInto(FCompactPose& A, const FCompactPose& B, float W, const TArray<float>* Mask)
	{
		for (const FCompactPoseBoneIndex I : A.ForEachBoneIndex())
		{
			const float Wi = Mask != nullptr ? W * (*Mask)[I.GetInt()] : W;
			if (Wi <= 0.f) { continue; }
			if (Wi >= 1.f) { A[I] = B[I]; continue; }
			FTransform Out;
			Out.Blend(A[I], B[I], Wi);
			A[I] = Out;
		}
	}
}

void FHWAnimProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	Frame = CastChecked<UHWAnimInstance>(InAnimInstance)->Pending;
}

void FHWAnimProxy::CacheBoneData(const FBoneContainer& Bones)
{
	if (bBonesCached && Bones.GetSerialNumber() == CachedSerial) { return; }
	bBonesCached = true;
	CachedSerial = Bones.GetSerialNumber();

	auto Compact = [&Bones](const TCHAR* Name)
	{
		const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(FName(Name));
		return MeshIndex != INDEX_NONE ? Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex)) : FCompactPoseBoneIndex(INDEX_NONE);
	};
	PelvisIndex = Compact(TEXT("pelvis"));

	// Upper body = spine_01 and everything under it. Compact order puts parents before children.
	const FCompactPoseBoneIndex Spine = Compact(TEXT("spine_01"));
	const int32 N = Bones.GetCompactPoseNumBones();
	UpperMask.Init(0.f, N);
	for (int32 I = 0; I < N; ++I)
	{
		const FCompactPoseBoneIndex Index(I);
		if (Index == Spine) { UpperMask[I] = 1.f; continue; }
		const FCompactPoseBoneIndex Parent = Bones.GetParentBoneIndex(Index);
		if (Parent.IsValid()) { UpperMask[I] = UpperMask[Parent.GetInt()]; }
	}
}

void FHWAnimProxy::SampleInto(FCompactPose& Pose, FBlendedCurve& Curve, UE::Anim::FStackAttributeContainer& Attributes, const FHWAnimSample& S) const
{
	FAnimationPoseData Data(Pose, Curve, Attributes);
	S.Anim->GetAnimationPose(Data, FAnimExtractContext(static_cast<double>(S.Time)));

	if (S.bAdditive) { return; } // deltas: nothing to pin
	// The capsule IS the root: gameplay moves it, so no clip may move the mesh off it.
	const FCompactPoseBoneIndex Root(0);
	Pose[Root].SetTranslation(Pose.GetRefPose(Root).GetTranslation());
	if (PelvisIndex.IsValid() && S.Drift != EHWDrift::None)
	{
		FTransform& Pelvis = Pose[PelvisIndex];
		FVector T = Pelvis.GetTranslation();
		FVector2f Off;
		if (S.Drift == EHWDrift::Full)
		{
			Off = FVector2f(static_cast<float>(T.X), static_cast<float>(T.Y)) - S.PelvisStart;
		}
		else
		{
			const float U = S.End > S.Start ? FMath::Clamp((S.Time - S.Start) / (S.End - S.Start), 0.f, 1.f) : 0.f;
			Off = (S.PelvisEnd - S.PelvisStart) * U;
		}
		T.X -= Off.X;
		T.Y -= Off.Y;
		Pelvis.SetTranslation(T);
	}
}

bool FHWAnimProxy::Evaluate(FPoseContext& Output)
{
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	CacheBoneData(Bones);
	Output.ResetToRefPose();

	// No FMemMark here: evaluation already runs inside one, and a mark of ours would free any attributes
	// the sampler adds to Output while Output still needs them.
	FCompactPose Tmp;
	Tmp.SetBoneContainer(&Bones);
	FBlendedCurve TmpCurve;
	TmpCurve.InitFrom(Output.Curve);
	UE::Anim::FStackAttributeContainer TmpAttributes;

	// Base: normalised n-way blend, accumulated as a running weighted average.
	float Acc = 0.f;
	for (const FHWAnimSample& S : Frame.Base)
	{
		if (S.Anim == nullptr || S.Weight <= KINDA_SMALL_NUMBER || S.bAdditive) { continue; }
		if (Acc <= 0.f)
		{
			SampleInto(Output.Pose, Output.Curve, Output.CustomAttributes, S);
			Acc = S.Weight;
			continue;
		}
		SampleInto(Tmp, TmpCurve, TmpAttributes, S);
		Acc += S.Weight;
		BlendInto(Output.Pose, Tmp, S.Weight / Acc, nullptr);
	}

	// Layers: actions, reactions, the guard — each over everything below it.
	for (const FHWAnimSample& L : Frame.Layers)
	{
		if (L.Anim == nullptr || L.Weight <= KINDA_SMALL_NUMBER) { continue; }
		if (L.bAdditive)
		{
			SampleInto(Tmp, TmpCurve, TmpAttributes, L);
			FAnimationPoseData BaseData(Output);
			const FAnimationPoseData AddData(Tmp, TmpCurve, TmpAttributes);
			FAnimationRuntime::AccumulateAdditivePose(BaseData, AddData, FMath::Min(L.Weight, 1.f), static_cast<EAdditiveAnimationType>(L.AdditiveType));
			Output.Pose.NormalizeRotations();
			continue;
		}
		if (Acc <= 0.f && !L.bUpperBody)
		{
			SampleInto(Output.Pose, Output.Curve, Output.CustomAttributes, L);
			Acc = 1.f;
			continue;
		}
		SampleInto(Tmp, TmpCurve, TmpAttributes, L);
		BlendInto(Output.Pose, Tmp, FMath::Min(L.Weight, 1.f), L.bUpperBody ? &UpperMask : nullptr);
	}
	Output.Pose.NormalizeRotations();
	return true;
}
