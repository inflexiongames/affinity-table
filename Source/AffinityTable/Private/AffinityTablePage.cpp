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

#include "AffinityTablePage.h"
#include "AffinityTable.h"

FAffinityTablePage::FAffinityTablePage(const UScriptStruct* InStruct, uint32 InRows, uint32 InColumns, bool InFixedMode)
	: Struct(InStruct)
	, Columns(InColumns)
	, FixedMode(InFixedMode)
	, CurrentDatablock(0)
{
	// Allocate memory now, if we ca;
	if (const uint32 BlockCount = InRows * InColumns)
	{
		AllocateBlocks(BlockCount);
	}
	else if (InFixedMode)
	{
		// This means we are including an empty table in the game
		UE_LOG(LogAffinityTable, Error, TEXT("Datatable page for %s created in Fixed mode with zero allocations"), *(InStruct->GetName()));
	}

	// By now if we have columns we have memory blocks for them. Either way allocate rows, empty or otherwise.
	for (uint32 i = 0; i < InRows; ++i)
	{
		AddRow();
	}
}

FAffinityTablePage::~FAffinityTablePage()
{
	// Deallocate all blocks
	for (const FStructDatablock* Datablock : Datablocks)
	{
		delete Datablock;
	}
}

void FAffinityTablePage::AddRow()
{
	const TSharedPtr<Row> NewRow = MakeShareable(new Row);
	AppendHandles(NewRow.Get());
	Rows.Add(NewRow);
}

void FAffinityTablePage::AddColumn()
{
	// Add one handle at the end of every valid row
	for (TSharedPtr<Row>& ThisRow : Rows)
	{
		if (ThisRow.IsValid())
		{
			AppendHandles(ThisRow.Get(), 1);
		}
	}
	Columns++;
}

void FAffinityTablePage::DeleteRow(uint32 RowIndex)
{
	Row* RowToDelete = GetRow(RowIndex);
	check(RowToDelete);

	// The row itself will no longer be utilized for the duration of this editor's run, but the
	// handles on each column will be recycled, and the memory space re-assigned as needed.
	uint32 DatablockIndex;
	FStructDatablock::DatablockHandle DatablockHandle;
	for (const DataHandle Column : *RowToDelete)
	{
		if (GetHandleData(Column, DatablockIndex, DatablockHandle))
		{
			Datablocks[DatablockIndex]->RecycleHandle(DatablockHandle);
		}
	}
	Rows[RowIndex].Reset();
}

void FAffinityTablePage::DeleteColumn(uint32 ColumnIndex)
{
	check(ColumnIndex < Columns && !DeletedColumns.Contains(ColumnIndex));

	// Recycle one handle out of each valid row. The rows themselves remain but this column index should not be accessed again
	uint32 DatablockIndex;
	FStructDatablock::DatablockHandle DatablockHandle;
	for (TSharedPtr<Row>& ThisRow : Rows)
	{
		if (ThisRow.IsValid())
		{
			check(ColumnIndex < static_cast<uint32>(ThisRow->Num()));
			DataHandle& ThisHandle = (*ThisRow)[ColumnIndex];
			if (GetHandleData(ThisHandle, DatablockIndex, DatablockHandle))
			{
				Datablocks[DatablockIndex]->RecycleHandle(DatablockHandle);
				ThisHandle = InvalidDataHandle;
			}
		}
	}
	DeletedColumns.Add(ColumnIndex);
}

FStructDatablock::DatablockPtr FAffinityTablePage::GetDatablockPtr(DataHandle Handle) const
{
	uint32 DatablockIndex;
	FStructDatablock::DatablockHandle DatablockHandle;
	FStructDatablock::DatablockPtr DataPtr = nullptr;
	if (GetHandleData(Handle, DatablockIndex, DatablockHandle))
	{
		DataPtr = Datablocks[DatablockIndex]->GetMemoryBlock(DatablockHandle);
	}
	return DataPtr;
}

FStructDatablock::DatablockPtr FAffinityTablePage::GetDatablockPtr(uint32 InRow, uint32 InColumn) const
{
	FStructDatablock::DatablockPtr Ptr = nullptr;
	if (Row* SelectedRow = GetRow(InRow))
	{
		check(InColumn < static_cast<uint32>(SelectedRow->Num()));
		Ptr = GetDatablockPtr((*SelectedRow)[InColumn]);
	}
	return Ptr;
}

