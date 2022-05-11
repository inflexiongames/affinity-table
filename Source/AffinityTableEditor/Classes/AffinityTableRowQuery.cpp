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


#include "AffinityTableRowQuery.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_MakeArray.h"
#include "K2Node_TemporaryVariable.h"
#include "KismetCompiler.h"

#define LOCTEXT_NAMESPACE "UK2Node_AffinityTableRowQuery"

FText UK2Node_AffinityTableRowQuery::NodeTitle(LOCTEXT("AffinityTableRowQuery_Title", "Query Affinity Table Row"));
FText UK2Node_AffinityTableRowQuery::NodeTooltip(LOCTEXT("AffinityTableRowQuery_Tooltip", "Queries an affinity table row"));

UK2Node_AffinityTableRowQuery::UK2Node_AffinityTableRowQuery(const FObjectInitializer& ObjectInitializer) :
	Super(ObjectInitializer)
{
}

void UK2Node_AffinityTableRowQuery::AllocateDefaultPins()
{
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

	// Execute
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Execute);

	// Query match and query mismatch
	UEdGraphPin* FoundPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Then);
	FoundPin->PinFriendlyName = LOCTEXT("AffinityTableRowQuery_Successful", "Match Found");
	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, QueryUnsuccessful);

	// Input for our datatable
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Object, UAffinityTable::StaticClass(), TablePinName);

	// Query tags
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Struct, FGameplayTag::StaticStruct(), RowPinName);

	// Whether we require an exact match
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Boolean, ExactMatchPinName);

	// Pins for our specific affinity table
	RefreshStructurePins();

	Super::AllocateDefaultPins();
}

FText UK2Node_AffinityTableRowQuery::GetTooltipText() const
{
	return NodeTooltip;
}

FText UK2Node_AffinityTableRowQuery::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return NodeTitle;
}

