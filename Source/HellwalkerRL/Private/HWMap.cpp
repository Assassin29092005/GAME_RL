#include "HWMap.h"

#include "HWWorldGen.h"

// (A named namespace, not an anonymous one: unity builds paste this file next to others.)
namespace HWMapPicture
{
	constexpr double ContourCm = 2000.0;   // a faint line every 20 m of height
	constexpr float Exposure = 2.f;        // the ground albedo is dark (0.05 - 0.2): lift it for a readable map
	constexpr double Relief = 3.0;         // the shading sees the hills three times as steep (the valley floor is gentle)
}

FVector2D HWMap::WorldToUV(const FVector2D& World, double HalfExtent)
{
	const double Span = FMath::Max(2.0 * HalfExtent, 1.0);
	return FVector2D((World.X + HalfExtent) / Span, (World.Y + HalfExtent) / Span);
}

FVector2D HWMap::UVToWorld(const FVector2D& UV, double HalfExtent)
{
	return FVector2D(UV.X * 2.0 * HalfExtent - HalfExtent, UV.Y * 2.0 * HalfExtent - HalfExtent);
}

FVector2D HWMap::FView::UVToScreen(const FVector2D& UV) const
{
	return Origin + (UV - WindowMin()) * (Side * Zoom);
}

FVector2D HWMap::FView::ScreenToUV(const FVector2D& Screen) const
{
	return WindowMin() + (Screen - Origin) / FMath::Max(Side * Zoom, 1e-6);
}

bool HWMap::FView::Contains(const FVector2D& Screen, double Margin) const
{
	return Screen.X >= Origin.X + Margin && Screen.X <= Origin.X + Side - Margin && Screen.Y >= Origin.Y + Margin && Screen.Y <= Origin.Y + Side - Margin;
}

void HWMap::ClampView(FVector2D& Center, double& Zoom)
{
	Zoom = FMath::Clamp(FMath::IsFinite(Zoom) ? Zoom : MinZoom, MinZoom, MaxZoom);
	const double Half = 0.5 / Zoom;
	Center.X = FMath::Clamp(FMath::IsFinite(Center.X) ? Center.X : 0.5, Half, 1.0 - Half);
	Center.Y = FMath::Clamp(FMath::IsFinite(Center.Y) ? Center.Y : 0.5, Half, 1.0 - Half);
}

FVector HWMap::ToCameraSpace(const FVector& Eye, const FRotator& View, const FVector& Target)
{
	const FQuat Q = View.Quaternion();
	const FVector D = Target - Eye;
	return FVector(FVector::DotProduct(D, Q.GetRightVector()), FVector::DotProduct(D, Q.GetUpVector()), FVector::DotProduct(D, Q.GetForwardVector()));
}

double HWMap::FocalPixels(double ScreenWidth, double HorizontalFovDegrees)
{
	const double HalfFov = FMath::DegreesToRadians(FMath::Clamp(HorizontalFovDegrees, 5.0, 170.0)) * 0.5;
	return ScreenWidth * 0.5 / FMath::Tan(HalfFov);
}

HWMap::FEdgeMarker HWMap::PlaceEdgeMarker(const FVector& CameraSpace, const FVector2D& Screen, double FocalPx, const FInsets& Insets)
{
	FEdgeMarker Out;
	const FVector2D C = Screen * 0.5;
	const double MinX = FMath::Min(Insets.Left, C.X);
	const double MaxX = FMath::Max(Screen.X - Insets.Right, C.X);
	const double MinY = FMath::Min(Insets.Top, C.Y);
	const double MaxY = FMath::Max(Screen.Y - Insets.Bottom, C.Y);

	// Screen space: right is +X, down is +Y (camera up is -Y).
	FVector2D D(CameraSpace.X, -CameraSpace.Y);
	if (CameraSpace.Z > KINDA_SMALL_NUMBER)
	{
		const FVector2D P = C + D * (FocalPx / CameraSpace.Z);
		if (P.X >= MinX && P.X <= MaxX && P.Y >= MinY && P.Y <= MaxY)
		{
			Out.Pos = P;
			Out.Dir = D.IsNearlyZero() ? FVector2D(0.0, 1.0) : D.GetSafeNormal();
			Out.bOnScreen = true;
			return Out;
		}
		// In front but off screen: the projection lies along D from the centre (the perspective divide keeps its direction).
	}
	else
	{
		// Beside or behind: fold downward by how far behind it is (continuous with the in-front case at Z = 0).
		D.Y -= CameraSpace.Z;
	}
	if (D.IsNearlyZero()) { D = FVector2D(0.0, 1.0); }
	D.Normalize();

	// The ray from the centre to the inset rectangle's border.
	double T = TNumericLimits<double>::Max();
	if (D.X > UE_DOUBLE_SMALL_NUMBER) { T = FMath::Min(T, (MaxX - C.X) / D.X); }
	else if (D.X < -UE_DOUBLE_SMALL_NUMBER) { T = FMath::Min(T, (MinX - C.X) / D.X); }
	if (D.Y > UE_DOUBLE_SMALL_NUMBER) { T = FMath::Min(T, (MaxY - C.Y) / D.Y); }
	else if (D.Y < -UE_DOUBLE_SMALL_NUMBER) { T = FMath::Min(T, (MinY - C.Y) / D.Y); }
	if (T == TNumericLimits<double>::Max()) { T = 0.0; }
	Out.Pos = FVector2D(FMath::Clamp(C.X + D.X * T, MinX, MaxX), FMath::Clamp(C.Y + D.Y * T, MinY, MaxY));
	Out.Dir = D;
	Out.bOnScreen = false;
	return Out;
}

