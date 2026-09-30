// Hellwalker — C2: loading a cast and measuring its clips, and the frame lock (HWWarpMoveTime).

#include "HWAnimTypes.h"

#include "HellwalkerRL.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AttributesRuntime.h"
#include "AnimationRuntime.h"
#include "BoneContainer.h"
#include "BonePose.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Misc/FileHelper.h"
#include "Misc/MemStack.h"
#include "Misc/Paths.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"

namespace
{
	constexpr float SampleRate = 60.f;   // measure on the game's own frame grid
	constexpr float MaxPlayRate = 2.5f;  // faster than this and a swing is a blur: trim the wind-up instead

	struct FTipPoint
	{
		FCompactPoseBoneIndex Bone = FCompactPoseBoneIndex(INDEX_NONE);
		FTransform Local = FTransform::Identity;
	};

	/** Samples one clip on the cast mesh's full skeleton. */
	struct FMeasurer
	{
		FBoneContainer Bones;
		TArray<FTipPoint> Tips;
		FCompactPoseBoneIndex Pelvis = FCompactPoseBoneIndex(INDEX_NONE);
		FVector ActorRightInMesh = FVector(-1.f, 0.f, 0.f);

		void Init(USkeletalMesh& Mesh, const FHWCastSpec& Spec)
		{
			const FReferenceSkeleton& Ref = Mesh.GetRefSkeleton();
			TArray<FBoneIndexType> Required;
			for (int32 I = 0; I < Ref.GetNum(); ++I) { Required.Add(static_cast<FBoneIndexType>(I)); }
			Bones.InitializeTo(Required, UE::Anim::FCurveFilterSettings(), Mesh);

			for (const FName& Name : Spec.TipPoints)
			{
				FTipPoint P;
				FTransform SocketLocal;
				int32 BoneIndex = INDEX_NONE;
				int32 SocketIndex = INDEX_NONE;
				if (Mesh.FindSocketInfo(Name, SocketLocal, BoneIndex, SocketIndex) != nullptr && BoneIndex != INDEX_NONE)
				{
					P.Local = SocketLocal;
				}
				else
				{
					BoneIndex = Ref.FindBoneIndex(Name);
				}
				if (BoneIndex == INDEX_NONE) { continue; }
				P.Bone = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(BoneIndex));
				if (P.Bone.IsValid()) { Tips.Add(P); }
			}
			const int32 PelvisMesh = Ref.FindBoneIndex(TEXT("pelvis"));
			if (PelvisMesh != INDEX_NONE) { Pelvis = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(PelvisMesh)); }
			// Mesh space -> actor space is a yaw of MeshYaw; the actor's right (+Y) seen from the mesh:
			ActorRightInMesh = FRotator(0.f, -Spec.MeshYaw, 0.f).RotateVector(FVector(0.f, 1.f, 0.f));
		}

		struct FSample
		{
			TArray<FVector> Tip;
			FVector Root = FVector::ZeroVector;
			FVector PelvisLocal = FVector::ZeroVector;
		};

		FSample Sample(const UAnimSequence& Anim, float Time) const
		{
			FMemMark Mark(FMemStack::Get()); // compact poses live on the mem stack
			FCompactPose Pose;
			Pose.SetBoneContainer(&Bones);
			FBlendedCurve Curve;
			Curve.InitFrom(Bones);
			UE::Anim::FStackAttributeContainer Attributes;
			FAnimationPoseData Data(Pose, Curve, Attributes);
			if (Anim.IsValidAdditive())
			{
				// An additive clip is a delta: measure it on the reference pose.
				Pose.ResetToRefPose();
				FCompactPose Delta;
				Delta.SetBoneContainer(&Bones);
				FBlendedCurve DeltaCurve;
				DeltaCurve.InitFrom(Bones);
				UE::Anim::FStackAttributeContainer DeltaAttributes;
				FAnimationPoseData DeltaData(Delta, DeltaCurve, DeltaAttributes);
				Anim.GetAnimationPose(DeltaData, FAnimExtractContext(static_cast<double>(Time)));
				FAnimationRuntime::AccumulateAdditivePose(Data, DeltaData, 1.f, Anim.GetAdditiveAnimType());
				Pose.NormalizeRotations();
			}
			else
			{
				Anim.GetAnimationPose(Data, FAnimExtractContext(static_cast<double>(Time)));
			}

			FSample S;
			S.Root = Pose[FCompactPoseBoneIndex(0)].GetTranslation();
			if (Pelvis.IsValid()) { S.PelvisLocal = Pose[Pelvis].GetTranslation(); }
			FCSPose<FCompactPose> CS;
			CS.InitPose(Pose);
			for (const FTipPoint& P : Tips)
			{
				S.Tip.Add((P.Local * CS.GetComponentSpaceTransform(P.Bone)).GetLocation());
			}
			return S;
		}
	};

	void Measure(const FMeasurer& M, FHWClip& C, bool bAttack, const FHWClipSpec& Spec)
	{
		const UAnimSequence& Anim = *C.Anim;
		const FMeasurer::FSample First = M.Sample(Anim, C.Start);
		const FMeasurer::FSample Last = M.Sample(Anim, C.End);
		C.PelvisStart = FVector2f(static_cast<float>(First.PelvisLocal.X), static_cast<float>(First.PelvisLocal.Y));
		C.PelvisEnd = FVector2f(static_cast<float>(Last.PelvisLocal.X), static_cast<float>(Last.PelvisLocal.Y));

		const float Travel = static_cast<float>(FVector::Dist2D(First.Root, Last.Root));
		const float Measured = C.Duration() > KINDA_SMALL_NUMBER ? Travel / C.Duration() : 0.f;
		C.RootSpeed = Measured > 5.f ? Measured : Spec.NominalSpeed;

		if (!bAttack || M.Tips.Num() == 0) { return; }

		// Tip speed on the 60 Hz grid; contact = the fastest instant (the weapon lands at full speed).
		const int32 N = FMath::Max(2, FMath::CeilToInt32(C.Duration() * SampleRate) + 1);
		TArray<FMeasurer::FSample> Samples;
		Samples.Reserve(N);
		for (int32 I = 0; I < N; ++I)
		{
			Samples.Add(M.Sample(Anim, FMath::Min(C.Start + I / SampleRate, C.End)));
		}
		TArray<float> Speed;
		TArray<FVector> Vel;
		Speed.Init(0.f, N);
		Vel.Init(FVector::ZeroVector, N);
		for (int32 I = 1; I < N; ++I)
		{
			for (int32 K = 0; K < M.Tips.Num(); ++K)
			{
				const FVector V = (Samples[I].Tip[K] - Samples[I - 1].Tip[K]) * SampleRate;
				const float Sp = static_cast<float>(V.Size());
				if (Sp > Speed[I]) { Speed[I] = Sp; Vel[I] = V; }
			}
		}
		const int32 Lo = FMath::Clamp(FMath::RoundToInt32(N * 0.1f), 1, N - 1);
		const int32 Hi = FMath::Clamp(FMath::RoundToInt32(N * 0.85f), Lo, N - 1);
		int32 ContactIdx = Lo;
		for (int32 I = Lo; I <= Hi; ++I)
		{
			if (Speed[I] > Speed[ContactIdx]) { ContactIdx = I; }
		}
		// Apex: walk back from contact while the tip keeps slowing — the pause at the top of the wind-up.
		int32 ApexIdx = ContactIdx;
		while (ApexIdx > 1 && Speed[ApexIdx - 1] <= Speed[ApexIdx]) { --ApexIdx; }

		if (Spec.Contact < 0.f) { C.Contact = C.Start + ContactIdx / SampleRate; }
		if (Spec.Apex < 0.f) { C.Apex = C.Start + ApexIdx / SampleRate; }
		C.Contact = FMath::Clamp(C.Contact, C.Start, C.End);
		C.Apex = FMath::Clamp(C.Apex, C.Start, C.Contact);
		C.ContactSpeed = Speed[ContactIdx];
		C.SweepSide = static_cast<float>(FVector::DotProduct(Vel[ContactIdx].GetSafeNormal(), M.ActorRightInMesh));
	}

	bool Resolve(const FHWClipSpec& Spec, USkeletalMesh& Mesh, const FMeasurer& M, bool bAttack, FHWClip& Out, FString& Report, const FString& Label)
	{
		if (!Spec.IsSet()) { return false; }
		UAnimSequence* Anim = LoadObject<UAnimSequence>(nullptr, *Spec.Path);
		if (Anim == nullptr)
		{
			Report += FString::Printf(TEXT("%-20s MISSING %s\n"), *Label, *Spec.Path);
			return false;
		}
		if (Anim->GetSkeleton() != Mesh.GetSkeleton())
		{
			Report += FString::Printf(TEXT("%-20s note: skeleton %s differs from the mesh's (compatible-skeleton remap)\n"),
				*Label, Anim->GetSkeleton() != nullptr ? *Anim->GetSkeleton()->GetName() : TEXT("none"));
		}
		const float Len = Anim->GetPlayLength();
		Out.Anim = Anim;
		Out.Start = FMath::Clamp(Spec.Start, 0.f, Len);
		Out.End = Spec.End > 0.f ? FMath::Clamp(Spec.End, Out.Start, Len) : Len;
		Out.Contact = Spec.Contact;
		Out.Apex = Spec.Apex;
		Out.Drift = Spec.Drift;
		Out.bLoop = Spec.bLoop;
		Out.bAdditive = Anim->IsValidAdditive();
		Out.AdditiveType = static_cast<uint8>(Anim->GetAdditiveAnimType());
		if (Out.bAdditive) { Out.Drift = EHWDrift::None; }
		Measure(M, Out, bAttack, Spec);
		if (bAttack)
		{
			Report += FString::Printf(TEXT("%-20s %-28s%s len %.2f  apex %.2f  contact %.2f (%4.0f cm/s)  side %+.2f  pelvis travel %.0f cm\n"),
				*Label, *Anim->GetName(), Out.bAdditive ? TEXT(" [additive]") : TEXT(""), Len, Out.Apex, Out.Contact, Out.ContactSpeed, Out.SweepSide,
				static_cast<float>((Out.PelvisEnd - Out.PelvisStart).Size()));
		}
		else
		{
			Report += FString::Printf(TEXT("%-20s %-28s%s len %.2f  root %.0f cm/s  pelvis travel %.0f cm\n"),
				*Label, *Anim->GetName(), Out.bAdditive ? TEXT(" [additive]") : TEXT(""), Len, Out.RootSpeed, static_cast<float>((Out.PelvisEnd - Out.PelvisStart).Size()));
		}
		return true;
	}

	const TCHAR* RoleName(EHWAnimRole R)
	{
		static const TCHAR* Names[] = {
			TEXT("Idle"), TEXT("MoveF"), TEXT("MoveB"), TEXT("MoveL"), TEXT("MoveR"),
			TEXT("GuardIdle"), TEXT("GuardMoveF"), TEXT("GuardMoveB"), TEXT("GuardMoveL"), TEXT("GuardMoveR"),
			TEXT("BlockHit"), TEXT("GuardBreak"),
			TEXT("HitF"), TEXT("HitB"), TEXT("HitL"), TEXT("HitR"), TEXT("Stagger"),
			TEXT("StunStart"), TEXT("StunLoop"), TEXT("StunEnd"),
			TEXT("Death"), TEXT("GuardStance"), TEXT("CounterStance") };
		static_assert(UE_ARRAY_COUNT(Names) == HWNumAnimRoles, "role names");
		return Names[static_cast<int32>(R)];
	}

	/** Standing height of the reference pose: the head bone, plus a crown. */
	float RefHeight(const USkeletalMesh& Mesh)
	{
		const FReferenceSkeleton& Ref = Mesh.GetRefSkeleton();
		int32 Bone = Ref.FindBoneIndex(TEXT("head"));
		if (Bone == INDEX_NONE) { return static_cast<float>(Mesh.GetImportedBounds().BoxExtent.Z * 2.0); }
		FTransform CS = FTransform::Identity;
		while (Bone != INDEX_NONE)
		{
			CS = CS * Ref.GetRefBonePose()[Bone];
			Bone = Ref.GetParentIndex(Bone);
		}
		return static_cast<float>(CS.GetLocation().Z) * 1.1f;
	}
}

