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
#include "AffinityTableListViewRow.h"

#include "AffinityTableCell.h"
#include "AffinityTableEditor.h"
#include "AffinityTableNode.h"
#include "AffinityTableStyles.h"

#include "AffinityTableHeader.h"
#include "Framework/Commands/GenericCommands.h"
#include "Misc/MessageDialog.h"
#include "ScopedTransaction.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/SCompoundWidget.h"

#define LOCTEXT_NAMESPACE "AffinityTableEditor"

class SAffinityTableRowHeader;

/**
 * Manages the behavior and appearance of row headers.
 */
class SAffinityTableRowHeader : public SAffinityTableHeader
{
protected:
	virtual void OnConstruct()
	{
		check(Node.IsValid());
		FAffinityTableNode::NodeSharedPtr Ptr = Node.Pin();

		// Gather colors from our parents
		TArray<FLinearColor> Colors;
		GatherUpstreamColors(Colors);
		int32 HandleCount = Colors.Num();

		// Horizontal box with our colors, plus that of our parents
		TSharedRef<SHorizontalBox> HandleHolder = SNew(SHorizontalBox);
		while (Colors.Num())
		{
			HandleHolder->AddSlot().MaxWidth(FAffinityTableStyles::RowHeaderColorWidth)[SNew(SColorBlock).Color(Colors.Pop())];
		}

		// An overlay to contain all of our components
		TSharedRef<SOverlay> RowOverlay = SNew(SOverlay);

		// Base overlay with text description
		float CellMargin = FAffinityTableStyles::RowCellMargin;
		RowOverlay->AddSlot()
			[SNew(SBorder)
					.BorderImage(&FCoreStyle::Get().GetWidgetStyle<FHeaderRowStyle>("TableView.Header").BackgroundBrush)
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Fill)
						[SNew(STextBlock)
								.Margin(FMargin(CellMargin + FAffinityTableStyles::RowHeaderColorWidth * HandleCount, CellMargin, CellMargin, CellMargin))
								.Text(MakeHeaderName())]];

		// Top color margin for expanded rows
		if (!Ptr->IsCollapsed() && Ptr->HasChildren())
		{
			RowOverlay->AddSlot()
				.VAlign(VAlign_Top)
					[SNew(SVerticalBox) + SVerticalBox::Slot()
											  .MaxHeight(3.f)
												  [SNew(SColorBlock).Color(Ptr->GetColor())]];
		}

		// Handles
		RowOverlay->AddSlot()[HandleHolder];
		ChildSlot[RowOverlay];
	}

	virtual void OnDeleteHeader()
	{
		FText WarningMessage(LOCTEXT("AT_Warning_DeleteRow", "Are you sure you want to delete this row?"));
		if (EAppReturnType::Yes == FMessageDialog::Open(EAppMsgType::YesNo, WarningMessage))
		{
			const FScopedTransaction Transaction(LOCTEXT("AT_Transaction_DeleteRow", "Delete Row"));
			TSharedPtr<FAffinityTableEditor> EditorPtr = Editor.Pin();
			EditorPtr->GetTableBeingEdited()->Modify();
			EditorPtr->DeleteRow(Node);
		}
	}

	virtual void OnSetColor()
	{
		Editor.Pin()->PickColorForHeader(this, true);
	}
};

void SAffinityTableListViewRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView)
{
	RowNode = InArgs._RowNode;
	Editor = InArgs._Editor;

	SMultiColumnTableRow<FAffinityTableNode::NodeSharedPtr>::Construct(
		FSuperRowType::FArguments().Style(&FAffinityTableStyles::Get().GetWidgetStyle<FTableRowStyle>("AffinityTableEditor.RowHeader")), InOwnerTableView);
}

TSharedRef<SWidget> SAffinityTableListViewRow::GenerateWidgetForColumn(const FName& InColumnName)
{
	if (InColumnName == FAffinityTableEditor::ColumnHeaderName)
	{
		return SNew(SAffinityTableRowHeader).Node(RowNode).Editor(Editor);
	}
	else
	{
		TWeakPtr<FAffinityTableNode> ColumnNode = Editor.Pin()->GetNodeForColumn(InColumnName);
		return SNew(SAffinityTableCell).Editor(Editor).RowNode(RowNode).ColumnNode(ColumnNode);
	}
}

#undef LOCTEXT_NAMESPACE
