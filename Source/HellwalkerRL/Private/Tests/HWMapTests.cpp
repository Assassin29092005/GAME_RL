// HellwalkerRL — the world map and keeper tracking: the pure helpers (HWMap.h), no world.
//
//   Project.HellwalkerRL.Map.Projection    world <-> map UV round trip, north up / east right; the zoomed view round trip and its clamp
//   Project.HellwalkerRL.Map.EdgeMarker    off-screen indicators: behind = bottom, beside = the side edge, always inside the insets
//   Project.HellwalkerRL.Map.Tracking      the default tracked keeper (nearest open; the final gate once it opens) and the player's pick
//   Project.HellwalkerRL.Map.Picture       the valley picture: size, opaque, deterministic, and every plaza where the projection puts it

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HWMap.h"
#include "HWWorldGen.h"

namespace
{
	constexpr EAutomationTestFlags HWMapTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWMapProjectionTest, "Project.HellwalkerRL.Map.Projection", HWMapTestFlags)

bool FHWMapProjectionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const double H = FHWWorldGen::HalfExtent;
	TestTrue(TEXT("the north-west corner is UV (0, 0)"), HWMap::WorldToUV(FVector2D(-H, -H), H).Equals(FVector2D(0.0, 0.0), 1e-9));
	TestTrue(TEXT("the south-east corner is UV (1, 1)"), HWMap::WorldToUV(FVector2D(H, H), H).Equals(FVector2D(1.0, 1.0), 1e-9));
	TestTrue(TEXT("the centre is UV (0.5, 0.5)"), HWMap::WorldToUV(FVector2D::ZeroVector, H).Equals(FVector2D(0.5, 0.5), 1e-9));
	TestTrue(TEXT("east (+X) is to the right"), HWMap::WorldToUV(FVector2D(1000.0, 0.0), H).X > 0.5);
	TestTrue(TEXT("north (-Y) is up"), HWMap::WorldToUV(FVector2D(0.0, -1000.0), H).Y < 0.5);
	const FHWWorldGen G(7);
	for (int32 I = 0; I < 200; ++I)
	{
		const FVector2D W((G.Hash01(I, 1, 5) * 2.0 - 1.0) * H, (G.Hash01(I, 2, 5) * 2.0 - 1.0) * H);
		const FVector2D Back = HWMap::UVToWorld(HWMap::WorldToUV(W, H), H);
		if (!Back.Equals(W, 1e-6))
		{
			AddError(FString::Printf(TEXT("world -> UV -> world moved (%.1f, %.1f) to (%.1f, %.1f)"), W.X, W.Y, Back.X, Back.Y));
			return false;
		}
	}

	// The view: zoomed and panned, screen <-> UV round trip; the window's centre sits at the square's centre.
	HWMap::FView V;
	V.Origin = FVector2D(200.0, 80.0);
	V.Side = 760.0;
	V.Center = FVector2D(0.4, 0.62);
	V.Zoom = 2.5;
	TestTrue(TEXT("the view centre is drawn at the square's centre"), V.UVToScreen(V.Center).Equals(V.Origin + FVector2D(380.0, 380.0), 1e-6));
	for (int32 I = 0; I < 50; ++I)
	{
		const FVector2D UV(G.Hash01(I, 3, 5), G.Hash01(I, 4, 5));
		const FVector2D Back = V.ScreenToUV(V.UVToScreen(UV));
		if (!Back.Equals(UV, 1e-9))
		{
			AddError(TEXT("UV -> screen -> UV moved a point"));
			return false;
		}
	}
	TestTrue(TEXT("the window's corner is at the square's corner"), V.UVToScreen(V.WindowMin()).Equals(V.Origin, 1e-6));
	TestTrue(TEXT("Contains: the centre yes, outside no"), V.Contains(V.Origin + FVector2D(380.0, 380.0)) && !V.Contains(V.Origin - FVector2D(1.0, 1.0)));

	FVector2D C(0.05, 0.97);
	double Z = 2.0;
	HWMap::ClampView(C, Z);
	TestTrue(TEXT("clamp keeps a zoomed window inside the valley"), C.Equals(FVector2D(0.25, 0.75), 1e-9) && Z == 2.0);
	C = FVector2D(0.1, 0.9);
	Z = 0.2;
	HWMap::ClampView(C, Z);
	TestTrue(TEXT("zoomed all the way out: the whole valley, centred"), Z == HWMap::MinZoom && C.Equals(FVector2D(0.5, 0.5), 1e-9));
	Z = 100.0;
	HWMap::ClampView(C, Z);
	TestTrue(TEXT("zoom is capped"), Z == HWMap::MaxZoom);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWMapEdgeMarkerTest, "Project.HellwalkerRL.Map.EdgeMarker", HWMapTestFlags)

bool FHWMapEdgeMarkerTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FVector2D Screen(1600.0, 900.0);
	HWMap::FInsets In;
	In.Left = 40.0;
	In.Right = 40.0;
	In.Top = 100.0;
	In.Bottom = 60.0;
	const double F = HWMap::FocalPixels(Screen.X, 90.0);
	TestTrue(TEXT("a 90 degree view: the focal length is half the width"), FMath::IsNearlyEqual(F, 800.0, 1e-6));
	auto Inside = [&](const HWMap::FEdgeMarker& M)
	{
		return M.Pos.X >= In.Left - 1e-6 && M.Pos.X <= Screen.X - In.Right + 1e-6 && M.Pos.Y >= In.Top - 1e-6 && M.Pos.Y <= Screen.Y - In.Bottom + 1e-6;
	};

	// The camera at the origin looking along +X, level: +Y is to its right, +Z up.
	const FVector Eye = FVector::ZeroVector;
	const FRotator Level(0.f, 0.f, 0.f);
	auto Place = [&](const FVector& Target, const FRotator& View) { return HWMap::PlaceEdgeMarker(HWMap::ToCameraSpace(Eye, View, Target), Screen, F, In); };

	const HWMap::FEdgeMarker Ahead = Place(FVector(5000.0, 0.0, 0.0), Level);
	TestTrue(TEXT("straight ahead: on screen, at the centre"), Ahead.bOnScreen && Ahead.Pos.Equals(Screen * 0.5, 1e-3));
	const HWMap::FEdgeMarker AheadRight = Place(FVector(5000.0, 2500.0, 0.0), Level);
	TestTrue(TEXT("ahead and a little right: on screen, right of centre, at its projection"), AheadRight.bOnScreen && FMath::IsNearlyEqual(AheadRight.Pos.X, 800.0 + 400.0, 1e-3));

	const HWMap::FEdgeMarker Behind = Place(FVector(-5000.0, 0.0, 0.0), Level);
	TestTrue(TEXT("straight behind: off screen, bottom centre, pointing down"), !Behind.bOnScreen && FMath::IsNearlyEqual(Behind.Pos.Y, Screen.Y - In.Bottom, 1e-6)
		&& FMath::IsNearlyEqual(Behind.Pos.X, Screen.X * 0.5, 1e-6) && Behind.Dir.Equals(FVector2D(0.0, 1.0), 1e-6));

	const HWMap::FEdgeMarker Right = Place(FVector(0.0, 5000.0, 0.0), Level);
	TestTrue(TEXT("beside, to the right: the right edge at mid height, pointing right"), !Right.bOnScreen && FMath::IsNearlyEqual(Right.Pos.X, Screen.X - In.Right, 1e-6)
		&& FMath::IsNearlyEqual(Right.Pos.Y, Screen.Y * 0.5, 1e-6) && Right.Dir.Equals(FVector2D(1.0, 0.0), 1e-6));
	const HWMap::FEdgeMarker Left = Place(FVector(0.0, -5000.0, 0.0), Level);
	TestTrue(TEXT("beside, to the left: the left edge, pointing left"), !Left.bOnScreen && FMath::IsNearlyEqual(Left.Pos.X, In.Left, 1e-6) && Left.Dir.X < -0.99);

	const HWMap::FEdgeMarker BehindLeft = Place(FVector(-3000.0, -4000.0, 0.0), Level);
	TestTrue(TEXT("behind and to the left: pointing left and down, on the left or bottom edge"), !BehindLeft.bOnScreen && BehindLeft.Dir.X < 0.0 && BehindLeft.Dir.Y > 0.0
		&& (FMath::IsNearlyEqual(BehindLeft.Pos.X, In.Left, 1e-6) || FMath::IsNearlyEqual(BehindLeft.Pos.Y, Screen.Y - In.Bottom, 1e-6)));
	const HWMap::FEdgeMarker BehindRight = Place(FVector(-4000.0, 300.0, 0.0), Level);
	TestTrue(TEXT("mostly behind, a little right: the bottom edge, right of centre"), !BehindRight.bOnScreen && BehindRight.Dir.X > 0.0
		&& FMath::IsNearlyEqual(BehindRight.Pos.Y, Screen.Y - In.Bottom, 1e-6) && BehindRight.Pos.X > Screen.X * 0.5);

	const HWMap::FEdgeMarker FarRight = Place(FVector(1000.0, 20000.0, 0.0), Level);
	TestTrue(TEXT("in front but far outside the view: the right edge"), !FarRight.bOnScreen && FMath::IsNearlyEqual(FarRight.Pos.X, Screen.X - In.Right, 1e-6) && FarRight.Dir.X > 0.99);
	const HWMap::FEdgeMarker Above = Place(FVector(1000.0, 0.0, 9000.0), Level);
	TestTrue(TEXT("high above, ahead: the top inset edge (under the compass), pointing up"), !Above.bOnScreen && FMath::IsNearlyEqual(Above.Pos.Y, In.Top, 1e-6) && Above.Dir.Y < -0.99);

	// The camera turned to face +Y: the target that was to the right is now ahead, the one ahead is now to the left.
	const FRotator FacingSouth(0.f, 90.f, 0.f);
	TestTrue(TEXT("turned toward it: on screen"), Place(FVector(0.0, 5000.0, 0.0), FacingSouth).bOnScreen);
	TestTrue(TEXT("turned away from it: it is now on the left edge"), Place(FVector(5000.0, 0.0, 0.0), FacingSouth).Dir.X < -0.99);
	// Pitched down like the exploring camera: a target behind at eye height is still at the bottom.
	const HWMap::FEdgeMarker PitchedBehind = Place(FVector(-90000.0, 0.0, 900.0), FRotator(-12.f, 0.f, 0.f));
	TestTrue(TEXT("camera pitched down: far behind is still the bottom edge"), !PitchedBehind.bOnScreen && PitchedBehind.Dir.Y > 0.99);

	for (const HWMap::FEdgeMarker& M : { Ahead, AheadRight, Behind, Right, Left, BehindLeft, BehindRight, FarRight, Above, PitchedBehind })
	{
		if (!Inside(M)) { AddError(FString::Printf(TEXT("an indicator left the inset rectangle: (%.1f, %.1f)"), M.Pos.X, M.Pos.Y)); }
		if (!FMath::IsNearlyEqual(M.Dir.Size(), 1.0, 1e-6)) { AddError(TEXT("an indicator's direction is not a unit vector")); }
	}
	const HWMap::FEdgeMarker AtEye = HWMap::PlaceEdgeMarker(FVector::ZeroVector, Screen, F, In);
	TestTrue(TEXT("a target at the eye itself: a defined direction, inside"), Inside(AtEye) && FMath::IsNearlyEqual(AtEye.Dir.Size(), 1.0, 1e-6));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWMapTrackingTest, "Project.HellwalkerRL.Map.Tracking", HWMapTestFlags)

