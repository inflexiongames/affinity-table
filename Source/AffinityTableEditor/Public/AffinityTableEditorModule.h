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

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Toolkits/IToolkitHost.h"

class FAffinityTableEditor;
class UAffinityTable;
class IAssetTypeActions;

/**
 * Constructs Affinity Table editor instances when the user wants to edit table assets.
 * Stock code. 
 */
class FAffinityTableEditorModule : public IModuleInterface, public IHasMenuExtensibility, public IHasToolBarExtensibility
{
public:
	// IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	// end IModuleInterface

	// IHasMenuExtensibility and IHasToolBarExtensibility
	virtual TSharedPtr<FExtensibilityManager> GetMenuExtensibilityManager() override;
	virtual TSharedPtr<FExtensibilityManager> GetToolBarExtensibilityManager() override;
	// end IHasMenuExtensibility and IHasToolBarExtensibility

	/** Creates an editor for the provided affinity table */
	virtual TSharedRef<FAffinityTableEditor> CreateAffinityTableEditor(const EToolkitMode::Type Mode, const TSharedPtr<class IToolkitHost>& InitToolkitHost, UAffinityTable* Table);

	/** DataTable Editor app identifier string */
	static const FName AffinityTableEditorAppIdentifier;

private:
	/** Sets-up the tools and actions associated with our editor */
	void RegisterAssetTools();

	/** Tears-down the actions associated with our editor */
	void UnregisterAssetTools();

private:
	/** Hooks for menu and toolbar extensibility */
	TSharedPtr<FExtensibilityManager> MenuExtensibilityManager;
	TSharedPtr<FExtensibilityManager> ToolBarExtensibilityManager;

	/** Collection of actions for this asset */
	TArray<TSharedRef<IAssetTypeActions>> RegisteredAssetTypeActions;
};
