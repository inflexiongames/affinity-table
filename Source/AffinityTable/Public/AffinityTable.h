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

#if WITH_EDITOR
#include <functional>
#endif

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Kismet/KismetArrayLibrary.h"
#include "Logging/LogMacros.h"
#include "UObject/Class.h"
#include "UObject/NoExportTypes.h"
#include "AffinityTable.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogAffinityTable, Log, All);

class UAssetImportData;
class FAffinityTablePage;

USTRUCT(BlueprintType)
struct FCellDataArrayWrapper
{
	GENERATED_USTRUCT_BODY()

public:
	FCellDataArrayWrapper()
	{
	}

	TArray<FAffinityTableCellDataWrapper> CellDataArray;
};

/**
 * Wrapper around a naked pointer so we can move data across blueprint calls
 */
USTRUCT(BlueprintType)
struct FAffinityTableCellDataWrapper
{
	GENERATED_USTRUCT_BODY()

public:
	FAffinityTableCellDataWrapper(uint8* InDataPtr = nullptr) :
		RawDataPtr(InDataPtr)
	{
	}

	uint8* RawDataPtr;
};

/**
 * Indexing
 *
 *	Grid queries costs at least two map lookups, and at most n x m lookups, where
 *	n is the number of segments of the row tag, and m is the number of segments in the
 *	column tag. Data misses cause hierarchical lookups. 
 *
 *	Lookups produce a pair of TagIndexes that reference the requested row | column for any
 *	such array in any page. Therefore the cost of finding a match is invariant to the number of queried structures.
 *
 *	TagIndexes are always sequential for rows and columns on asset load. When deleting and appending during
 *	edit sessions, unused array locations on our TablePages are emptied (rows) or assigned invalid handles (columns), and their
 *	previous handles recycled for new locations. Serialization re-normalizes the data order.
 *
 *	For example, assume the map Row(tag) = { a: 0, a.a: 1, b: 2 },
 *
 *		- appending b.a produces { ..., b.a: 3 }
 *		- deleting a.a removes it from Row(tag), recycling its cell handle. 
 *		- re-adding a.a produces Row(tag) = { ..., a.a: 4 }
 *		- Where Page(structure n).Rows[0] = [Handle 1, ...Handle n], deleting the n-1 column will yield [Handle1, ..., InvalidHandle, Handle n]
 *
 * StructureGrid assets are assumed to be relatively small ( ~< 100 rows | cols)
 */

/**
 * An asset that defines data relationships between pairs of tags.
 *
 * Tag intersections contain a collection of data in the form of one or more UScriptStruct's. Queries 
 * are taxonomic, taking advantage of the nature of gameplay tags.
 *
 * Asset editing is done by the AffinityTableEditor.
 *
 */
UCLASS(BlueprintType)
class AFFINITYTABLE_API UAffinityTable : public UObject
{
	GENERATED_BODY()

public:
	/** Indexes a row or column after a given tag */
	using TagIndex = uint32;

	/** Invalid tag index designation */
	static const uint32 InvalidIndex = MAX_uint32;

	/** Quickly identifies a cell by its row and column index */
	struct Cell
	{
		TagIndex Row;
		TagIndex Column;
	};

	/** Identifies a cell by its tags. Needs querying to yield an actual cell */
	struct CellTags
	{
		FGameplayTag Row;
		FGameplayTag Column;
	};

public:
	/** Provides context about the data contained in this asset */
	UPROPERTY(EditAnywhere, Category = Table)
	FString Description;

	/** Defines the data contents (pages) of each cell */
	UPROPERTY(EditAnywhere, Category = Cells)
	TArray<UScriptStruct*> Structures;

