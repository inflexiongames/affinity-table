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

#include "Styling/ISlateStyle.h"

/**
 * Convenience class that defines styles used by the editor and its UI components.
 */
class FAffinityTableStyles
{
public:
	// Numeric and parametric styles
	//////////////////////////////////////////////////////////////////////////

	/* 
	 *	Row Styles
	 */
	static const float RowHeaderMinWidth;
	static const float RowHeaderColorWidth;
	static const float RowCellMargin;

	/**
	 * Column styles
	 */
	static const float ColHeaderColorHeight;
	static const float ColCellMargin;

	/*
	 *	Cell Styles
	 */
	static const FColor CellBackgroundFocus;
	static const FColor CellReferenced;
	static const FColor CellTargeted;
	static const float CellPadding;
	static const float CellTextMargin;
	static const float CellBackgroundAlpha;
	static const float CellBackgroundDepthMultiplier;

	/**
	 * Other
	 */
	static const FColor AssetTypeColor;

	/** Picks and returns a color for a row or column header */
	static FLinearColor PickColor();

	// Styles that work with Slate's ISlateStyle model
	//////////////////////////////////////////////////////////////////////////

	/**
	 * Sets up our style objects
	 */
	static void Initialize();

	/**
	 * Closes our style objects
	 */
	static void Shutdown();

	/**
	 * Access to our styleset
	 */
	static const ISlateStyle& Get();

private:
	/** Initializes our style components */
	static TSharedRef<class FSlateStyleSet> CreateStyles();

	/** Shared style container */
	static TSharedPtr<class FSlateStyleSet> StyleInstance;
};