bool FHWMapTrackingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	// The shipped valley's three keepers (FHWWorldGen::Shrines() order): the Warden, the Sage, the final gate (sealed).
	const FHWWorldGen G(7);
	TArray<HWMap::FKeeperPin> K;
	for (const FHWSite& S : G.GetSites())
	{
		if (S.Kind != EHWSiteKind::Shrine) { continue; }
		K.SetNum(FMath::Max(K.Num(), S.Index + 1));
		K[S.Index].Pos = S.Pos;
		K[S.Index].bSealed = FHWWorldGen::Shrines()[S.Index].bFinal;
	}
	TestEqual(TEXT("three keepers"), K.Num(), 3);
	if (K.Num() != 3) { return false; }
	const FVector2D NearWarden = K[0].Pos + FVector2D(2000.0, 0.0);
	const FVector2D NearSage = K[1].Pos + FVector2D(0.0, 2000.0);
	const FVector2D NearGate = K[2].Pos - FVector2D(2000.0, 2000.0);
	TestEqual(TEXT("by the Warden: the Warden is the objective"), HWMap::DefaultTrackedKeeper(K, NearWarden), 0);
	TestEqual(TEXT("by the Sage: the Sage"), HWMap::DefaultTrackedKeeper(K, NearSage), 1);
	TestTrue(TEXT("by the sealed gate: never the gate while it is sealed"), HWMap::DefaultTrackedKeeper(K, NearGate) != 2 && HWMap::DefaultTrackedKeeper(K, NearGate) != INDEX_NONE);
	K[0].bCleared = true;
	TestEqual(TEXT("the Warden cleared: the Sage, even from beside the Warden's shrine"), HWMap::DefaultTrackedKeeper(K, NearWarden), 1);
	K[1].bCleared = true;
	K[2].bSealed = false;
	TestEqual(TEXT("both cleared, the gate opens: the gate"), HWMap::DefaultTrackedKeeper(K, NearWarden), 2);
	K[2].bCleared = true;
	TestEqual(TEXT("all cleared: nothing to track"), HWMap::DefaultTrackedKeeper(K, NearWarden), static_cast<int32>(INDEX_NONE));

	// The player's pick.
	for (HWMap::FKeeperPin& P : K) { P.bCleared = false; }
	K[2].bSealed = true;
	TestEqual(TEXT("no pick: the objective"), HWMap::ResolveTrackedKeeper(K, INDEX_NONE, NearWarden), 0);
	TestEqual(TEXT("picked the Sage from beside the Warden: the Sage"), HWMap::ResolveTrackedKeeper(K, 1, NearWarden), 1);
	TestEqual(TEXT("the sealed gate may be picked"), HWMap::ResolveTrackedKeeper(K, 2, NearWarden), 2);
	K[1].bCleared = true;
	TestEqual(TEXT("a picked keeper that falls drops off: back to the objective"), HWMap::ResolveTrackedKeeper(K, 1, NearSage), 0);
	TestEqual(TEXT("a pick out of range: the objective"), HWMap::ResolveTrackedKeeper(K, 7, NearWarden), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHWMapPictureTest, "Project.HellwalkerRL.Map.Picture", HWMapTestFlags)