	/** To retain row FGameplayTags in order.*/
	UPROPERTY()
	TArray<FGameplayTag> RowTags;
	/** To retain column FGameplayTags in order.*/
	UPROPERTY()
	TArray<FGameplayTag> ColumnTags;

public:
	/**
	 * Creates a new UAffinityTable instance
	 * @param ObjectInitializer Valid default object initialization data
	 */
	UAffinityTable(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Cleans up this instance */
	virtual ~UAffinityTable();

	// UObject Interface
	virtual void GetPreloadDependencies(TArray<UObject*>& OutDeps) override;
	// End of UObject Interface

	/**
	 * Provides the index of a row based on its tag
	 * @param InTag A valid tag
	 * @param ExactMatch if true, don't find closest match
	 */
	TagIndex GetRowIndex(const FGameplayTag& InTag, bool ExactMatch = true) const;

	/**
	 * Provides the index of a column based on its tag
	 * @param InTag A valid tag
	 * @param ExactMatch If true, don't find closest match
	 */
	TagIndex GetColumnIndex(const FGameplayTag& InTag, bool ExactMatch = true) const;

	/**
	 * Retrieve in-memory data for a given cell/structure, or nullptr if the parameters are invalid
	 * @param InCell cell address for the structure data
	 * @param Type of structure we're interested in
	 */
	uint8* GetCellData(const Cell& InCell, const UScriptStruct* InScriptStruct);

	/**
	 * Retrieve in-memory data for a given row/structure, or nullptr if the parameters are invalid
	 * @param RowIndex index of the row for the structure data
	 * @param Type of structure we're interested in
	 */
	void GetRowData(TagIndex RowIndex, const UScriptStruct* InScriptStruct, TArray<uint8*>& OutData);

	/**
	 * Queries an affinity table for information contained at the intersection of the provided row and column.
	 * @param InCellTags Coordinates of the requested cell
	 * @param ExactMatch If true, look for an exact Row Vs Column match. Otherwise find the closest tag
	 * @param InStructureTypes The types of structure to return. These must be known to the table asset.
	 * @param OutMemoryPtrs Pointers to hold data locations for the requested structures, InStructureTypes order. 
 	 * @return True if a match was found.
	 */
	bool Query(const CellTags& InCellTags, bool ExactMatch, TArray<const UScriptStruct*>& InStructureTypes, TArray<FAffinityTableCellDataWrapper>& OutMemoryPtrs);

	/**
	 * Queries an affinity table for information contained at in the provided row.
	 * @param RowTag Requested Row to get the data for
	 * @param ExactMatch If true, look for an exact Row Vs Column match. Otherwise find the closest tag
	 * @param InStructureTypes The types of structure to return. These must be known to the table asset.
	 * @param OutMemoryPtrs Pointers to hold data locations for the requested structures, InStructureTypes order. 
 	 * @return True if a match was found.
	 */
	bool QueryForRow(const FGameplayTag& RowTag, bool ExactMatch, TArray<const UScriptStruct*>& InStructureTypes, TArray<FCellDataArrayWrapper>& OutMemoryPtrs);

#if WITH_EDITOR

	/**
	 * Callback for events that happen to our structure array
	 * @param Event EPropertyChangeType descriptor
	 */
	using StructureChangeCallback = std::function<void(uint32 ChangeType)>;

	/**
	 * Assigns a callback for structure change event notification
	 * @param InCallback A correctly-formed callback
	 */
	void SetStructureChangeCallback(StructureChangeCallback InCallback);

	/**
	 * Reacts to changes on this object's properties
	 * @param PropertyChangedEvent Data about the property that changed
	 */
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;

	/** Const access to our column map */
	FORCEINLINE const TMap<FGameplayTag, TagIndex>& GetColumns() const
	{
		return Columns;
	}

	/** Const access to our rows map */
	FORCEINLINE const TMap<FGameplayTag, TagIndex>& GetRows() const
	{
		return Rows;
	}

	/**
	 * Adds a new row. Returns false if the row already exists
	 * @param InTag Tag to add
	 */
	bool AddRow(FGameplayTag& InTag);

	/**
	 * Adds a new column. Returns false if the column already exists
	 * @param InTag Tag to add
	 */
	bool AddColumn(FGameplayTag& InTag);

	/**
	 * Removes the row that contains this tag.
	 * @param InTag Tag root to remove
	 */
	void DeleteRow(const FGameplayTag& InTag);

	/**
	 * Removes the column that contains this tag.
	 * @param InTag Tag root to remove
	 */
	void DeleteColumn(const FGameplayTag& InTag);

	/**
	 * Sets the color associated with this row tag
	 * @param InTag The tag we are modifying
	 * @param Color The color we are setting this tag to
	 * @param IsRowTag True if this is row, false for column
	 */
	void SetTagColor(const FGameplayTag& InTag, const FLinearColor& Color, bool IsRowTag);

	/**
	 * Tries to find a previously assigned color for this tag. If found, the color is
	 * assigned on the provided parameter and the function returns true. Otherwise the
	 * function returns false and no color is assigned.
	 *
	 * @param InTag Tag to search
	 * @param OutColor Receives the existing color if we have a tag for it
	 * @param IsRowTag true if the tag queried is a row tag, false for column
	 */
	bool TryGetTagColor(const FGameplayTag& InTag, FLinearColor& OutColor, bool IsRowTag) const;

	/**
	 * Sets a directed, unidirectional link from the child to the parent cell.
	 * This link is useful for data propagation in the editor, but currently has no effect during gameplay.
	 * @param InStruct Structure that determines the link domain
	 * @param Child Cell that receives data
	 * @param Parent Cell that propagates data
	 */
	void SetInheritanceLink(const UScriptStruct* InStruct, const CellTags& Child, const CellTags& Parent);

	/**
	 * Retrieves the parent of the provided child, if we have a link. Otherwise returns false.
	 * @param InStruct Structure that determines the link domain
	 * @param Child The child cell we want to query
	 * @param OutParent Holds the parent of the child cell, if any
	 * @return True if there is a registered relationship for this child
	 */
	bool TryGetInheritanceLink(const UScriptStruct* InStruct, const CellTags& Child, CellTags& OutParent);

	/**
	 * Removes any existing inheritance link recorded for this cell.
	 * This points the cell to an invalid tag, meaning we 'know' it is not linked as opposed to not knowing
	 * about the tag at all. The editor proceeds in different ways accordingly. 
	 */
	void RemoveInheritanceLink(const UScriptStruct* InStruct, const CellTags& InCell);

#endif

private:
	/** Defines a map of inheritance connections */
	using InheritanceMap = TMap<FString, CellTags>;

	/**
	 * Loads data for this asset from the provided file. All other contents are deleted.
	 * @param Ar Archive with a previously saved table
	 */
	void LoadTable(FArchive& Ar);

	// Compatibility migrations.
	//
	// These functions implement a FULL loading procedure (minus the version check), plus any required
	// compatibility operations in order to leave the table in a functional state.

	/** Loads tables with format version 2 */
	//void LoadTable_V2(FArchive& Ar); //left as comment for example of a version migration.

	/**
	 * Clears all data on this table, freeing up all memory utilized by any existing structures.
	 * Does not touch exposed properties. Failing to re-allocate structure memory after this call
	 * will result in memory exceptions. 
	 */
	void ClearTable();

#if WITH_EDITOR
	/**
	 * Runs logic that needs to happen in Serialize() before the call to Super::Serialize()
	 */
	void PreSaveTable();
	/**
	 * Saves the contents of this asset
	 * @param Ar Archive to save our table into
	 */
	void SaveTable(FArchive& Ar);
#endif

	/**
	 * Serialization and de-serialization hook
	 */
	virtual void Serialize(FArchive& Ar) override;

	/**
	 * Serializes the information from the provided page in or out of the provided archive
	 * @param Ar The archive we are reading or writing to
	 * @param Page The page that holds or receives the data
	 * @param Struct The structure that corresponds to this page
	 */
	void SerializePage(FArchive& Ar, FAffinityTablePage* Page, UScriptStruct* Struct);

	/**
	 * Allocates memmory pages for our owned structures.
	 * @param InRows Number of rows to allocate
	 * @param InColumns Number of columns to allocate per row
	 */
	void AllocatePageMemory(uint32 InRows, uint32 InColumns);

	/**
	 * Make a single hashable string for the provided cell
	 * @param InCell The cell to hash
	 */
	FString StringIDForCell(const CellTags& InCell);

	/**
	 * Finds a tag index in the provided map
	 * @param InMap map to search
	 * @param InTag Valid tag
	 * @param ExactMatch If true, only an exact match is valid
	 */
	TagIndex GetIndex(const TMap<FGameplayTag, TagIndex>& InMap, const FGameplayTag& InTag, bool ExactMatch) const;

	/**
	 * Verify that the provided structure is loaded. Attempt to load if necessary.
	 * @param ScriptStruct Structure to verify
	 */
	void EnsureStructIsLoaded(UScriptStruct* ScriptStruct);

	/**
	 * Finds the memory page for the provided structure. Returns nullptr if we have no page
	 * @param InScriptStruct non-null pointer to a valid structure
	 */
	FAffinityTablePage* GetPageForStruct(const UScriptStruct* InScriptStruct);

	/** Tags available in our table's rows */
	TMap<FGameplayTag, TagIndex> Rows;

	/** Tags available in our table's columns */
	TMap<FGameplayTag, TagIndex> Columns;

	/** Colors for rows */
	TMap<FGameplayTag, FLinearColor> RowColors;

	/** Colors for columns */
	TMap<FGameplayTag, FLinearColor> ColumnColors;

	/** Memory pages for our structures. length(Pages) === length(Structures)  */
	TArray<TSharedRef<FAffinityTablePage>> Pages;

	/** Inheritance set. Used mostly for the editor */
	TMap<FName, InheritanceMap> InheritanceMaps;

	/** Index generator for rows */
	TagIndex NextRowIndex;

	/** Index generator for columns */
	TagIndex NextColumnIndex;

	/** True if we do not allow dynamic allocations */
	bool FixedModeActive;

	/** Data serialization versioning */
	static const uint32 FileFormatVersion;

#if WITH_EDITOR
	/** Callback for events happening to our structure array */
	StructureChangeCallback ChangeCallback;
#endif
};

/**
 * Utility functions that expose Affinity Tables to blueprints. 
 */
UCLASS()
class AFFINITYTABLE_API UAffinityTableBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Perform a query over the provided affinity table */
	UFUNCTION(BlueprintCallable, CustomThunk, Category = "AffinityTable", meta = (BlueprintInternalUseOnly = "true"))
	static bool QueryTable(UAffinityTable* Table, const FGameplayTag& RowTag, const FGameplayTag& ColumnTag, bool ExactMatch, TArray<const UScriptStruct*> StructureTypes, TArray<FAffinityTableCellDataWrapper>& OutMemoryPtrs);

