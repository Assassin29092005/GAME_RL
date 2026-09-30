// Hellwalker — open world automation tests (the generator is pure, so these need no world).
//
//   Project.HellwalkerRL.World.Deterministic   same seed, same valley; another seed, another valley
//   Project.HellwalkerRL.World.Layout          4 bells, 3 shrines (one final), inside the valley, apart, all connected
//   Project.HellwalkerRL.World.Plazas          every site plaza is flat and level (duels are fought on them)
//   Project.HellwalkerRL.World.Paths           every path is walkable by the explorer (no slope over 40 degrees)
//   Project.HellwalkerRL.World.Shrines         every shrine's cast is authored and its script exists

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HWAnimTypes.h"
#include "HWWorldGen.h"
#include "HWCore/HWBrain.h"

namespace
{
	constexpr EAutomationTestFlags HWWorldTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;
	constexpr int32 Seed = 7; // the shipped valley (AHWOpenWorldGameMode::WorldSeed)
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWWorldDeterministicTest, "Project.HellwalkerRL.World.Deterministic", HWWorldTestFlags)

bool FHWWorldDeterministicTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FHWWorldGen A(Seed);
	const FHWWorldGen B(Seed);
	const FHWWorldGen C(Seed + 1);
	int32 Differ = 0;
	for (int32 I = 0; I < 500; ++I)
	{
		const double X = (A.Hash01(I, 1, 99) * 2.0 - 1.0) * FHWWorldGen::HalfExtent;
		const double Y = (A.Hash01(I, 2, 99) * 2.0 - 1.0) * FHWWorldGen::HalfExtent;
		if (A.Height(X, Y) != B.Height(X, Y))
		{
			AddError(FString::Printf(TEXT("seed %d gives two heights at (%.0f, %.0f)"), Seed, X, Y));
			return false;
		}
		Differ += FMath::Abs(A.Height(X, Y) - C.Height(X, Y)) > 1.0 ? 1 : 0;
	}
	TestTrue(TEXT("another seed is another valley"), Differ > 250);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWWorldLayoutTest, "Project.HellwalkerRL.World.Layout", HWWorldTestFlags)

bool FHWWorldLayoutTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FHWWorldGen G(Seed);
	const TArray<FHWSite>& Sites = G.GetSites();
	int32 Bells = 0;
	int32 ShrineSites = 0;
	for (const FHWSite& S : Sites)
	{
		Bells += S.Kind == EHWSiteKind::Bell ? 1 : 0;
		ShrineSites += S.Kind == EHWSiteKind::Shrine ? 1 : 0;
		const double Limit = FHWWorldGen::HalfExtent * 0.8;
		TestTrue(FString::Printf(TEXT("%s is inside the valley"), *S.Title), FMath::Abs(S.Pos.X) < Limit && FMath::Abs(S.Pos.Y) < Limit);
	}
	TestEqual(TEXT("bells"), Bells, 4);
	TestEqual(TEXT("shrine sites"), ShrineSites, FHWWorldGen::Shrines().Num());
	int32 Finals = 0;
	for (const FHWShrineSpec& S : FHWWorldGen::Shrines()) { Finals += S.bFinal ? 1 : 0; }
	TestEqual(TEXT("exactly one final shrine"), Finals, 1);

	// Plazas never overlap (20 m of ground between any two).
	for (int32 I = 0; I < Sites.Num(); ++I)
	{
		for (int32 J = I + 1; J < Sites.Num(); ++J)
		{
			const double Gap = FVector2D::Distance(Sites[I].Pos, Sites[J].Pos) - Sites[I].Radius - Sites[J].Radius;
			if (Gap < 2000.0) { AddError(FString::Printf(TEXT("%s and %s are only %.0f m apart"), *Sites[I].Title, *Sites[J].Title, Gap / 100.0)); }
		}
	}

	// Every site is reachable by path from the first bell.
	TArray<bool> Seen;
	Seen.Init(false, Sites.Num());
	TArray<int32> Open = { 0 };
	Seen[0] = true;
	while (Open.Num() > 0)
	{
		const int32 N = Open.Pop();
		for (const FIntPoint& L : G.GetLinks())
		{
			const int32 Other = L.X == N ? L.Y : (L.Y == N ? L.X : -1);
			if (Other >= 0 && !Seen[Other]) { Seen[Other] = true; Open.Add(Other); }
		}
	}
	for (int32 I = 0; I < Sites.Num(); ++I)
	{
		if (!Seen[I]) { AddError(FString::Printf(TEXT("%s is not on the path network"), *Sites[I].Title)); }
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWWorldPlazasTest, "Project.HellwalkerRL.World.Plazas", HWWorldTestFlags)

bool FHWWorldPlazasTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FHWWorldGen G(Seed);
	for (const FHWSite& S : G.GetSites())
	{
		double Worst = 0.0;
		for (int32 K = 0; K < 64; ++K)
		{
			const double A = K * 2.0 * UE_DOUBLE_PI / 64.0;
			const double R = S.Radius * (0.15 + 0.8 * G.Hash01(K, S.Index, 7));
			const FVector2D P = S.Pos + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R;
			Worst = FMath::Max(Worst, FMath::Abs(G.Height(P.X, P.Y) - S.Height));
		}
		if (Worst > 5.0) { AddError(FString::Printf(TEXT("%s plaza is not flat: %.1f cm off level"), *S.Title, Worst)); }
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWWorldPathsTest, "Project.HellwalkerRL.World.Paths", HWWorldTestFlags)