UHWAnimSet* UHWAnimSet::Load(UObject* Outer, FName CastName)
{
	const FHWCastSpec* Spec = HWFindCastSpec(CastName);
	if (Spec == nullptr) { return nullptr; }
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *Spec->MeshPath);
	if (Mesh == nullptr)
	{
		UE_LOG(LogHellwalkerRL, Display, TEXT("Cast %s: mesh %s not in the project — greybox."), *CastName.ToString(), *Spec->MeshPath);
		return nullptr;
	}

	UHWAnimSet* Set = NewObject<UHWAnimSet>(Outer);
	Set->Mesh = Mesh;
	Set->CastName = CastName;
	Set->MeshYaw = Spec->MeshYaw;
	Set->bHandBlades = Spec->bHandBlades;
	Set->TrailSocket = Spec->TrailSocket;
	Set->TrailSystem = Spec->TrailSystem;
	Set->TrailColor = Spec->TrailColor;
	Set->TrailWidth = Spec->TrailWidth;
	Set->TrailSockets = Spec->TrailSockets;
	const float Height = RefHeight(*Mesh);
	Set->MeshScale = Height > 1.f ? Spec->Height / Height : 1.f;
	if (!Spec->LookMeshPath.IsEmpty())
	{
		Set->LookMesh = LoadObject<USkeletalMesh>(nullptr, *Spec->LookMeshPath);
		if (Set->LookMesh != nullptr)
		{
			const float LookRef = RefHeight(*Set->LookMesh);
			Set->LookScale = LookRef > 1.f ? (Spec->LookHeight > 0.f ? Spec->LookHeight : Spec->Height) / LookRef : 1.f;
			Set->LookRetargetTag = Spec->LookRetargetTag;
		}
		else
		{
			UE_LOG(LogHellwalkerRL, Display, TEXT("Cast %s: look %s not in the project - the driver is drawn."), *CastName.ToString(), *Spec->LookMeshPath);
		}
	}
	Set->Roles.SetNum(HWNumAnimRoles);
	Set->Moves.SetNum(HW::NumMoves);

	FMeasurer M;
	M.Init(*Mesh, *Spec);
	FString& R = Set->Report;
	R += FString::Printf(TEXT("Cast %s  mesh %s  ref height %.0f cm -> scale %.3f  tips %d\n\n"),
		*CastName.ToString(), *Mesh->GetName(), Height, Set->MeshScale, M.Tips.Num());

	for (int32 I = 0; I < HWNumAnimRoles; ++I)
	{
		const FHWClipSpec& S = Spec->Roles[I];
		if (S.IsSet() && !Resolve(S, *Mesh, M, false, Set->Roles[I], R, RoleName(static_cast<EHWAnimRole>(I)))) { ++Set->MissingClips; }
	}
	R += TEXT("\n");
	for (int32 I = 1; I < HW::NumMoves; ++I)
	{
		const FHWClipSpec& S = Spec->Moves[I];
		const HW::FMoveData& Mv = HW::Move(static_cast<HW::EMoveId>(I));
		if (S.IsSet() && !Resolve(S, *Mesh, M, Mv.IsAttack(), Set->Moves[I], R, UTF8_TO_TCHAR(Mv.Name))) { ++Set->MissingClips; }
	}

	const FString Path = FPaths::ProjectSavedDir() / TEXT("HellwalkerRL") / FString::Printf(TEXT("AnimReport_%s.txt"), *CastName.ToString());
	FFileHelper::SaveStringToFile(R, *Path);
	UE_LOG(LogHellwalkerRL, Display, TEXT("Cast %s loaded (%d clips missing) — report %s"), *CastName.ToString(), Set->MissingClips, *Path);
	return Set;
}

