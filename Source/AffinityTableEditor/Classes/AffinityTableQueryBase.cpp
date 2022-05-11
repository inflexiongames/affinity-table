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


#include "AffinityTableQueryBase.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_MakeArray.h"
#include "K2Node_TemporaryVariable.h"
#include "KismetCompiler.h"

#define LOCTEXT_NAMESPACE "UK2Node_AffinityTableQueryBase"

FName UK2Node_AffinityTableQueryBase::TablePinName(TEXT("Table"));
FName UK2Node_AffinityTableQueryBase::RowPinName(TEXT("Row Tag"));
FName UK2Node_AffinityTableQueryBase::ColumnPinName(TEXT("Column Tag"));
FName UK2Node_AffinityTableQueryBase::ExactMatchPinName(TEXT("Exact Match"));
FName UK2Node_AffinityTableQueryBase::QueryUnsuccessful(TEXT("Match Not Found"));

UK2Node_AffinityTableQueryBase::UK2Node_AffinityTableQueryBase(const FObjectInitializer& ObjectInitializer) :
	Super(ObjectInitializer),
	TableAsset(nullptr)
{
}

void UK2Node_AffinityTableQueryBase::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
		check(NodeSpawner != nullptr);

		ActionRegistrar.AddBlueprintAction(ActionKey, NodeSpawner);
	}
}

void UK2Node_AffinityTableQueryBase::PinDefaultValueChanged(UEdGraphPin* Pin)
{
	Super::PinDefaultValueChanged(Pin);
	if (Pin == GetInputPin(UK2Node_AffinityTableQueryBase::TablePinName))
	{
		if (RefreshDatatable())
		{
			GetGraph()->NotifyGraphChanged();
			ReconstructNode();
		}
	}
}

void UK2Node_AffinityTableQueryBase::EarlyValidation(FCompilerResultsLog& MessageLog) const
{
	Super::EarlyValidation(MessageLog);
	ValidateConnections(MessageLog);
}

void UK2Node_AffinityTableQueryBase::PreloadRequiredAssets()
{
	UEdGraphPin* TablePin = GetInputPin(TablePinName);
	RefreshDatatable();

	if (TableAsset)
	{
		PreloadObject(TableAsset);
	}

	Super::PreloadRequiredAssets();
}

bool UK2Node_AffinityTableQueryBase::IsConnectionDisallowed(const UEdGraphPin* MyPin, const UEdGraphPin* OtherPin, FString& OutReason) const
{
	if (MyPin == GetInputPin(TablePinName))
	{
		OutReason = TEXT("Because outputs are customized to a specific AffinityTable, this cannot be a variable. Please select an asset from the dropdown");
		return true;
	}
	return false;
}

void UK2Node_AffinityTableQueryBase::ValidateNodeDuringCompilation(class FCompilerResultsLog& MessageLog) const
{
	Super::ValidateNodeDuringCompilation(MessageLog);
	ValidateConnections(MessageLog);
}

bool UK2Node_AffinityTableQueryBase::RefreshDatatable()
{
	UEdGraphPin* TablePin = GetInputPin(TablePinName);

	// We look at our default value rather than exploring the pin links because the links themselves are
	// not supported: we create output connections on compile time based on the specific table we query.
	UAffinityTable* NewTableAsset = Cast<UAffinityTable>(TablePin->DefaultObject);
	if (NewTableAsset != TableAsset)
	{
		TableAsset = NewTableAsset;
		return true;
	}
	return false;
}

void UK2Node_AffinityTableQueryBase::RefreshStructurePins()
{
	check(0 && "You must implement RefreshStructurePins() in your custom AffinityTable Query!");
}

bool UK2Node_AffinityTableQueryBase::ValidateConnections(FCompilerResultsLog& MessageLog) const
{
	// Must have a table
	if (!TableAsset)
	{
		MessageLog.Error(*LOCTEXT("AffinityTableQuery_Error_NoAsset", "No Affinity Table in @@").ToString(), this);
		return false;
	}

	// Make sure that all the connected nodes are valid
	bool StructsValid = true;
	int32 FoundStructs = 0;
	for (UEdGraphPin* Pin : Pins)
	{
		if (IsOutputStructPin(Pin))
		{
			UScriptStruct* DataStruct = Pin->LinkedTo.Num()
											? Cast<UScriptStruct>(Pin->LinkedTo[0]->PinType.PinSubCategoryObject.Get())
											: Cast<UScriptStruct>(Pin->PinType.PinSubCategoryObject.Get());

			if (DataStruct && !TableAsset->Structures.Contains(DataStruct))
			{
				MessageLog.Error(
					*FText::Format(
						LOCTEXT("AffinityTableQuery_Error_WrongStruct", "The table {0} does not contain structure {1} in @@, please refresh the asset pin"),
						FText::FromName(TableAsset->GetFName()),
						FText::FromName(DataStruct->GetFName()))
						 .ToString(),
					this);
				StructsValid = false;
			}
			FoundStructs++;
		}
	}

	if (FoundStructs < TableAsset->Structures.Num())
	{
		MessageLog.Warning(
			*FText::Format(
				LOCTEXT("AffinityTableQuery_Error_MissingStruct", "The table {0} has more structures than displayed in @@, please refresh the asset pin"),
				FText::FromName(TableAsset->GetFName()))
				 .ToString(),
			this);
	}

	return StructsValid;
}

bool UK2Node_AffinityTableQueryBase::IsOutputStructPin(const UEdGraphPin* Pin) const
{
	return Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct;
}

UEdGraphPin* UK2Node_AffinityTableQueryBase::GetQuerySuccessfulPin() const
{
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

	UEdGraphPin* Pin = FindPinChecked(UEdGraphSchema_K2::PN_Then);
	check(Pin->Direction == EGPD_Output);
	return Pin;
}

UEdGraphPin* UK2Node_AffinityTableQueryBase::GetQueryUnsuccessfulPin() const
{
	UEdGraphPin* Pin = FindPinChecked(UK2Node_AffinityTableQueryBase::QueryUnsuccessful);
	check(Pin->Direction == EGPD_Output);
	return Pin;
}

UEdGraphPin* UK2Node_AffinityTableQueryBase::GetInputPin(const FName& PinName) const
{
	UEdGraphPin* Pin = FindPinChecked(PinName);
	check(Pin->Direction == EGPD_Input);
	return Pin;
}

UK2Node_CallFunction* UK2Node_AffinityTableQueryBase::SpawnAffinityTableFunction(const FName& FunctionName, FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	UK2Node_CallFunction* Function = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	check(Function);

	Function->FunctionReference.SetExternalMember(FunctionName, UAffinityTableBlueprintLibrary::StaticClass());
	Function->AllocateDefaultPins();
	return Function;
}

#undef LOCTEXT_NAMESPACE
