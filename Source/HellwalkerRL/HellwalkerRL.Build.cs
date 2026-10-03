using UnrealBuildTool;

public class HellwalkerRL : ModuleRules
{
	public HellwalkerRL(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Enhanced Input is enabled by default in 5.8; only the module dependency is needed (PLAN §5).
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput" });
		// RenderCore: GWhiteTexture for the HUD's filled shapes (the map's icons, the keeper indicators).
		// HTTP + Json: the anonymous research telemetry (HWTelemetry, Firebase over REST).
		PrivateDependencyModuleNames.AddRange(new string[] { "PhysicsCore", "AssetRegistry", "Niagara", "ProceduralMeshComponent", "Mover", "RenderCore", "HTTP", "Json" });
	}
}