	UFUNCTION(BlueprintCallable, CustomThunk, Category = "AffinityTable", meta = (BlueprintInternalUseOnly = "true"))
	static bool QueryTableForRow(UAffinityTable* Table, const FGameplayTag& RowTag, bool ExactMatch, TArray<const UScriptStruct*> StructureTypes, TArray<FCellDataArrayWrapper>& OutMemoryPtrs);

	UFUNCTION(BlueprintCallable, CustomThunk, Category = "AffinityTable", meta = (CustomStructureParam = "OutData", BlueprintInternalUseOnly = "true"))
	static void GetTableCellData(const UScriptStruct* StructType, int32 DataIndex, TArray<FAffinityTableCellDataWrapper> MemoryPtrs, FAffinityTableCellDataWrapper& OutData);

	UFUNCTION(BlueprintCallable, CustomThunk, Category = "AffinityTable", meta = (CustomStructureParam = "OutData", BlueprintInternalUseOnly = "true"))
	static void GetTableCellsData(const UScriptStruct* StructType, int32 DataIndex, TArray<FCellDataArrayWrapper> MemoryPtrs, TArray<FAffinityTableCellDataWrapper>& OutData);

	// Implements QueryTable
	DECLARE_FUNCTION(execQueryTable)
	{
		P_GET_OBJECT(UAffinityTable, Table);
		P_GET_STRUCT_REF(FGameplayTag, RowTag);
		P_GET_STRUCT_REF(FGameplayTag, ColumnTag);
		P_GET_UBOOL(ExactMatch);
		P_GET_TARRAY(const UScriptStruct*, StructureTypes);
		P_GET_TARRAY_REF(FAffinityTableCellDataWrapper, OutMemoryPtrs);

		P_FINISH;

		check(Table);
		UAffinityTable::CellTags Cell{ RowTag, ColumnTag };
		*(bool*) RESULT_PARAM = Table->Query(Cell, ExactMatch, StructureTypes, OutMemoryPtrs);
	}

