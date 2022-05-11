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

#include "AffinityTableStyles.h"

#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "Interfaces/IPluginManager.h"
#include "Slate/SlateGameResources.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"

#ifndef DEFAULT_FONT
#define DEFAULT_FONT(...) FCoreStyle::GetDefaultFontStyle(__VA_ARGS__)
#endif

// Numeric styles
//////////////////////////////////////////////////////////////////////////

const float FAffinityTableStyles::RowHeaderMinWidth = 150.0f;
const float FAffinityTableStyles::RowHeaderColorWidth = 5.0f;
const float FAffinityTableStyles::RowCellMargin = 5.0f;
const float FAffinityTableStyles::ColHeaderColorHeight = 5.0f;
const float FAffinityTableStyles::ColCellMargin = 5.0f;
const float FAffinityTableStyles::CellPadding = 2.f;
const float FAffinityTableStyles::CellTextMargin = 5.f;
const float FAffinityTableStyles::CellBackgroundAlpha = 0.6f;
const float FAffinityTableStyles::CellBackgroundDepthMultiplier = 0.5f;

const FColor FAffinityTableStyles::CellBackgroundFocus = FColor(161, 157, 175);
const FColor FAffinityTableStyles::CellReferenced = FColor(255, 237, 186);
const FColor FAffinityTableStyles::CellTargeted = FColor(108, 167, 123);
const FColor FAffinityTableStyles::AssetTypeColor = FColor(62, 140, 35);

TSharedPtr<FSlateStyleSet> FAffinityTableStyles::StyleInstance = nullptr;

// Automatic Color Selection
//////////////////////////////////////////////////////////////////////////

/**
 * An arbitrary palette of colors. These are cycled sequentially when the user
 * adds new rows or columns. Users can later customize the colors of each header,
 * so these are just a starting point. 
 */
namespace HeaderColors
{
FLinearColor NodeColors[] = {
	FLinearColor(FColor(116, 0, 184)),
	FLinearColor(FColor(105, 48, 195)),
	FLinearColor(FColor(94, 96, 206)),
	FLinearColor(FColor(83, 144, 217)),
	FLinearColor(FColor(78, 168, 222)),
	FLinearColor(FColor(72, 191, 227)),
	FLinearColor(FColor(86, 207, 225)),
	FLinearColor(FColor(100, 223, 223)),
	FLinearColor(FColor(114, 239, 221)),
	FLinearColor(FColor(128, 255, 219))
};

// Number of colors in our palette
uint8 ColorCount = 10;
// Current color
uint8 ColorMarker = 0;

}	 // namespace HeaderColors

FLinearColor FAffinityTableStyles::PickColor()
{
	return HeaderColors::NodeColors[HeaderColors::ColorMarker++ % HeaderColors::ColorCount];
}

// Styles
//////////////////////////////////////////////////////////////////////////

void FAffinityTableStyles::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = CreateStyles();
	}
}

void FAffinityTableStyles::Shutdown()
{
	if (StyleInstance.IsValid())
	{
		StyleInstance.Reset();
	}
}

const ISlateStyle& FAffinityTableStyles::Get()
{
	return *StyleInstance;
}

TSharedRef<class FSlateStyleSet> FAffinityTableStyles::CreateStyles()
{
	const FTextBlockStyle NormalText = FEditorStyle::GetWidgetStyle<FTextBlockStyle>("NormalText");
	TSharedRef<FSlateStyleSet> Style = MakeShareable(new FSlateStyleSet("AffinityTableEditorStyle"));

	/** Row headers */
	FTableRowStyle RowStyle = FCoreStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row");
	RowStyle
		.SetInactiveBrush(*FEditorStyle::GetBrush("NoBrush"))
		.SetInactiveHoveredBrush(*FEditorStyle::GetBrush("NoBrush"))
		.SetActiveBrush(*FEditorStyle::GetBrush("NoBrush"))
		.SetActiveHoveredBrush(*FEditorStyle::GetBrush("NoBrush"))
		.SetSelectedTextColor(RowStyle.TextColor);

	Style->Set("AffinityTableEditor.RowHeader", RowStyle);

	/** Cell text, inherited */
	FTextBlockStyle CellTextInherited = FTextBlockStyle(NormalText)
											.SetFont(DEFAULT_FONT("Regular", 10))
											.SetColorAndOpacity(FLinearColor(0.37f, 0.11f, 0.23f));

	Style->Set("AffinityTableEditor.CellTextInherited", CellTextInherited);

	/** Cell text, normal */
	FTextBlockStyle CellTextNormal = FTextBlockStyle(NormalText)
										 .SetFont(DEFAULT_FONT("Bold", 10))
										 .SetColorAndOpacity(FLinearColor::White);

	Style->Set("AffinityTableEditor.CellText", CellTextNormal);

	// Asset Icon
	const FString BaseDir = IPluginManager::Get().FindPlugin("AffinityTable")->GetBaseDir();
	Style->SetContentRoot(BaseDir / TEXT("Content"));
	Style->Set("ClassIcon.AffinityTable", new FSlateImageBrush(Style->RootToContentDir(TEXT("AffinityTableIcon16"), TEXT(".png")), FVector2D(16, 16)));
	Style->Set("ClassThumbnail.AffinityTable", new FSlateImageBrush(Style->RootToContentDir(TEXT("AffinityTableIcon64"), TEXT(".png")), FVector2D(64, 64)));
	FSlateStyleRegistry::RegisterSlateStyle(Style.Get());

	return Style;
}

#undef DEFAULT_FONT
