/**
 * Copyright 2022 Inflexion Games. All Rights Reserved.
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

#include "AffinityTable.h"
#include "AffinityTableEditor.h"
#include "AffinityTableNode.h"
#include "CoreMinimal.h"
#include "Widgets/Views/STableRow.h"

/**
 * Represents one row in our affinity table editor. Required by STableListView.
 */
class SAffinityTableListViewRow : public SMultiColumnTableRow<FAffinityTableNode::NodeSharedPtr>
{
public:
	SLATE_BEGIN_ARGS(SAffinityTableListViewRow)
	{
	}
	// The row we are constructing
	SLATE_ARGUMENT(TWeakPtr<FAffinityTableNode>, RowNode)
	// Parent editor
	SLATE_ARGUMENT(TWeakPtr<FAffinityTableEditor>, Editor)
	SLATE_END_ARGS()

public:
	/** Stock. Configures an instance of this object */
	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView);

	/** Stock. Creates an SWidget for a cell in this row given the provided column name */
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& InColumnName) override;

private:
	/** Node representing the tag housed in this row */
	TWeakPtr<FAffinityTableNode> RowNode;

	/** Active table editor */
	TWeakPtr<FAffinityTableEditor> Editor;
};