	// Implements QueryTableForRow
	DECLARE_FUNCTION(execQueryTableForRow)
	{
		P_GET_OBJECT(UAffinityTable, Table);
		P_GET_STRUCT_REF(FGameplayTag, RowTag);
		P_GET_UBOOL(ExactMatch);
		P_GET_TARRAY(const UScriptStruct*, StructureTypes);
		P_GET_TARRAY_REF(FCellDataArrayWrapper, OutMemoryPtrs);

		P_FINISH;

		check(Table);
		*(bool*) RESULT_PARAM = Table->QueryForRow(RowTag, ExactMatch, StructureTypes, OutMemoryPtrs);
	}

	// Implements GetTableCellDataFromArray
	DECLARE_FUNCTION(execGetTableCellData)
	{
		P_GET_OBJECT(UScriptStruct, StructType);
		P_GET_PROPERTY(FIntProperty, DataIndex);
		P_GET_TARRAY(FAffinityTableCellDataWrapper, MemoryPtrs);

		// Re-purpose the out parameter. The caller must have changed its type and ptr to the structure we are writing
		Stack.StepCompiledIn<FStructProperty>(nullptr);
		uint8* OutDataPtr = Stack.MostRecentPropertyAddress;

		P_FINISH;

		check(StructType);
		check(DataIndex < MemoryPtrs.Num());
		check(OutDataPtr);

		StructType->CopyScriptStruct(OutDataPtr, MemoryPtrs[DataIndex].RawDataPtr);
	}

