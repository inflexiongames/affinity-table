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

#include "CoreMinimal.h"
#include "Math/NumericLimits.h"

/**
 * Owns and manages a block of memory that holds multiple structures of a specific UScriptStruct type.
 *
 * Each allocation is stored as a pointer to a buffer of structure blocks. Our owner assigns those blocks
 * as required.
 *
 * 1 block = 1 structure. Therefore the size of this datablock = block capacity * size(structure type)
 *
 */
class FStructDatablock
{
public:
	/** Maximum number of block allocations per instance */
	static const uint32 MaxDatablockCapacity = 512;

	/** Invalid handle designation */
	static const uint32 InvalidHandle = MAX_uint32;

	/** Pointer datatype for memory allocations */
	using DatablockPtrType = uint8*;

	/**
	 * Defines a handle to a memory location in our block.
	 * If you ever change the size of this handle, make sure to update AffinityTablePage::DataHandle
	 */
	using DatablockHandle = uint32;

	/** Defines a public pointer to a single structured block in our allocated space */
	using DatablockPtr = DatablockPtrType;

	/**
	 * @param InStruct Structure used to manage the data in our allocated block
	 * @param DesiredCapacity Number of allocations to reserve on this block. Will cap at MaxDatablockCapacity. 
	 * @param AllocNow If true, allocate right away. Otherwise alloc on first handle request. 
	 */
	FStructDatablock(const UScriptStruct* InStruct, const uint32 DesiredCapacity, bool AllocNow = false);

	/** Destroys this instance. Will deallocate all of our memory */
	~FStructDatablock();

	/**
	 * Returns a handle to a memory location ready to hold structure data
	 * @return a valid handle, or InvalidHandle if no more handles are available
	 */
	DatablockHandle NewHandle();

	/**
	 * Mark a handle as unused and available for others if needed
	 * @param Handle The handle we are recycling
	 */
	void RecycleHandle(DatablockHandle Handle);

	/**
	 * Returns the memory location for a structure given its handle.
	 * @param Handle Valid memory location handle for this block. 
	 */
	FORCEINLINE DatablockPtr GetMemoryBlock(DatablockHandle Handle)
	{
		check(Handle != InvalidHandle);
		return Datablock + static_cast<SIZE_T>(Handle) * StructSize;
	}

	/**
	 * De-allocates our block if: (1) free handles = capacity, or (2) no handles have been committed.
	 */
	void GarbageCollect();

private:
	/**
	 * Allocates our datablock. We can re-allocate if necessary, but a manual deletion has to happen first.
	 * This call allocates the full capacity of the datablock. 
	 */
	void Alloc();

	/**
	 * Deallocates our full datablock. All handles to our memory will be invalid.  
	 */
	void Dealloc();

	/** Struct used to manage our allocations. Assumed to be valid for the lifetime of this class */
	const UScriptStruct* Struct;

	/** Pointer to the location of our allocated memory */
	DatablockPtrType Datablock;

	/** Capacity of our allocated array (number of structures we can hold) */
	uint32 Capacity;

	/** Cached struct size */
	SIZE_T StructSize;

	/** Handle to the next available, unstructured datablock */
	DatablockHandle NextHandle;

	/** Cached struct name. See Dealloc() */
	FName StructName;

	/** Array of structured, recycled handles */
	TArray<DatablockHandle> FreeHandles;
};
