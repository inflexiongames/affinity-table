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
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/SCompoundWidget.h"

class FAffinityTableNode;
class FAffinityTableEditor;

/**
 * Base class for table row and column headers.
 * Takes care of some mechanical logistics. Relies on concrete implementations for the final widget look.
 */
class SAffinityTableHeader : public SCompoundWidget
{
public:
	// -V:SLATE_BEGIN_ARGS:832, Constructor is obfuscated by macro
	SLATE_BEGIN_ARGS(SAffinityTableHeader)
	{
	}
	SLATE_ARGUMENT(TWeakPtr<FAffinityTableNode>, Node)
	SLATE_ARGUMENT(TWeakPtr<FAffinityTableEditor>, Editor)
	SLATE_END_ARGS()

public:
	/** Furnish a table header */
	void Construct(const FArguments& InArgs);

	/** Handle mouse double-clicks */
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	/** Handle mouse right-clicks  */
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	/** Access to our contained node */
	FORCEINLINE TWeakPtr<FAffinityTableNode>& GetNode()
	{
		return Node;
	}

protected:
	/**
	 * Gathers the colors of this node and all of its valid parents
	 * @param Colors Array to hold the colors in the tree
	 */
	void GatherUpstreamColors(TArray<FLinearColor>& Colors);

	/** Concrete class construction mechanism. Expected to at least set ChildSlot */
	virtual void OnConstruct() = 0;

	/** Handle the user deleting this header */
	virtual void OnDeleteHeader() = 0;

	/** Handle the using configuring the color of this header */
	virtual void OnSetColor() = 0;

	/** Returns the name for this header based on the node's state */
	FText MakeHeaderName() const;

	/** Reference to the node we are created after */
	TWeakPtr<FAffinityTableNode> Node;

	/** Access to our editor */
	TWeakPtr<FAffinityTableEditor> Editor;
};
