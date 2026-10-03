// HellwalkerRL — the world map's pure parts: world <-> map coordinates, the map view (zoom and pan), where a keeper's
// indicator sits on the screen edge when it is off screen, which keeper is tracked, and the valley's picture from above.
// No UObjects and no drawing here, so it is unit-tested (Project.HellwalkerRL.Map.*). AHWOpenWorld renders the picture
// once per world (off the game thread), AHWPlayerController drives the view, AHWHUD draws the map and the indicators.
//
// Orientation, as AHWHUD::DrawCompass: +X is east (map right), +Y is south (map down) — north is up.

#pragma once

#include "CoreMinimal.h"
#include <atomic>

class FHWWorldGen;

namespace HWMap
{
	/** World XY (cm) -> map UV: (0, 0) is the valley's north-west corner, (1, 1) its south-east one. */
	HELLWALKERRL_API FVector2D WorldToUV(const FVector2D& World, double HalfExtent);
	HELLWALKERRL_API FVector2D UVToWorld(const FVector2D& UV, double HalfExtent);

	constexpr double MinZoom = 1.0;   // the whole valley
	constexpr double MaxZoom = 4.0;   // ~600 m across: the picture's pixels (2.3 m) are still about screen-sized

	/** The map on screen: a square of Side pixels at Origin, showing the UV window 1 / Zoom wide centred on Center. */
	struct FView
	{
		FVector2D Origin = FVector2D::ZeroVector;
		double Side = 1.0;
		FVector2D Center = FVector2D(0.5, 0.5);
		double Zoom = 1.0;

		/** The UV window's north-west corner and its width. */
		FVector2D WindowMin() const { return Center - FVector2D(0.5 / Zoom, 0.5 / Zoom); }
		double WindowSize() const { return 1.0 / Zoom; }
		HELLWALKERRL_API FVector2D UVToScreen(const FVector2D& UV) const;
		HELLWALKERRL_API FVector2D ScreenToUV(const FVector2D& Screen) const;
		/** Inside the square, Margin pixels in from its edges. */
		HELLWALKERRL_API bool Contains(const FVector2D& Screen, double Margin = 0.0) const;
	};
	/** Zoom into [MinZoom, MaxZoom]; the centre moved so that the window stays inside the valley. */
	HELLWALKERRL_API void ClampView(FVector2D& Center, double& Zoom);

	/** Margins (pixels) the edge indicators keep from each screen edge (the top one clears the compass and the objective). */
	struct FInsets
	{
		double Left = 0.0;
		double Top = 0.0;
		double Right = 0.0;
		double Bottom = 0.0;
	};

	/** Where a target's indicator sits on screen. */
	struct FEdgeMarker
	{
		FVector2D Pos = FVector2D::ZeroVector;   // pixels
		FVector2D Dir = FVector2D(0.0, 1.0);     // unit, screen space (+Y down): from the screen centre toward the target
		bool bOnScreen = false;                  // in front and inside the inset rectangle: Pos is its projection
	};

	/** A world point in camera space: X right, Y up, Z forward (cm). */
	HELLWALKERRL_API FVector ToCameraSpace(const FVector& Eye, const FRotator& View, const FVector& Target);
	/** The projection's scale in pixels: half the screen width over tan(half the horizontal field of view). */
	HELLWALKERRL_API double FocalPixels(double ScreenWidth, double HorizontalFovDegrees);
	/**
	 * The indicator for a target given in camera space. In front of the camera and inside the inset rectangle: on screen,
	 * at its projection. Otherwise on the rectangle's border, along the ray from the screen centre toward it — a target
	 * beside or behind the camera is folded downward by how far behind it is (straight behind = bottom centre), so the
	 * arrow never points away from it.
	 */
	HELLWALKERRL_API FEdgeMarker PlaceEdgeMarker(const FVector& CameraSpace, const FVector2D& Screen, double FocalPx, const FInsets& Insets);

	/** A keeper, as the tracking rule sees it. */
	struct FKeeperPin
	{
		FVector2D Pos = FVector2D::ZeroVector;   // cm
		bool bCleared = false;
		bool bSealed = false;
	};
	/** The current objective: the nearest keeper neither cleared nor sealed (so the final gate, once it opens); -1 if none. */
	HELLWALKERRL_API int32 DefaultTrackedKeeper(TConstArrayView<FKeeperPin> Keepers, const FVector2D& From);
	/** The keeper the player picked, while it stands (a sealed one may be tracked; a cleared one drops off), else the objective. */
	HELLWALKERRL_API int32 ResolveTrackedKeeper(TConstArrayView<FKeeperPin> Keepers, int32 Picked, const FVector2D& From);

	/** The valley from above: Size x Size pixels, row 0 the north edge, sRGB (FColor is BGRA in memory: PF_B8G8R8A8). */
	struct FPicture
	{
		int32 Size = 0;
		TArray<FColor> Pixels;
		/** Set (last) by the thread that renders it: only then may the game thread read Pixels. */
		std::atomic<bool> bReady{ false };
	};
	constexpr int32 PictureSize = 1024;   // 2.3 m a pixel; ~1 s to render on one pool thread, 4 MB
	/**
	 * The generator's ground colour (paths and plazas included), hill-shaded from the north-west (relief exaggerated),
	 * with faint 20 m contours; the mountain wall past the walkable valley is dimmed.
	 */
	HELLWALKERRL_API void RenderPicture(const FHWWorldGen& Gen, int32 Size, TArray<FColor>& OutPixels);
}