bool FHWWorldPathsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FHWWorldGen G(Seed);
	const double MaxSlope = FMath::Tan(FMath::DegreesToRadians(40.0));
	for (const FIntPoint& L : G.GetLinks())
	{
		const FHWSite& A = G.GetSites()[L.X];
		const FHWSite& B = G.GetSites()[L.Y];
		const double Len = FVector2D::Distance(A.Pos, B.Pos);
		const int32 N = FMath::Max(2, FMath::CeilToInt32(Len / 200.0));
		double Worst = 0.0;
		double Prev = G.Height(A.Pos.X, A.Pos.Y);
		for (int32 K = 1; K <= N; ++K)
		{
			const FVector2D P = FMath::Lerp(A.Pos, B.Pos, static_cast<double>(K) / N);
			const double H = G.Height(P.X, P.Y);
			Worst = FMath::Max(Worst, FMath::Abs(H - Prev) / (Len / N));
			Prev = H;
		}
		AddInfo(FString::Printf(TEXT("%s -> %s: %.0f m, steepest %.0f deg"), *A.Title, *B.Title, Len / 100.0, FMath::RadiansToDegrees(FMath::Atan(Worst))));
		if (Worst > MaxSlope) { AddError(FString::Printf(TEXT("path %s -> %s climbs at %.0f deg"), *A.Title, *B.Title, FMath::RadiansToDegrees(FMath::Atan(Worst)))); }
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWWorldShrinesTest, "Project.HellwalkerRL.World.Shrines", HWWorldTestFlags)

bool FHWWorldShrinesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	for (const FHWShrineSpec& S : FHWWorldGen::Shrines())
	{
		TestNotNull(*FString::Printf(TEXT("%s: cast %s is authored"), *S.Title, *S.Cast.ToString()), HWFindCastSpec(S.Cast));
		TestTrue(*FString::Printf(TEXT("%s: script %d exists"), *S.Title, S.Script), S.Script >= 0 && S.Script < HW::NumBossScripts);
		const HW::FScriptSlot* Slots = nullptr;
		const int32 N = HW::BossScript(S.Script, Slots);
		TestTrue(*FString::Printf(TEXT("%s: script has slots"), *S.Title), N > 4 && Slots != nullptr);
		// A chain slot is an attack continuing the string (never the first slot).
		for (int32 I = 0; I < N; ++I)
		{
			if (Slots[I].bChain && (I == 0 || Slots[I].Type != HW::ESlotType::Attack))
			{
				AddError(FString::Printf(TEXT("%s: script slot %d chains but is not an attack"), *S.Title, I));
			}
		}
	}
	return !HasAnyErrors();
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWWorldGroundLayersTest, "Project.HellwalkerRL.World.GroundLayers", HWWorldTestFlags)

bool FHWWorldGroundLayersTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	// The textured ground reads the vertex colour as layer weights: they must be a partition of one, the plazas
	// paved (path dirt), and the steepest open ground bare rock.
	const FHWWorldGen G(Seed);
	double Steepest = 0.0;
	FLinearColor AtSteepest = FLinearColor::Black;
	for (int32 J = 0; J < 48; ++J)
	{
		for (int32 I = 0; I < 48; ++I)
		{
			const double X = -FHWWorldGen::HalfExtent * 0.95 + I * FHWWorldGen::HalfExtent * 1.9 / 47.0;
			const double Y = -FHWWorldGen::HalfExtent * 0.95 + J * FHWWorldGen::HalfExtent * 1.9 / 47.0;
			const FVector N = G.Normal(X, Y);
			const FLinearColor L = G.GroundLayersFrom(X, Y, G.Height(X, Y), N);
			const float Sum = L.R + L.G + L.B + L.A;
			if (FMath::Abs(Sum - 1.f) > 1e-3f || FMath::Min(FMath::Min(L.R, L.G), FMath::Min(L.B, L.A)) < 0.f)
			{
				AddError(FString::Printf(TEXT("Ground layers at (%.0f, %.0f) are not a partition of one: %s"), X, Y, *L.ToString()));
				return false;
			}
			if (G.PathDistance(X, Y) > 1000.0 && G.SiteDistance(X, Y) > 1000.0 && 1.0 - N.Z > Steepest)
			{
				Steepest = 1.0 - N.Z;
				AtSteepest = L;
			}
		}
	}
	TestTrue(TEXT("the steepest open ground is rock"), Steepest > 0.26 && AtSteepest.R > 0.9f);
	for (const FHWSite& S : G.GetSites())
	{
		const FLinearColor L = G.GroundLayersFrom(S.Pos.X, S.Pos.Y, S.Height, G.Normal(S.Pos.X, S.Pos.Y));
		if (L.B < 0.9f) { AddError(FString::Printf(TEXT("%s plaza is not paved: %s"), *S.Title, *L.ToString())); }
	}
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