	// Implements GetTableCellsData
	DECLARE_FUNCTION(execGetTableCellsData)
	{
		P_GET_OBJECT(UScriptStruct, StructType);
		P_GET_PROPERTY(FIntProperty, DataIndex);
		P_GET_TARRAY(FCellDataArrayWrapper, MemoryPtrs);

		// Re-purpose the out parameter. The caller must have changed its type and ptr to the structure we are writing
		Stack.StepCompiledIn<FArrayProperty>(nullptr);
		uint8* OutDataPtr = Stack.MostRecentPropertyAddress;
		FArrayProperty* OutArrayProperty = CastFieldChecked<FArrayProperty>(Stack.MostRecentProperty);

		if (!OutArrayProperty)
		{
			UE_LOG(LogAffinityTable, Error, TEXT("Malformed array input to GetTableCellsData!"));
			Stack.bArrayContextFailed = true;
			return;
		}

		P_FINISH;

		check(StructType);
		check(DataIndex < MemoryPtrs.Num());
		check(OutDataPtr);

		//FScriptArrayHelper lets us access wild card FArrayProperty and their raw data!
		FScriptArrayHelper OutArrayHelper(OutArrayProperty, OutDataPtr);
		for (int i = 0; i < MemoryPtrs[DataIndex].CellDataArray.Num(); i++)
		{
			int outIndex = OutArrayHelper.AddValue();
			uint8* outRawData = OutArrayHelper.GetRawPtr(outIndex);
			StructType->CopyScriptStruct(outRawData, MemoryPtrs[DataIndex].CellDataArray[i].RawDataPtr);
		}
	}
};
