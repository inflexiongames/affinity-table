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


#include "AffinityTableHeader.h"
#include "AffinityTableEditor.h"
#include "AffinityTableNode.h"
#include "Slate/Public/Framework/Commands/GenericCommands.h"
#include "Slate/Public/Framework/MultiBox/MultiBoxBuilder.h"

void SAffinityTableHeader::Construct(const FArguments& InArgs)
{
	Node = InArgs._Node;
	Editor = InArgs._Editor;
	OnConstruct();
}

FReply SAffinityTableHeader::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	Editor.Pin()->ToggleNode(Node);
	return FReply::Unhandled();
}

FReply SAffinityTableHeader::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		const FGenericCommands GenericCommands = FGenericCommands::Get();
		FMenuBuilder MenuBuilder(true, nullptr);
		MenuBuilder.BeginSection("GridContextMenu");
		{
			MenuBuilder.AddMenuEntry(
				FText::FromString(TEXT("Set Color")),
				FText::FromString(TEXT("Sets the color of this header")),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([this]() { OnSetColor(); })),
				NAME_None,
				EUserInterfaceActionType::Button);

			MenuBuilder.AddMenuEntry(
				FText::FromString(TEXT("Delete")),
				FText::FromString(TEXT("Deletes this tag entry and all of its descendants")),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([this]() { OnDeleteHeader(); })),
				NAME_None,
				EUserInterfaceActionType::Button);
		}
		MenuBuilder.EndSection();

		FWidgetPath WidgetPath = MouseEvent.GetEventPath() ? *MouseEvent.GetEventPath() : FWidgetPath();
		FSlateApplication::Get().PushMenu(
			AsShared(),
			WidgetPath,
			MenuBuilder.MakeWidget(),
			MouseEvent.GetScreenSpacePosition(),
			FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));

		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SAffinityTableHeader::GatherUpstreamColors(TArray<FLinearColor>& Colors)
{
	const FAffinityTableNode* ThisNode = Node.Pin().Get();
	while (ThisNode && ThisNode->GetTag().IsValid())
	{
		Colors.Add(ThisNode->GetColor());
		ThisNode = ThisNode->GetParent();
	};
}
