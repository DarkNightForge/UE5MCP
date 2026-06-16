// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UE5MCP : ModuleRules
{
	public UE5MCP(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Projects",
			"InputCore",
			"EditorFramework",
			"UnrealEd",
			"ToolMenus",
			"CoreUObject",
			"DeveloperSettings",
			"Engine",
			"HTTPServer",
			"Slate",
			"SlateCore",
			"SourceControl",
			"Json",
			"JsonUtilities"
		});
	}
}
