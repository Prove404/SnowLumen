using UnrealBuildTool;

public class WorldClimData : ModuleRules
{
    public WorldClimData(ReadOnlyTargetRules Target) : base(Target)
    {
        // Editor-only module
        if (Target.bBuildEditor == true)
        {
            PrivateDependencyModuleNames.Add("UnrealEd");
        }

        PublicIncludePaths.AddRange(
            new string[] {
                "WorldClimData/Public",
            }
        );

        PrivateIncludePaths.AddRange(
            new string[] {
                "WorldClimData/Private",
                "WorldClimData/Classes",
            }
        );

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "Slate",
                "SlateCore",
                "AssetTools",
                "AssetRegistry",
                "UnrealEd",
                "EditorFramework",
                "Projects"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "InputCore",
                "Kismet",
                "PropertyEditor",
                "ContentBrowser"
            }
        );
    }
}