bool FHWMapPictureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	constexpr int32 Size = 320; // 7.5 m pixels: the pixel under a site's centre is well inside even an 11 m plaza
	const FHWWorldGen G(7);
	TArray<FColor> A;
	TArray<FColor> B;
	HWMap::RenderPicture(G, Size, A);
	HWMap::RenderPicture(G, Size, B);
	TestEqual(TEXT("Size x Size pixels"), A.Num(), Size * Size);
	TestTrue(TEXT("the same valley, the same picture"), A == B);
	TestTrue(TEXT("opaque"), !A.ContainsByPredicate([](const FColor& C) { return C.A != 255; }));
	// Every plaza is drawn in the plaza's grey where the projection says it is (a flipped or transposed picture is not).
	int32 Grey = 0;
	for (const FHWSite& S : G.GetSites())
	{
		const FVector2D UV = HWMap::WorldToUV(S.Pos, FHWWorldGen::HalfExtent);
		const int32 X = FMath::Clamp(FMath::FloorToInt32(UV.X * Size), 0, Size - 1);
		const int32 Y = FMath::Clamp(FMath::FloorToInt32(UV.Y * Size), 0, Size - 1);
		const FColor C = A[Y * Size + X];
		const int32 Spread = FMath::Max3(C.R, C.G, C.B) - FMath::Min3(C.R, C.G, C.B);
		Grey += Spread <= 10 ? 1 : 0;
		if (Spread > 10) { AddInfo(FString::Printf(TEXT("%s: pixel (%d, %d, %d) is not plaza grey"), *S.Id.ToString(), C.R, C.G, C.B)); }
	}
	TestEqual(TEXT("every site's plaza is grey on the picture"), Grey, G.GetSites().Num());
	return !HasAnyErrors();
}

#endif
