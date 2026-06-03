using System.IO;
using UnrealBuildTool;

public class FreeRig_VT : ModuleRules
{
    public FreeRig_VT(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "Json",
            "JsonUtilities",
            "EnhancedInput",
            "Paper2D",
            "LiveLink",
            "LiveLinkInterface",
            "LiveLinkAnimationCore",
            "Networking",
            "Media",
            "MediaAssets",
            "MediaUtils",
            "Sockets",
            "Slate",
            "SlateCore",
            "UMG",
            "WebSocketNetworking",
        });  

        // Add the private include path for WebSocketServer.h
        PrivateIncludePaths.Add(Path.Combine(EngineDirectory, "Plugins/Experimental/WebSocketNetworking/Source/WebSocketNetworking/Private"));
    }
}