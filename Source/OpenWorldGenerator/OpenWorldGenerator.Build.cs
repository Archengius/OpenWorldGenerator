// Copyright Nikita Zolotukhin. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class OpenWorldGenerator : ModuleRules
{
	public OpenWorldGenerator(ReadOnlyTargetRules Target) : base(Target)
	{
		CppStandard = CppStandardVersion.Cpp20;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		
		PublicDependencyModuleNames.AddRange( new string[] { 
			"Core", 
			"CoreUObject",
			"Engine",
			"InputCore",
			"UMG",
			"EnhancedInput",
			"DeveloperSettings",
			"FastNoise2",
			"GeometryCore",
			"GeometryFramework", 
			"PCG",
			"Chaos",
			"ChaosSolverEngine",
			"PhysicsCore",
			"RenderCore",
			"DynamicMesh",
			"Foliage",
			"StructUtils"
		} );
		PrivateDependencyModuleNames.AddRange(new string[] {});
	}
}
