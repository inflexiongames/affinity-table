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
#include "AffinityTableCell.h"
#include "AffinityTableListViewRow.h"
#include "AffinityTableStyles.h"
#include "Brushes/SlateColorBrush.h"
#include "DataTableUtils.h"
#include "Widgets/Colors/SColorBlock.h"

void SAffinityTableCell::Construct(const FArguments& InArgs)
{
	Editor = InArgs._Editor;
	RowNode = InArgs._RowNode;
	ColumnNode = InArgs._ColumnNode;

	check(RowNode.IsValid());
	check(ColumnNode.IsValid());

	Cell = Editor.Pin()->GetCell(RowNode.Pin().Get(), ColumnNode.Pin().Get());
	check(Cell.IsValid());

	Cell.Pin()->UICell = SharedThis(this);
	FText Label = FText::FromString(TEXT("Empty"));
	State = EState::Default;
	DefaultState = EState::Default;

	static const FLinearColor BackgroundColorFocus(FAffinityTableStyles::CellBackgroundFocus);

	// We don't need a specific color now. the background color will be updated as soon as we update
	// the cell's contents. However, pick something transparent.
	FLinearColor BackgroundColor = FLinearColor::Transparent;

	ChildSlot
		[SNew(SHorizontalBox) + SHorizontalBox::Slot()
									.HAlign(HAlign_Fill)
									.VAlign(VAlign_Fill)
									.Padding(FAffinityTableStyles::CellPadding)
										[SNew(SOverlay)

											+ SOverlay::Slot()
												  [SAssignNew(BackgroundOverlay, SColorBlock)
														  .Color(BackgroundColor)]

											+ SOverlay::Slot()
												  [SAssignNew(FocusOverlay, SColorBlock)
														  .Color(BackgroundColorFocus)]

											+ SOverlay::Slot()
												  [SAssignNew(CellText, STextBlock)
														  .Margin(FMargin(FAffinityTableStyles::CellTextMargin))
														  .Text(Label)]]];
	FocusOverlay->SetVisibility(EVisibility::Hidden);

	TSharedPtr<FAffinityTableEditor> EditorPtr = Editor.Pin();
	const TWeakPtr<FAffinityTableEditor::PageView>& View = EditorPtr->GetActivePageView();
	if (View.IsValid())
	{
		UpdateDescription(View.Pin().Get());
	}
}

FReply SAffinityTableCell::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// Left mouse button always selects and handles. Right mouse button only does that if this cell hasn't
	// been previously selected. Otherwise it returns unhandled so we can open the context menu.
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton || State == DefaultState)
	{
		TSharedPtr<SAffinityTableCell> Shared = SharedThis(this);
		Editor.Pin()->SelectCell(Shared);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SAffinityTableCell::SetCellState(EState InState)
{
	if (InState != State)
	{
		State = InState == EState::Default ? DefaultState : InState;
		SColorBlock::FArguments ColorArgs;
		switch (State)
		{
			case EState::Selected:
				ColorArgs._Color = FAffinityTableStyles::CellBackgroundFocus;
				break;
			case EState::Referenced:
				ColorArgs._Color = FAffinityTableStyles::CellReferenced;
				break;
			case EState::Targeted:
				ColorArgs._Color = FAffinityTableStyles::CellTargeted;
				break;
		}
		FocusOverlay->SetVisibility(State == EState::Default ? EVisibility::Hidden : EVisibility::Visible);
		FocusOverlay->Construct(ColorArgs);
	}
}

void SAffinityTableCell::UpdateDescription(const FAffinityTableEditor::PageView* View)
{
	check(Cell.IsValid());
	TSharedPtr<FAffinityTableEditor::Cell> CellPtr = Cell.Pin();

	FString Desc;
	uint8* CellData = Editor.Pin()->GetTableBeingEdited()->GetCellData(CellPtr->TableCell, View->PageStruct);
	if (CellData && View->VisibleProperties.Num())
	{
		FString FullDesc;
		for (int32 i = 0; i < View->VisibleProperties.Num(); ++i)
		{
			if (i > 0)
			{
				FullDesc += ",\n";
			}
			FullDesc += DataTableUtils::GetPropertyValueAsText(View->VisibleProperties[i], CellData).ToString();
		}
		Desc = FullDesc;
	}
	// Temporary cell inheritance description: On the early stages of this editor, we are
	// experimenting with information that is useful to show when no property check box is ticked.
	else
	{
		if (CellPtr->InheritsData())
		{
			Desc = CellPtr->InheritedCell.Pin()->Row->GetTag().ToString() + ", " + CellPtr->InheritedCell.Pin()->Column->GetTag().ToString();
		}
		else
		{
			Desc = TEXT("[independent]");
		}
	}
	CellText->SetText(FText::FromString(Desc));
	CellText->SetTextStyle(GetTextStyle(View));

	// Reset our background color
	if (View->DisplayTaxonomyColor)
	{
		FLinearColor BackgroundColor = GetBackgroundColor(View);

		SColorBlock::FArguments BackgroundColorArgs;
		BackgroundColorArgs._Color = BackgroundColor;
		BackgroundOverlay->Construct(BackgroundColorArgs);
		BackgroundOverlay->SetVisibility(EVisibility::Visible);
	}
	else
	{
		BackgroundOverlay->SetVisibility(EVisibility::Hidden);
	}
}

const FTextBlockStyle* SAffinityTableCell::GetTextStyle(const FAffinityTableEditor::PageView* View)
{
	TSharedPtr<FAffinityTableEditor::Cell> CellPtr = Cell.Pin();
	if (CellPtr->InheritsData())
	{
		// A version of the inherited style, with custom color
		InheritedTextStyle = FAffinityTableStyles::Get().GetWidgetStyle<FTextBlockStyle>("AffinityTableEditor.CellTextInherited");
		InheritedTextStyle.SetColorAndOpacity(View->DisplayRowInheritance ? CellPtr->InheritedCell.Pin()->Row->GetColor() : CellPtr->InheritedCell.Pin()->Column->GetColor());
		return &InheritedTextStyle;
	}
	return &FAffinityTableStyles::Get().GetWidgetStyle<FTextBlockStyle>("AffinityTableEditor.CellText");
}

FLinearColor SAffinityTableCell::GetBackgroundColor(const FAffinityTableEditor::PageView* View)
{
	check(Cell.IsValid());

	TSharedPtr<FAffinityTableEditor::Cell> ThisCell = Cell.Pin();
	const FAffinityTableNode* BaseNode = View->DisplayRowInheritance ? ThisCell->Row : ThisCell->Column;
	float BaseAlpha = FAffinityTableStyles::CellBackgroundAlpha;
	FLinearColor Color = BaseNode->GetColor();

	// If we inherit data, render the distance to our parent. Make sure we are at least 1 level down (otherwise cells
	// sharing the same row/col as their parent will render with the same alpha)
	if (ThisCell->InheritsData())
	{
		const FAffinityTableNode* TargetNode = View->DisplayRowInheritance ? ThisCell->InheritedCell.Pin()->Row : ThisCell->InheritedCell.Pin()->Column;
		Color = TargetNode->GetColor();
		int32 Levels = BaseNode->GetTag().GetGameplayTagParents().Num() - TargetNode->GetTag().GetGameplayTagParents().Num() + 1;
		BaseAlpha *= pow(FAffinityTableStyles::CellBackgroundDepthMultiplier, Levels);
	}
	Color.A = BaseAlpha;
	return Color;
}