void UK2Node_AffinityTableRowQuery::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	const UEdGraphSchema_K2* Schema = CompilerContext.GetSchema();

	// functions and their parameter names in UAffinityTableBlueprintLibrary
	static const FName QueryFunctionName = GET_FUNCTION_NAME_CHECKED(UAffinityTableBlueprintLibrary, QueryTableForRow);
	static const FName GetTableCellDataFunctionName = GET_FUNCTION_NAME_CHECKED(UAffinityTableBlueprintLibrary, GetTableCellsData);
	static const TCHAR* TableParamName = TEXT("Table");
	static const TCHAR* RowParamName = TEXT("RowTag");
	static const TCHAR* ExactMatchParamName = TEXT("ExactMatch");

	// Connects an input pin to an input function parameter
	auto ConnectInput = [this, &CompilerContext](UK2Node_CallFunction* Function, const FName& From, const TCHAR* To) {
		UEdGraphPin* FromPin = GetInputPin(From);
		UEdGraphPin* ToPin = Function->FindPinChecked(To);
		check(FromPin && ToPin);

		if (FromPin->LinkedTo.Num())
		{
			CompilerContext.MovePinLinksToIntermediate(*FromPin, *ToPin);
		}
		else
		{
			ToPin->DefaultObject = FromPin->DefaultObject;
			ToPin->DefaultValue = FromPin->DefaultValue;
		}
	};

	RefreshDatatable();

	if (!ValidateConnections(CompilerContext.MessageLog))
	{
		BreakAllNodeLinks();
		return;
	}

	// Query function
	//////////////////////////////////////////////////////////////////////////

	UK2Node_CallFunction* QueryFunction = SpawnAffinityTableFunction(QueryFunctionName, CompilerContext, SourceGraph);
	CompilerContext.MovePinLinksToIntermediate(*GetExecPin(), *(QueryFunction->GetExecPin()));

	// Connect query parameters to the function's input. Ignore Structs that have no output vars
	ConnectInput(QueryFunction, TablePinName, TableParamName);
	ConnectInput(QueryFunction, RowPinName, RowParamName);
	ConnectInput(QueryFunction, ExactMatchPinName, ExactMatchParamName);

	// An array with the structures we are interested in
	//////////////////////////////////////////////////////////////////////////

	UK2Node_MakeArray* StructureArray = CompilerContext.SpawnIntermediateNode<UK2Node_MakeArray>(this, SourceGraph);
	StructureArray->AllocateDefaultPins();
	UEdGraphPin* StructureArrayOut = StructureArray->GetOutputPin();

	// Connect available structures to our array
	StructureArrayOut->MakeLinkTo(QueryFunction->FindPinChecked(TEXT("StructureTypes")));
	StructureArray->PinConnectionListChanged(StructureArrayOut);

	TArray<UEdGraphPin*> OutputStructurePins;
	if (TableAsset)
	{
		int32 InsertedStructs = 0;
		for (UEdGraphPin* Pin : Pins)
		{
			// Rely on UE's connection type validation: All of our output structures are Affinity table structures.
			if (IsOutputStructPin(Pin) && Pin->LinkedTo.Num())
			{
				UScriptStruct* PinStruct = Cast<UScriptStruct>(Pin->LinkedTo[0]->PinType.PinSubCategoryObject.Get());
				if (PinStruct)
				{
					OutputStructurePins.Add(Pin);

					if (InsertedStructs)
					{
						StructureArray->AddInputPin();
					}

					UEdGraphPin* PinSlot = StructureArray->FindPinChecked(StructureArray->GetPinName(InsertedStructs++));
					Schema->TrySetDefaultObject(*PinSlot, PinStruct);
				}
			}
		}
	}

	// Branch node for success/failure routing
	//////////////////////////////////////////////////////////////////////////

	UK2Node_IfThenElse* BranchNode = CompilerContext.SpawnIntermediateNode<UK2Node_IfThenElse>(this, SourceGraph);
	BranchNode->AllocateDefaultPins();

	// inputs
	QueryFunction->GetThenPin()->MakeLinkTo(BranchNode->GetExecPin());
	QueryFunction->FindPinChecked(UEdGraphSchema_K2::PN_ReturnValue)->MakeLinkTo(BranchNode->GetConditionPin());

	// Parameter extraction for each structure
	//////////////////////////////////////////////////////////////////////////

	UEdGraphPin* MemoryPointersPin = QueryFunction->FindPinChecked(TEXT("OutMemoryPtrs"));
	UEdGraphPin* ExecutionChain = BranchNode->GetThenPin();
	for (int32 i = 0; i < OutputStructurePins.Num(); ++i)
	{
		UK2Node_CallFunction* DataExtractionFunction = SpawnAffinityTableFunction(GetTableCellDataFunctionName, CompilerContext, SourceGraph);

		// Struct type
		UEdGraphPin* OutputStructurePin = OutputStructurePins[i];
		UScriptStruct* DataStruct = Cast<UScriptStruct>(OutputStructurePin->LinkedTo[0]->PinType.PinSubCategoryObject.Get());
		UEdGraphPin* DataPin = DataExtractionFunction->FindPinChecked(TEXT("StructType"));
		Schema->TrySetDefaultObject(*DataPin, DataStruct);

		// Array index
		UEdGraphPin* IndexPin = DataExtractionFunction->FindPinChecked(TEXT("DataIndex"));
		IndexPin->DefaultValue = FString::FromInt(i);

		// Data wrappers
		MemoryPointersPin->MakeLinkTo(DataExtractionFunction->FindPinChecked(TEXT("MemoryPtrs")));

		// Output
		UEdGraphPin* DataOutputPin = DataExtractionFunction->FindPinChecked(TEXT("OutData"));
		DataOutputPin->PinType = OutputStructurePin->PinType;
		DataOutputPin->PinType.PinSubCategoryObject = OutputStructurePin->PinType.PinSubCategoryObject;

		// Execution chain
		CompilerContext.MovePinLinksToIntermediate(*OutputStructurePin, *DataOutputPin);
		ExecutionChain->MakeLinkTo(DataExtractionFunction->GetExecPin());
		ExecutionChain = DataExtractionFunction->GetThenPin();
	}

	// Final output wiring
	CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(UEdGraphSchema_K2::PN_Then), *ExecutionChain);
	CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(QueryUnsuccessful), *(BranchNode->GetElsePin()));

	BreakAllNodeLinks();
}

void UK2Node_AffinityTableRowQuery::RefreshStructurePins()
{
	for (UEdGraphPin* OldPin : StructPins)
	{
		DestroyPin(OldPin);
	}
	StructPins.Empty(TableAsset ? TableAsset->Structures.Num() : 0);

	if (TableAsset != nullptr)
	{
		for (UScriptStruct* Structure : TableAsset->Structures)
		{
			if (Structure)
			{
				//since we are query the row, return an array of all cells across a row
				UEdGraphNode::FCreatePinParams Params = UEdGraphNode::FCreatePinParams();
				Params.ContainerType = EPinContainerType::Array;
				Params.bIsReference = true;
				StructPins.Add(CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Struct, Structure, Structure->GetFName(), Params));
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
