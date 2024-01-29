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
#include "AffinityTableEditorModule.h"
#include "AffinityTable.h"
#include "AffinityTableActions.h"
#include "AffinityTableEditor.h"
#include "AffinityTableStyles.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"

const FName FAffinityTableEditorModule::AffinityTableEditorAppIdentifier(TEXT("AffinityTableEditorApp"));

TSharedPtr<FExtensibilityManager> FAffinityTableEditorModule::GetMenuExtensibilityManager()
{
	return MenuExtensibilityManager;
}

TSharedPtr<FExtensibilityManager> FAffinityTableEditorModule::GetToolBarExtensibilityManager()
{
	return ToolBarExtensibilityManager;
}

void FAffinityTableEditorModule::StartupModule()
{
	MenuExtensibilityManager = MakeShareable(new FExtensibilityManager);
	ToolBarExtensibilityManager = MakeShareable(new FExtensibilityManager);

	RegisterAssetTools();
}

void FAffinityTableEditorModule::ShutdownModule()
{
	MenuExtensibilityManager.Reset();
	ToolBarExtensibilityManager.Reset();

	UnregisterAssetTools();
}

TSharedRef<FAffinityTableEditor> FAffinityTableEditorModule::CreateAffinityTableEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UAffinityTable* Table)
{
	// We currently have no need to hide our editor under an interface, there is only one kind and
	// there are no particular configuration parameters. We can change that in the future if needed
	// and return an IAffinityTableEditor here.
	TSharedRef<FAffinityTableEditor> NewAffinityTableEditor(new FAffinityTableEditor());
	NewAffinityTableEditor->InitAffinityTableEditor(Mode, InitToolkitHost, Table);
	return NewAffinityTableEditor;
}

void FAffinityTableEditorModule::RegisterAssetTools()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	// Register available actions
	TSharedRef<IAssetTypeActions> AffinityTableActions = MakeShareable(new FAffinityTableActions);
	AssetTools.RegisterAssetTypeActions(AffinityTableActions);
	RegisteredAssetTypeActions.Add(AffinityTableActions);

	// Styles
	FAffinityTableStyles::Initialize();

	// ...
}

void FAffinityTableEditorModule::UnregisterAssetTools()
{
	FAssetToolsModule* AffinityTableModule = FModuleManager::GetModulePtr<FAssetToolsModule>("AssetTools");
	if (AffinityTableModule != nullptr)
	{
		IAssetTools& AssetTools = AffinityTableModule->Get();
		for (auto Action : RegisteredAssetTypeActions)
		{
			AssetTools.UnregisterAssetTypeActions(Action);
		}
	}

	// Styles
	FAffinityTableStyles::Shutdown();
}

IMPLEMENT_MODULE(FAffinityTableEditorModule, AffinityTableEditor)