void UHWAnimSet::Survey(FName CastName)
{
	const FHWCastSpec* Spec = HWFindCastSpec(CastName);
	if (Spec == nullptr || Spec->SurveyPath.IsEmpty()) { return; }
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *Spec->MeshPath);
	if (Mesh == nullptr) { return; }

	IAssetRegistry& Registry = FAssetRegistryModule::GetRegistry();
	Registry.ScanPathsSynchronous({ Spec->SurveyPath }, true);
	TArray<FAssetData> Assets;
	Registry.GetAssetsByPath(FName(*Spec->SurveyPath), Assets, true);
	Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.PackageName.LexicalLess(B.PackageName); });

	FMeasurer M;
	M.Init(*Mesh, *Spec);
	FString R = FString::Printf(TEXT("Survey %s under %s - every clip measured as an attack.\n")
		TEXT("side: + the weapon crosses to the attacker's RIGHT at contact (SweepLeft), - to the LEFT (SweepRight)\n\n"),
		*CastName.ToString(), *Spec->SurveyPath);
	int32 N = 0;
	for (const FAssetData& A : Assets)
	{
		if (A.AssetClassPath != UAnimSequence::StaticClass()->GetClassPathName()) { continue; }
		UAnimSequence* Anim = Cast<UAnimSequence>(A.GetAsset());
		if (Anim == nullptr || Anim->GetSkeleton() != Mesh->GetSkeleton()) { continue; }
		FHWClipSpec S;
		S.Path = A.GetObjectPathString();
		FHWClip C;
		FString Line;
		Resolve(S, *Mesh, M, true, C, Line, A.PackagePath.ToString().RightChop(Spec->SurveyPath.Len()));
		R += Line;
		++N;
	}
	const FString Path = FPaths::ProjectSavedDir() / TEXT("HellwalkerRL") / FString::Printf(TEXT("AnimSurvey_%s.txt"), *CastName.ToString());
	FFileHelper::SaveStringToFile(R, *Path);
	UE_LOG(LogHellwalkerRL, Display, TEXT("Survey %s: %d clips -> %s"), *CastName.ToString(), N, *Path);
}

