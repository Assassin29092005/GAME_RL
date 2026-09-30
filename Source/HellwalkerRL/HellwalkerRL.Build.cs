using UnrealBuildTool;

public class HellwalkerRL : ModuleRules
{
	public HellwalkerRL(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Enhanced Input is enabled by default in 5.8; only the module dependency is needed (PLAN §5).
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput" });
		PrivateDependencyModuleNames.AddRange(new string[] { "PhysicsCore", "AssetRegistry", "Niagara", "ProceduralMeshComponent", "Mover" });
	}
}
