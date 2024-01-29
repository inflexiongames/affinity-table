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
#include "StructDatablock.h"
#include "UObject/Class.h"

/**
 * FAffinityTablePage manages all the cell/structure data allocations for a single UScriptStruct type.
 *
 * Fixed Vs Dynamic allocations
 *
 * Pages use two memory management strategies: if you know the exact size of your table,
 * provide it on construction and the required memory will be allocated in one go. This is
 * preferred for runtime when the size won't change. If you are going to be changing the
 * size (editor, or dynamic tables) leave it at zero, and the page will allocate memory in chunks as
 * needed. There may be some waste for unused blocks, but the page will try to be smart and
 * recycle/garbage collect if it can.
 *
 * You can mix these modes by providing an initial size and activating dynamic mode: the memory will
 *  be allocated, and subsequent blocks of FStructDatablock::MaxDatablockCapacity will be added as required.
 *
 */
class FAffinityTablePage
{
public:
	/** A key that identifies a datablock in our array (left 32 bits) and the handle to a block inside of it (right 32 bits) */
	using DataHandle = uint64;

	/** Invalid handle */
	static constexpr uint64 InvalidDataHandle = MAX_uint64;

	/** A row in our page is an ordered array of in-memory structures */
	using Row = TArray<DataHandle>;

	/**
	 * Creates a new instance.
	 * @param InStruct The UScriptStruct used to format our page's memory
	 * @param InRows Number of rows to allocate. Cannot be zero if FixedMode = true
	 * @param InColumns Number of columns to allocate per row.
	 * @param InFixedMode If true and InBlocks * InColumns is nonzero, allocation happens immediately and it remains static
	 *	for the lifetime of the instance.
	 */
	FAffinityTablePage(const UScriptStruct* InStruct, uint32 InRows = 0, uint32 InColumns = 0, bool InFixedMode = false);

	/** Clean-up */
	~FAffinityTablePage();

	/**
	 * Allocates memory for one row of FAffinityTablePage::Columns elements
	 */
	void AddRow();

	/**
	 * Inserts a column in this page and permanently increases the number of columns available to rows.
	 */
	void AddColumn();

	/**
	 * Removes a row based on the provided index. All handles in the row will be recycled.
	 * @param RowIndex Index of the row to remove.
	 */
	void DeleteRow(uint32 RowIndex);

	/**
	 * Removes a column based on the provided index. All handles for each affected row will be recycled.
	 * @param ColumnIndex Index of the column to remove
	 */
	void DeleteColumn(uint32 ColumnIndex);

	/**
	 * Const access to this page's structure
	 */
	FORCEINLINE const UScriptStruct* GetStruct() const
	{
		return Struct.Get();
	}

	/**
	 * Retrieve our row and column count, including cells that may be temporarily
	 * recycled if we are in editor mode.
	 */
	FORCEINLINE void GetRowAndColumnCount(uint32& OutRows, uint32& OutColumns) const
	{
		OutRows = static_cast<uint32>(Rows.Num());
		OutColumns = Columns;
	}

	/**
	 * Retrieve the data associated with the provided cell position.
	 * @param InRow Row index
	 * @param InColumn index
	 */
	FStructDatablock::DatablockPtr GetDatablockPtr(uint32 InRow, uint32 InColumn) const;

	/**
	 * Retrieve the data associated with the provided handle
	 * @param Handle A handle to the requested data
	 */
	FStructDatablock::DatablockPtr GetDatablockPtr(DataHandle Handle) const;

	/**
	 * Retrieve the data associated with the provided row
	 * @param InRow Row index
	 * @param OutDataBlocks List of datablock pointers for the whole row
	 */
	void GetDatablockPtrsForRow(uint32 InRow, TArray<FStructDatablock::DatablockPtr>& OutDataBlocks) const;

	/**
	 * Provide the size footprint of our assigned structure
	 */
	int32 GetStructSize() const;

private:
	/**
	 * Allocates enough datablocks to satisfy the provided capacity. Memory is immediately committed.
	 * If FixedMode, we allocate EXACTLY the required size.
	 * @param Capacity block capacity in dynamic mode, exact capacity in fixed mode
	 */
	void AllocateBlocks(uint32 Capacity = FStructDatablock::MaxDatablockCapacity);

	/**
	 * Adds a number of new handles to the end of the provided array. If the count is zero, we add one handle per
	 * column, and one invalid handle for each column with an invalid registered index
	 * @param InRow The row we append handles to
	 * @param Count Number of handles to add. If set to zero, we add as many as we have columns, with column validation.
	 */
	void AppendHandles(Row* InRow, uint32 Count = 0);

	/**
	 * Retrieves a row for sequential access to its columns, or nullptr if the row is invalid or the index is out of range.
	 * Use this call if you are going to batch-process a single row.
	 * @param RowIndex Index of the requested row
	 */
	FORCEINLINE Row* GetRow(uint32 RowIndex) const
	{
		check(RowIndex < static_cast<uint32>(Rows.Num()));
		return Rows[RowIndex].IsValid() ? Rows[RowIndex].Get() : nullptr;
	}

	/**
	 * Creates a datahandle with the provided datblock and datablock index
	 * @param DatablockIndex Index to the datablock in our array
	 * @param DatablockHandle Handle to a memory location in a datablock
	 */
	DataHandle MakeHandle(uint32 DatablockIndex, FStructDatablock::DatablockHandle DatablockHandle) const;

	/**
	 * Retrieves the datablock index and handle stored on this page handle, verifying that both are
	 * valid. Returns true if successful.
	 * @param Handle Handle to separate.
	 * @param OutDatablockIndex Index to the datablock containing the memory allocation for this handle
	 * @param OutDatablockHandle Handle pointing to a memory location inside of a datablock
	 */
	bool GetHandleData(DataHandle Handle, uint32& OutDatablockIndex, FStructDatablock::DatablockHandle& OutDatablockHandle) const;

	/**
	 * Produces a datablock handle ready for assignation.
	 * @return A valid handle, or FStructDatablock::InvalidHandle if we ran out of memory
	 */
	DataHandle NewHandle();

	/**
	 * Finds an available handle within our memory blocks. Picks new active blocks as necessary.
	 */
	DataHandle FindAvailableHandle();

	/** Struct for this page */
	TWeakObjectPtr<const UScriptStruct> Struct;

	/** Rows of this page, inserted in order based on the Row tags in our grid */
	TArray<TSharedPtr<Row>> Rows;

	/** Data blocks managed by this page */
	TArray<FStructDatablock*> Datablocks;

	/** Set of columns that are no longer usable */
	TSet<uint32> DeletedColumns;

	/** Number of columns per row */
	uint32 Columns;

	/** True if we are running in fixed memory mode */
	bool FixedMode;

	/** Reference to our working datablock */
	uint32 CurrentDatablock;
};