void FAffinityTablePage::GetDatablockPtrsForRow(uint32 InRow, TArray<FStructDatablock::DatablockPtr>& OutDataBlocks) const
{
	if (Row* SelectedRow = GetRow(InRow))
	{
		for (int i = 0; i < SelectedRow->Num(); i++)
		{
			if (FStructDatablock::DatablockPtr Ptr = GetDatablockPtr((*SelectedRow)[i]))
			{
				OutDataBlocks.Add(Ptr);
			}
		}
	}
}

int32 FAffinityTablePage::GetStructSize() const
{
	// The size of our structure is constant
	if (Datablocks.Num())
	{
		return Datablocks[0]->GetStructSize();
	}
	return 0;
}

void FAffinityTablePage::AllocateBlocks(uint32 Capacity)
{
	check(Capacity);
	check(Struct.IsValid());

	uint32 FullBlocks = Capacity / FStructDatablock::MaxDatablockCapacity;
	const uint32 SmallBlock = Capacity % FStructDatablock::MaxDatablockCapacity;

	// This function always adds at least one datablock.
	// Move the current datablock to the first new added set.
	CurrentDatablock = Datablocks.Num();

	while (FullBlocks)
	{
		FStructDatablock* Datablock = new FStructDatablock(Struct.Get(), FStructDatablock::MaxDatablockCapacity, true);
		Datablocks.Add(Datablock);
		--FullBlocks;
	}

	if (SmallBlock)
	{
		FStructDatablock* Datablock = new FStructDatablock(Struct.Get(), SmallBlock, true);
		Datablocks.Add(Datablock);
	}
}

void FAffinityTablePage::AppendHandles(Row* InRow, uint32 Count)
{
	check(InRow);

	// Minor speed-up if we have no deleted columns (will happen during the game)
	if (!Count && !DeletedColumns.Num())
	{
		Count = Columns;
	}

	// A specific number of handles
	if (Count)
	{
		while (Count)
		{
			InRow->Add(NewHandle());
			--Count;
		}
	}

	// One for each column, making sure to insert invalid handles for delete-marked columns
	else
	{
		for (uint32 i = 0; i < Columns; ++i)
		{
			InRow->Add(DeletedColumns.Contains(i) ? InvalidDataHandle : NewHandle());
		}
	}
}

FAffinityTablePage::DataHandle FAffinityTablePage::MakeHandle(uint32 DatablockIndex, FStructDatablock::DatablockHandle DatablockHandle) const
{
	check(DatablockIndex < static_cast<uint32>(Datablocks.Num()));
	check(DatablockHandle != FStructDatablock::InvalidHandle);

	// Index | Handle
	const DataHandle Handle = (static_cast<uint64>(DatablockIndex) << 32) | (DatablockHandle);
	return Handle;
}

bool FAffinityTablePage::GetHandleData(DataHandle Handle, uint32& OutDatablockIndex, FStructDatablock::DatablockHandle& OutDatablockHandle) const
{
	if (Handle != InvalidDataHandle)
	{
		OutDatablockIndex = (Handle & 0xffffffff00000000) >> 32;
		OutDatablockHandle = Handle & 0x00000000ffffffff;

		check(OutDatablockIndex < static_cast<uint32>(Datablocks.Num()));
		check(OutDatablockHandle != FStructDatablock::InvalidHandle);
		return true;
	}
	return false;
}

FAffinityTablePage::DataHandle FAffinityTablePage::FindAvailableHandle()
{
	DataHandle Handle = InvalidDataHandle;
	if (Datablocks.Num())
	{
		FStructDatablock* Datablock = Datablocks[CurrentDatablock];
		FStructDatablock::DatablockHandle DatablockHandle = Datablock->NewHandle();
		if (DatablockHandle == FStructDatablock::InvalidHandle)
		{
			for (int32 i = 0; i < Datablocks.Num(); ++i)
			{
				Datablock = Datablocks[i];
				DatablockHandle = Datablock->NewHandle();
				if (DatablockHandle != FStructDatablock::InvalidHandle)
				{
					CurrentDatablock = static_cast<uint32>(i);
					Handle = MakeHandle(CurrentDatablock, DatablockHandle);
					break;
				}
			}
		}
		else
		{
			Handle = MakeHandle(CurrentDatablock, DatablockHandle);
		}
	}
	return Handle;
}

FAffinityTablePage::DataHandle FAffinityTablePage::NewHandle()
{
	DataHandle Handle = FindAvailableHandle();

	// If we have no more space, allocate a full block. This operation is only valid in dynamic mode.
	if (Handle == InvalidDataHandle && !FixedMode)
	{
		AllocateBlocks();
		Handle = FindAvailableHandle();
		check(Handle != InvalidDataHandle);
	}

	check(Handle != FStructDatablock::InvalidHandle);
	return Handle;
}