int32 HWMap::DefaultTrackedKeeper(TConstArrayView<FKeeperPin> Keepers, const FVector2D& From)
{
	int32 Best = INDEX_NONE;
	double BestD = TNumericLimits<double>::Max();
	for (int32 I = 0; I < Keepers.Num(); ++I)
	{
		const FKeeperPin& K = Keepers[I];
		if (K.bCleared || K.bSealed) { continue; }
		const double D = FVector2D::DistSquared(K.Pos, From);
		if (D < BestD)
		{
			BestD = D;
			Best = I;
		}
	}
	return Best;
}

int32 HWMap::ResolveTrackedKeeper(TConstArrayView<FKeeperPin> Keepers, int32 Picked, const FVector2D& From)
{
	if (Keepers.IsValidIndex(Picked) && !Keepers[Picked].bCleared) { return Picked; }
	return DefaultTrackedKeeper(Keepers, From);
}

void HWMap::RenderPicture(const FHWWorldGen& Gen, int32 Size, TArray<FColor>& OutPixels)
{
	using namespace HWMapPicture;
	Size = FMath::Max(Size, 2);
	const double Half = FHWWorldGen::HalfExtent;
	const double Step = 2.0 * Half / static_cast<double>(Size);
	auto Coord = [Half, Step](int32 I) { return -Half + (static_cast<double>(I) + 0.5) * Step; };

	// Heights at the pixel centres, with a one-pixel border for the normals (central differences).
	const int32 V = Size + 2;
	TArray<double> Heights;
	Heights.SetNumUninitialized(V * V);
	for (int32 J = 0; J < V; ++J)
	{
		for (int32 I = 0; I < V; ++I) { Heights[J * V + I] = Gen.Height(Coord(I - 1), Coord(J - 1)); }
	}
	auto HeightAt = [&Heights, V](int32 I, int32 J) { return Heights[(J + 1) * V + (I + 1)]; };

	// Light from the north-west (-X, -Y), 45 degrees up: the cartographer's convention. Flat ground keeps its colour.
	const FVector Light = FVector(-1.0, -1.0, UE_SQRT_2).GetSafeNormal();
	const double Flat = Light.Z;
	OutPixels.SetNumUninitialized(Size * Size);
	for (int32 J = 0; J < Size; ++J)
	{
		for (int32 I = 0; I < Size; ++I)
		{
			const double H = HeightAt(I, J);
			const double Dx = HeightAt(I - 1, J) - HeightAt(I + 1, J);
			const double Dy = HeightAt(I, J - 1) - HeightAt(I, J + 1);
			const FVector N = FVector(Dx, Dy, 2.0 * Step).GetSafeNormal();
			FLinearColor Color = Gen.GroundColorFrom(Coord(I), Coord(J), H, N);
			const FVector Steep = FVector(Dx * Relief, Dy * Relief, 2.0 * Step).GetSafeNormal();
			const double Shade = FMath::Clamp(FVector::DotProduct(Steep, Light) / Flat, 0.3, 1.4);
			float K = Exposure * static_cast<float>(0.3 + 0.7 * Shade);
			const int64 Band = FMath::FloorToInt64(H / ContourCm);
			if (Band != FMath::FloorToInt64(HeightAt(I + 1, J) / ContourCm) || Band != FMath::FloorToInt64(HeightAt(I, J + 1) / ContourCm)) { K *= 0.8f; }
			// The mountain wall (FHWWorldGen::Raw rises from 78% of the half extent): out of bounds, dimmed.
			const double Edge = FMath::Max(FMath::Abs(Coord(I)), FMath::Abs(Coord(J))) / Half;
			K *= static_cast<float>(1.0 - 0.45 * FMath::SmoothStep(0.8, 0.97, Edge));
			Color.R *= K;
			Color.G *= K;
			Color.B *= K;
			Color.A = 1.f;
			OutPixels[J * Size + I] = Color.ToFColor(true);
		}
	}
}
