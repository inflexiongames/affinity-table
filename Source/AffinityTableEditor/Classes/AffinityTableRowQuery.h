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

#include "AffinityTableQueryBase.h"

#include "AffinityTableRowQuery.generated.h"

/**
 * Queries structure datasets from a specific AffinityTable asset based on
 * row and column gameplay tags. 
 */
UCLASS()
class UK2Node_AffinityTableRowQuery : public UK2Node_AffinityTableQueryBase
{
	GENERATED_UCLASS_BODY()

public:
	// UEdGraphNode interface
	virtual void AllocateDefaultPins() override;
	virtual FText GetTooltipText() const override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual void ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	// End of UEdGraphNode interface

private:
	/** Create new output pins on this node based on our queried AffinityTable */
	virtual void RefreshStructurePins() override;

	/** Human-readable tooltip for our node */
	static FText NodeTooltip;

	/** Human-readable title for our node */
	static FText NodeTitle;
};
