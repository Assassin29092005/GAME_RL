#include "HellwalkerRL.h"

#include "Modules/ModuleManager.h"
#include "Animation/AnimTypes.h"          // ENABLE_ANIM_DEBUG
#include "HAL/IConsoleManager.h"
#include "VisualLogger/VisualLogger.h"    // ENABLE_VISUAL_LOG

DEFINE_LOG_CATEGORY(LogHellwalkerRL);

// The Game Animation Sample's Blueprints (the open-world explorer) read two engine console variables to decide whether to
// run Offset Root Bone and Foot Placement. The engine registers them only in builds with animation debugging, so in a
// Shipping (or Test) build they do not exist, the Blueprints read 0 and switch both off: the explorer's root drifted down
// with the slide's root motion and the body sank into the ground (found 2026-10-05 by dumping the pose in Shipping vs
// Development). Register them with the engine's defaults wherever the engine does not.
#if !ENABLE_ANIM_DEBUG
static TAutoConsoleVariable<int32> CVarHWOffsetRootBoneEnable(TEXT("a.AnimNode.OffsetRootBone.Enable"), 1,
	TEXT("Toggle Offset Root Bone (registered by HellwalkerRL where the engine's animation debugging is compiled out)."));
#endif
#if !(ENABLE_ANIM_DEBUG && ENABLE_VISUAL_LOG)
static TAutoConsoleVariable<bool> CVarHWFootPlacementEnable(TEXT("a.AnimNode.FootPlacement.Enable"), true,
	TEXT("Enable/Disable Foot Placement (registered by HellwalkerRL where the engine's foot placement debugging is compiled out)."));
#endif

IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, HellwalkerRL, "HellwalkerRL");
