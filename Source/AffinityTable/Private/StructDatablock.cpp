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

#include "StructDatablock.h"
#include "AffinityTable.h"

FStructDatablock::FStructDatablock(const UScriptStruct* InStruct, const uint32 DesiredCapacity, bool AllocNow /* = false */)
	: Struct(InStruct)
	, Datablock(nullptr)
	, Capacity(1)
	, StructSize(0)
	, NextHandle(InvalidHandle)
{
	check(DesiredCapacity);
	StructName = Struct->GetFName();

	// Warn if we need to cap this capacity
	Capacity = DesiredCapacity < MaxDatablockCapacity ? DesiredCapacity : MaxDatablockCapacity;
	check(Capacity == DesiredCapacity);

	if (AllocNow)
	{
		Alloc();
	}
}

FStructDatablock::~FStructDatablock()
{
	Dealloc();
};

FStructDatablock::DatablockHandle FStructDatablock::NewHandle()
{
	// Must we alloc?
	if (Datablock == nullptr)
	{
		Alloc();
	}

	// New, unopened handle.
	if (NextHandle < Capacity)
	{
		return NextHandle++;
	}

	// Refurbished handle.
	if (FreeHandles.Num() > 0)
	{
		DatablockHandle RecycledHandle;
		FreeHandles.HeapPop(RecycledHandle);

		Struct->ClearScriptStruct(GetMemoryBlock(RecycledHandle));
		return RecycledHandle;
	}

	// No luck.
	return InvalidHandle;
}

void FStructDatablock::RecycleHandle(FStructDatablock::DatablockHandle Handle)
{
	check(Handle != InvalidHandle);
	FreeHandles.Add(Handle);
}

void FStructDatablock::GarbageCollect()
{
	if (Datablock != nullptr && (NextHandle == 0 || FreeHandles.Num() == Capacity))
	{
		Dealloc();
	}
}

void FStructDatablock::Alloc()
{
	check(Datablock == nullptr);
	check(FreeHandles.Num() == 0);

	StructSize = static_cast<SIZE_T>(Struct->GetStructureSize());
	check(StructSize);

	Datablock = (DatablockPtrType) FMemory::Malloc(StructSize * static_cast<SIZE_T>(Capacity));
	check(Datablock != nullptr);

	Struct->InitializeStruct(Datablock, static_cast<int32>(Capacity));
	NextHandle = 0;
}

void FStructDatablock::Dealloc()
{
	if (Datablock != nullptr)
	{
		// Our struct should NEVER be null here (since we used it to allocate the datablock)
		// but we've seen our share of strange things in this world...
		check(!Struct.IsExplicitlyNull());

		// Under rare circumstances, some cook or build processes may invalidate structs before destroying
		// our owning AffinityTable. Calling UScriptStruct::DestroyStruct will crash the runtime.
		// Catch and log those events. We run the risk of leaking any dependent properties but the
		// process will survive. We still safely de-allocate the memory created by this block.
		if (Struct.IsValid() && Struct->IsValidLowLevel() && !Struct->GetFName().IsNone())
		{
			Struct->DestroyStruct(Datablock, Capacity);
		}
		else
		{
			if (Struct.IsStale())
			{
				UE_LOG(LogAffinityTable, Display, TEXT("UScriptStruct for [%s] was deleted before all StructDataBlocks in an AffinityTable were freed"), *StructName.ToString());
			}
			else if (Struct.IsStale(true))
			{
				UE_LOG(LogAffinityTable, Display, TEXT("UScriptStruct for [%s] was marked pending kill before all StructDataBlocks in an AffinityTable were freed"), *StructName.ToString());
			}
			else
			{
				// If we get here, something has gone terribly wrong with the weak pointer since it thinks it's pointing at a valid object
				UE_LOG(LogAffinityTable, Error, TEXT("A UScriptStruct weak object pointer for [%s] is probably pointing at garbage memory."), *StructName.ToString());
			}
		}

		FMemory::Free(Datablock);

		Datablock = nullptr;
		NextHandle = InvalidHandle;
		FreeHandles.Empty();
	}
}
