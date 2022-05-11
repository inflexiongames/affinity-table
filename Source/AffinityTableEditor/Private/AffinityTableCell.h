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
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SAffinityTableListViewRow;
class FAffinityTableNode;

/**
 * Contains the data required to visually edit a row/column intersection in our grid.
 * Manages UI appearance to reflect user actions such as mouse clicks. 
 */
class SAffinityTableCell : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAffinityTableCell)
	{
	}
	SLATE_ARGUMENT(TWeakPtr<FAffinityTableEditor>, Editor)
	SLATE_ARGUMENT(TWeakPtr<FAffinityTableNode>, RowNode)
	SLATE_ARGUMENT(TWeakPtr<FAffinityTableNode>, ColumnNode)
	SLATE_END_ARGS()

	/**
	 * States this cell responds to
	 */
	enum EState
	{
		// No special indicators
		Default,
		// This cell is selected for edit
		Selected,
		// This cell is referenced for copy
		Referenced,
		// This cell is referenced for paste
		Targeted
	};

	/** Constructs this widget */
	void Construct(const FArguments& InArgs);

	/** Widget Interfaces */

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	/**
	 * Changes the state of this cell
	 * @param InState New state for this cell
	 */
	void SetCellState(EState InState);

	/**
	 * Updates the description text on this cell based on the provided view
	 * @param View Pointer to the active view
	 */
	void UpdateDescription(const FAffinityTableEditor::PageView* View);

	/**
	 * Read/Write access to our cell
	 */
	FORCEINLINE TWeakPtr<FAffinityTableEditor::Cell> GetCell()
	{
		return Cell;
	}

	/**
	 * Read-only access to our row
	 */
	FORCEINLINE const TWeakPtr<FAffinityTableNode>& GetRow() const
	{
		return RowNode;
	}

	/**
	 * Read-only access to our column
	 */
	FORCEINLINE const TWeakPtr<FAffinityTableNode>& GetColumn() const
	{
		return ColumnNode;
	}

	/**
	 * Changes the state of this cell at rest, which is normally EState::Default
	 * @param InState New default state
	 */
	FORCEINLINE void SetDefaultState(EState InState)
	{
		DefaultState = InState;
	}

private:
	/**
	 * Returns the desired style for this cell based on its contents and status
	 * @param View The current page view
	 */
	const FTextBlockStyle* GetTextStyle(const FAffinityTableEditor::PageView* View);

	/**
	 * Returns a taxonomic color that ilustrates the depth level of this node
	 * @param BaseNode The node to color classify
	 */
	FLinearColor GetBackgroundColor(const FAffinityTableEditor::PageView* View);

	/** Full cell overlay for alternate states */
	TSharedPtr<class SColorBlock> FocusOverlay;

	/** Background color overlay */
	TSharedPtr<class SColorBlock> BackgroundOverlay;

	/** Reference to the application editor */
	TWeakPtr<FAffinityTableEditor> Editor;

	/** Reference to our structured cell on the editor */
	TWeakPtr<FAffinityTableEditor::Cell> Cell;

	/** Reference to our owning row node */
	TWeakPtr<FAffinityTableNode> RowNode;

	/** Reference to our owning column node */
	TWeakPtr<FAffinityTableNode> ColumnNode;

	/** Our current UI state */
	EState State;

	/** Our state when we are assigned default */
	EState DefaultState;

	/** UI for rendering textual data */
	TSharedPtr<class STextBlock> CellText;

	/** Style we use when we are inherited */
	FTextBlockStyle InheritedTextStyle;
};
