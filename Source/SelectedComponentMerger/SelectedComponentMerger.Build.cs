using UnrealBuildTool;

public class SelectedComponentMerger : ModuleRules
{
    public SelectedComponentMerger(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "AssetRegistry",
            "AssetTools",
            "ContentBrowser",
            "Kismet",
            "MeshMergeUtilities",
            "Slate",
            "SlateCore",
            "SubobjectEditor",
            "ToolMenus",
            "UnrealEd"
        });
    }
}