// ---------------------------------------------------------------------------------------------------
// The frame lock

namespace
{
	struct FWarpKey { float F; float T; };

	float EvalKeys(const FWarpKey* K, int32 N, float X)
	{
		if (X <= K[0].F) { return K[0].T; }
		for (int32 I = 1; I < N; ++I)
		{
			if (X <= K[I].F)
			{
				const float Span = FMath::Max(K[I].F - K[I - 1].F, KINDA_SMALL_NUMBER);
				return FMath::Lerp(K[I - 1].T, K[I].T, (X - K[I - 1].F) / Span);
			}
		}
		return K[N - 1].T;
	}
}

float HWWarpMoveTime(const HW::FMoveData& M, const FHWClip& C, float T)
{
	if (C.End <= C.Start) { return C.Start; } // pure function of the marks (tests run it without a clip)
	if (C.bLoop)
	{
		return C.Start + FMath::Fmod(FMath::Max(T, 0.f) / SampleRate, C.Duration());
	}
	const float Total = static_cast<float>(FMath::Max(M.TotalFrames(), 1));

	if (M.IsAttack() && C.Contact >= 0.f)
	{
		const float S = static_cast<float>(FMath::Max(M.Startup, 1));
		const int32 Fake = M.FakeImpactFrame;
		const bool bImitates = Fake > 0 && Fake < M.Startup;
		// Rate caps: trim the clip's wind-up / follow-through rather than play it as a blur. A hold or a
		// feint trims like the move it imitates, so the two are identical up to the fake impact.
		const float Imitated = bImitates ? static_cast<float>(Fake) : S;
		const float Start = FMath::Max(C.Start, C.Contact - Imitated / SampleRate * MaxPlayRate);
		const float End = FMath::Min(C.End, C.Contact + FMath::Max(Total - S, 1.f) / SampleRate * MaxPlayRate);
		const float Apex = (C.Apex >= Start && C.Apex < C.Contact) ? C.Apex : FMath::Lerp(Start, C.Contact, 0.6f);

		FWarpKey K[8];
		int32 N = 0;
		K[N++] = { 0.f, Start };
		if (bImitates)
		{
			// Up to the fake impact the clip plays exactly as the move it imitates (impact at Fake).
			const float Fk = static_cast<float>(Fake);
			const float R = FMath::Clamp((Apex - Start) / FMath::Max(C.Contact - Start, KINDA_SMALL_NUMBER), 0.2f, 0.9f);
			const float A0 = FMath::Max(1.f, Fk * R);
			const float Swing = FMath::Max(Fk - A0, 3.f);
			K[N++] = { A0, Apex };
			if (M.Symbol == HW::ESym::BFeint)
			{
				// The fake: half the swing arrives at the fake impact, then it pulls back to the top.
				K[N++] = { Fk, FMath::Lerp(Apex, C.Contact, 0.5f) };
				K[N++] = { FMath::Min(Fk + 6.f, S - Swing - 0.5f), Apex };
			}
			// The hold: the real swing takes as long as the imitated one did.
			const float Release = S - Swing;
			if (Release > K[N - 1].F + 0.5f) { K[N++] = { Release, Apex }; }
		}
		K[N++] = { S, C.Contact };
		K[N++] = { FMath::Max(Total, S + 1.f), End };
		// Keys must be increasing in frames (degenerate authoring collapses to the plain mapping).
		for (int32 I = 1; I < N; ++I) { K[I].F = FMath::Max(K[I].F, K[I - 1].F + 0.01f); }
		return EvalKeys(K, N, T);
	}

	const float End = FMath::Min(C.End, C.Start + Total / SampleRate * MaxPlayRate);
	return FMath::Lerp(C.Start, End, FMath::Clamp(T / Total, 0.f, 1.f));
}
