// Copyright Nikita Zolotukhin. All Rights Reserved.

using System;
using UnrealBuildTool;
using System.IO;

public class FastNoise2 : ModuleRules
{
	public FastNoise2(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;
		PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, "include"));

		string libraryCommonPath = Path.Combine(ModuleDirectory, "lib", Target.Platform.ToString(), "FastNoise");

		// TODO @open-world-generator: Compile FastNoise2 for other platforms (Linux at least)
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicAdditionalLibraries.Add(Path.ChangeExtension(libraryCommonPath, "lib"));

			RuntimeDependencies.Add("$(BinaryOutputDir)/FastNoise.dll", Path.ChangeExtension(libraryCommonPath, "dll"));
			PublicRuntimeLibraryPaths.Add( Path.ChangeExtension(libraryCommonPath, "dll") );
		}
	}
}
