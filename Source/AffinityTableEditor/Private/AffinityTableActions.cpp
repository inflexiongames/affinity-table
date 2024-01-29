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
#include "AffinityTableActions.h"
#include "AffinityTable.h"
#include "AffinityTableEditor.h"
#include "AffinityTableStyles.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

FText FAffinityTableActions::GetName() const
{
	return LOCTEXT("FAffinityTableActionName", "AffinityTable");
}

FColor FAffinityTableActions::GetTypeColor() const
{
	return FAffinityTableStyles::AssetTypeColor;
}

UClass* FAffinityTableActions::GetSupportedClass() const
{
	return UAffinityTable::StaticClass();
}

void FAffinityTableActions::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<class IToolkitHost> EditWithinLevelEditor)	// -V813 (cannot change UE-defined signature)
{
	const EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

	for (auto ObjIt = InObjects.CreateConstIterator(); ObjIt; ++ObjIt)
	{
		if (UAffinityTable* Table = Cast<UAffinityTable>(*ObjIt))
		{
			TSharedRef<FAffinityTableEditor> NewTableEditor(new FAffinityTableEditor());
			NewTableEditor->InitAffinityTableEditor(Mode, EditWithinLevelEditor, Table);
		}
	}
}

uint32 FAffinityTableActions::GetCategories()
{
	return EAssetTypeCategories::Misc;
}

#undef LOCTEXT_NAMESPACE
