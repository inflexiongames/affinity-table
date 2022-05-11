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
#include "CoreMinimal.h"
#include "EdGraph/EdGraphNodeUtils.h"
#include "K2Node.h"
#include "UObject/ObjectMacros.h"

#include "AffinityTableQueryBase.generated.h"

class UEdGraph;
class FKismetCompilerContext;

/**
 * Queries structure datasets from a specific AffinityTable asset based on
 * row and column gameplay tags. 
 */
UCLASS(Abstract)
class UK2Node_AffinityTableQueryBase : public UK2Node
{
	GENERATED_UCLASS_BODY()

public:
	// UEdGraphNode interface
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual void PinDefaultValueChanged(UEdGraphPin* Pin) override;
	// End of UEdGraphNode interface

	// UK2Node interface
	virtual void EarlyValidation(class FCompilerResultsLog& MessageLog) const override;
	virtual void PreloadRequiredAssets() override;

	virtual bool IsConnectionDisallowed(const UEdGraphPin* MyPin, const UEdGraphPin* OtherPin, FString& OutReason) const;
	// End of UK2Node interface

	// UEdGraphNodeInterface
	virtual void ValidateNodeDuringCompilation(class FCompilerResultsLog& MessageLog) const override;
	// End of UEdGraphNodeInerface
protected:
	/**
	 * Re-acquire the datatable from our designated pin. Returns true if we have a new
	 * table and the node has to be reconstructed
	 */
	bool RefreshDatatable();

	/** Create new output pins on this node based on our queried AffinityTable */
	virtual void RefreshStructurePins();

	/** Verifies that the data connected to this table is sane */
	bool ValidateConnections(class FCompilerResultsLog& MessageLog) const;

	/** Shorthand for testing if this pin connects to an output structure */
	bool IsOutputStructPin(const UEdGraphPin* Pin) const;

	/** Provides our execution pin for successful queries */
	UEdGraphPin* GetQuerySuccessfulPin() const;

	/** Provides our execution pin for failed queries */
	UEdGraphPin* GetQueryUnsuccessfulPin() const;

	/** Returns an input pin labeled after the provided name */
	UEdGraphPin* GetInputPin(const FName& PinName) const;

	/** Spawns a CallFunction node bound to a function in our UAffinityTableBlueprintLibrary class */
	class UK2Node_CallFunction* SpawnAffinityTableFunction(const FName& FunctionName, FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph);

	/** Table asset we use for queries */
	UAffinityTable* TableAsset;

	/** Cached list of output pins */
	TArray<UEdGraphPin*> StructPins;

	/** Name of the datatable input pin */
	static FName TablePinName;

	/** Name of our row tag pin */
	static FName RowPinName;

	/** Name for our column tag pin */
	static FName ColumnPinName;

	/** Name for our exact match boolean flag */
	static FName ExactMatchPinName;

	/** Name for our QueryUnsuccessful execution pin */
	static FName QueryUnsuccessful;
};
