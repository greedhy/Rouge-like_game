// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class test_25d : ModuleRules
{
	public test_25d(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
	

        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "CoreOnline", "Engine", "InputCore", "EnhancedInput", "GameplayAbilities", "ModelViewViewModel", "NetCore", "OnlineSubsystem", "SlateCore" });

        PrivateDependencyModuleNames.AddRange(new string[] { "GameplayTags", "GameplayTasks", "NavigationSystem", "Niagara", "AIModule", "UMG", "Slate", "OnlineSubsystemUtils" });
        DynamicallyLoadedModuleNames.Add("OnlineSubsystemNull");
        PrivateDependencyModuleNames.Add("Json");
        PrivateDependencyModuleNames.Add("ProceduralMeshComponent");
        PublicDependencyModuleNames.Add("MassEntity");
        PublicDependencyModuleNames.Add("MassCore");
        PrivateDependencyModuleNames.Add("MassSimulation");
        if (Target.bBuildEditor)
            PrivateDependencyModuleNames.AddRange(new string[] { "MeshDescription", "StaticMeshDescription", "SkeletalMeshDescription", "AnimationCore" });
        // Uncomment if you are using Slate UI
        // PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

        // Uncomment if you are using online features
        // PrivateDependencyModuleNames.Add("OnlineSubsystem");

        // To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
    }
}
