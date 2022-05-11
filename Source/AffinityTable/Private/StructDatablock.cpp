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


#include "StructDatablock.h"
#include "AffinityTable.h"
#include "UObject/Class.h"

FStructDatablock::FStructDatablock(const UScriptStruct* InStruct, const uint32 DesiredCapacity, bool AllocNow /* = false */) :
	Struct(InStruct),
	Datablock(nullptr),
	Capacity(1),
	StructSize(0),
	NextHandle(InvalidHandle)
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
		check(Struct);

		// Under rare circumstances, some cook or build processes may invalidate structs before destroying
		// our owning AffinityTable. Calling UScriptStruct::DestroyStruct will crash the runtime.
		// Catch and log those events. We run the risk of leaking any dependent properties but the
		// process will survive. We still safely de-allocate the memory created by this block.
		if (!Struct->GetFName().IsNone() && Struct->IsValidLowLevel())
		{
			Struct->DestroyStruct(Datablock, Capacity);
		}
		else
		{
			UE_LOG(LogAffinityTable, Display, TEXT("Structure %s was deleted before its AffinityTable could free it"), *StructName.ToString());
		}

		FMemory::Free(Datablock);

		Datablock = nullptr;
		NextHandle = InvalidHandle;
		FreeHandles.Empty();
	}
}
