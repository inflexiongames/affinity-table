/**
 * Copyright 2024 Inflexion Games. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
using UnrealBuildTool;

public class AffinityTableEditor : ModuleRules
{
    public AffinityTableEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicIncludePaths.AddRange(
            new string[] {
            }
            );


        PrivateIncludePaths.AddRange(
            new string[] {
			}
            );


        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "EditorStyle",
                "Engine",
                "InputCore",
                "LevelEditor",
                "Slate",
                "AssetTools",
                // For blueprint components
                "KismetCompiler",
                "KismetWidgets",
                "Kismet",
                "BlueprintGraph",
                "Slate",
                "SlateCore",
                "WorkspaceMenuStructure",
                "Projects"
            }
            );


        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "PropertyEditor",
                "SlateCore",
                "ApplicationCore",
                "UnrealEd",
                "Json",
                "JsonUtilities",
                "AffinityTable",
                "EditorWidgets",
                "GameplayTags",
                "AppFramework"
            }
            );


        DynamicallyLoadedModuleNames.AddRange(
            new string[]
            {
				// ... add any modules that your module loads dynamically here ...
			}
            );
    }
}
