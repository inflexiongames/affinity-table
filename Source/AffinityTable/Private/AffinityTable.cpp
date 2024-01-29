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

#include "AffinityTable.h"
#include "AffinityTablePage.h"

#include "UObject/LinkerLoad.h"

DEFINE_LOG_CATEGORY(LogAffinityTable);

// CHANGELOG
// V: Change
// ----------------------------------------------------------------------------------------------------
// 1: Initial AT version
// 2: Structures are no longer transient since they must be loaded before this table can serialize.
// 3: Per-structure inheritance maps
// 4: Last known structure footprints
constexpr uint32 UAffinityTable::FileFormatVersion = 4;

// AffinityTable
//////////////////////////////////////////////////////////////////////////

bool UAffinityTable::CellTags::operator!=(const CellTags& Parent) const
{
	return Row != Parent.Row || Column != Parent.Column;
}

UAffinityTable::UAffinityTable(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
// Normally, fixed mode is only for gameplay, but we can easily change this later if required
#if WITH_EDITOR
	, bFixedModeActive(false)
#else
	, bFixedModeActive(true)
#endif
{
}

UAffinityTable::~UAffinityTable()
{
	ClearTable();
}

void UAffinityTable::GetPreloadDependencies(TArray<UObject*>& OutDeps)
{
	Super::GetPreloadDependencies(OutDeps);
	for (UScriptStruct* Structure : Structures)
	{
		OutDeps.Add(Structure);
	}
}

bool UAffinityTable::Query(const CellTags& InCellTags, const bool ExactMatch, TArray<const UScriptStruct*>& InStructureTypes, TArray<FAffinityTableCellDataWrapper>& OutMemoryPtrs) const
{
	bool QueryResult = false;
	if (Structures.Num())
	{
		// Make sure we have a result. Cell indexes will be invalid if we didn't find an exact match or a closest match.
		if (const Cell QueriedCell{ GetRowIndex(InCellTags.Row, ExactMatch), GetColumnIndex(InCellTags.Column, ExactMatch) };
			QueriedCell.Row != InvalidIndex && QueriedCell.Column != InvalidIndex)
		{
			// Insert data locations for all known requested structures. At this point, it is
			// an error to query a structure we don't know about.
			for (const UScriptStruct* Struct : InStructureTypes)
			{
				if (uint8* Data = GetCellData(QueriedCell, Struct))
				{
					// The wrapper is an inconvenience, but most of the time queries will come from
					// blueprint functions, which need it to move the data around.
					OutMemoryPtrs.Add(FAffinityTableCellDataWrapper(Data));
				}
				else
				{
					UE_LOG(LogAffinityTable, Error, TEXT("AffinityTable query requested the structure %s, not included on table %s (or the structure has no data)"), *Struct->GetName(), *GetPathName());
				}
			}
			QueryResult = (OutMemoryPtrs.Num() == InStructureTypes.Num());
		}
	}
	return QueryResult;
}

bool UAffinityTable::QueryForRow(const FGameplayTag& RowTag, const bool ExactMatch, TArray<const UScriptStruct*>& InStructureTypes, TArray<FCellDataArrayWrapper>& OutMemoryPtrs) const
{
	bool QueryResult = false;
	if (Structures.Num())
	{
		// Make sure we have a result. Will be invalid if we didn't find an exact match or a closest match.
		if (const TagIndex RowIndex = GetRowIndex(RowTag, ExactMatch);
			RowIndex != InvalidIndex)
		{
			TArray<uint8*> Data;
			// Insert data locations for all known requested structures. At this point, it is
			// an error to query a structure we don't know about.
			for (const UScriptStruct* Struct : InStructureTypes)
			{
				GetRowData(RowIndex, Struct, Data);
				if (Data.Num() > 0)
				{
					const int CurrIndex = OutMemoryPtrs.Num();
					// The wrapper is an inconvenience, but most of the time queries will come from
					// blueprint functions, which need it to move the data around.
					OutMemoryPtrs.Add(FCellDataArrayWrapper());

					for (uint8* CellData : Data)
					{
						OutMemoryPtrs[CurrIndex].CellDataArray.Add(FAffinityTableCellDataWrapper(CellData));
					}
					Data.Empty(Data.Num());
				}
				else
				{
					UE_LOG(LogAffinityTable, Error, TEXT("AffinityTable QueryForRow requested the structure %s, not included on table %s (or the structure has no data)"), *Struct->GetName(), *GetPathName());
				}
			}
			QueryResult = (OutMemoryPtrs.Num() == InStructureTypes.Num());
		}
	}
	return QueryResult;
}

UAffinityTable::TagIndex UAffinityTable::GetRowIndex(const FGameplayTag& InTag, bool ExactMatch) const
{
	return GetIndex(Rows, InTag, ExactMatch);
}

UAffinityTable::TagIndex UAffinityTable::GetColumnIndex(const FGameplayTag& InTag, bool ExactMatch) const
{
	return GetIndex(Columns, InTag, ExactMatch);
}

uint8* UAffinityTable::GetCellData(const Cell InCell, const UScriptStruct* InScriptStruct) const
{
	FStructDatablock::DatablockPtr Data = nullptr;
	if (const FAffinityTablePage* Page = GetPageForStruct(InScriptStruct))
	{
		Data = Page->GetDatablockPtr(InCell.Row, InCell.Column);
	}
	return Data;
}

void UAffinityTable::GetRowData(const TagIndex RowIndex, const UScriptStruct* InScriptStruct, TArray<uint8*>& OutData) const
{
	if (const FAffinityTablePage* Page = GetPageForStruct(InScriptStruct))
	{
		Page->GetDatablockPtrsForRow(RowIndex, OutData);
	}
}

#if WITH_EDITOR

void UAffinityTable::SetStructureChangeCallback(const StructureChangeCallback& InCallback)
{
	ChangeCallback = InCallback;
}

void UAffinityTable::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Respond to Structure changes
	if (const FName PropertyName = (PropertyChangedEvent.Property != nullptr)
									   ? PropertyChangedEvent.Property->GetFName()
									   : NAME_None;
		PropertyName == GET_MEMBER_NAME_CHECKED(UAffinityTable, Structures))
	{
		static EPropertyChangeType::Type ObservedChanges = EPropertyChangeType::ValueSet | EPropertyChangeType::ArrayRemove | EPropertyChangeType::ArrayClear;
		if (PropertyChangedEvent.ChangeType & ObservedChanges)
		{
			// Allocate memory for new pages. We must keep the dimensionality of other pages, which might include currently unused (deleted) rows.
			// On the other hand, if we have no pages yet, go with the number of registered rows/columns
			uint32 RowCount = Rows.Num();
			uint32 ColumnCount = Columns.Num();
			if (Pages.Num())
			{
				const TSharedRef<FAffinityTablePage> ExistingPage = Pages[0];
				ExistingPage->GetRowAndColumnCount(RowCount, ColumnCount);
			}

			AllocatePageMemory(RowCount, ColumnCount);

			if (ChangeCallback)
			{
				ChangeCallback(PropertyChangedEvent.ChangeType);
			}
		}
	}
}

// The following 4 functions could be collapsed into fronts with a common add and a common delete, but
// the gains are not much in terms of space or simplicity.

bool UAffinityTable::AddRow(const FGameplayTag& InTag)
{
	if (InTag.IsValid() && !Rows.Contains(InTag))
	{
		MarkPackageDirty();
		for (const TSharedRef<FAffinityTablePage>& Page : Pages)
		{
			Page->AddRow();
		}
		Rows.Add(InTag, NextRowIndex++);

		// Recursive add
		const FGameplayTag Parent = InTag.RequestDirectParent();
		AddRow(Parent);
		return true;
	}
	return false;
}

bool UAffinityTable::AddColumn(const FGameplayTag& InTag)
{
	if (InTag.IsValid() && !Columns.Contains(InTag))
	{
		MarkPackageDirty();
		for (const TSharedRef<FAffinityTablePage>& Page : Pages)
		{
			Page->AddColumn();
		}
		Columns.Add(InTag, NextColumnIndex++);

		const FGameplayTag Parent = InTag.RequestDirectParent();
		AddColumn(Parent);
		return true;
	}
	return false;
}

void UAffinityTable::DeleteRow(const FGameplayTag& InTag)
{
	if (InTag.IsValid() && Rows.Contains(InTag))
	{
		MarkPackageDirty();
		const TagIndex RowIndex = Rows[InTag];
		for (const TSharedRef<FAffinityTablePage>& Page : Pages)
		{
			Page->DeleteRow(RowIndex);
		}
		Rows.Remove(InTag);
		if (RowColors.Contains(InTag))
		{
			RowColors.Remove(InTag);
		}
	}
}

void UAffinityTable::DeleteColumn(const FGameplayTag& InTag)
{
	if (InTag.IsValid() && Columns.Contains(InTag))
	{
		MarkPackageDirty();
		const TagIndex ColIndex = Columns[InTag];
		for (const TSharedRef<FAffinityTablePage>& Page : Pages)
		{
			Page->DeleteColumn(ColIndex);
		}
		Columns.Remove(InTag);
		if (ColumnColors.Contains(InTag))
		{
			ColumnColors.Remove(InTag);
		}
	}
}

void UAffinityTable::SetTagColor(const FGameplayTag& InTag, const FLinearColor& Color, const bool IsRowTag)
{
	if (TMap<FGameplayTag, FLinearColor>& ColorMap = IsRowTag
														 ? RowColors
														 : ColumnColors;
		!ColorMap.Contains(InTag))
	{
		ColorMap.Add(InTag, Color);
		MarkPackageDirty();
	}
	else if (ColorMap[InTag] != Color)
	{
		ColorMap[InTag] = Color;
		MarkPackageDirty();
	}
}

bool UAffinityTable::TryGetTagColor(const FGameplayTag& InTag, FLinearColor& OutColor, const bool IsRowTag) const
{
	if (const TMap<FGameplayTag, FLinearColor>& ColorMap = IsRowTag
															   ? RowColors
															   : ColumnColors;
		ColorMap.Contains(InTag))
	{
		OutColor = ColorMap[InTag];
		return true;
	}
	return false;
}

void UAffinityTable::SetInheritanceLink(const UScriptStruct* InStruct, const CellTags& Child, const CellTags& Parent)
{
	check(InStruct);

	InheritanceMap& Map = InheritanceMaps.FindOrAdd(InStruct->GetFName());
	if (const FString CellID = StringIDForCell(Child);
		!Map.Contains(CellID))
	{
		Map.Add(CellID, Parent);
		MarkPackageDirty();
	}
	else if (Map[CellID] != Parent)
	{
		Map[CellID] = Parent;
		MarkPackageDirty();
	}
}

bool UAffinityTable::TryGetInheritanceLink(const UScriptStruct* InStruct, const CellTags& Child, CellTags& OutParent)
{
	check(InStruct);

	InheritanceMap& Map = InheritanceMaps.FindOrAdd(InStruct->GetFName());
	if (const FString CellID = StringIDForCell(Child);
		Map.Contains(CellID))
	{
		OutParent = Map[CellID];
		return true;
	}
	return false;
}

void UAffinityTable::RemoveInheritanceLink(const UScriptStruct* InStruct, const CellTags& InCell)
{
	check(InStruct);
	FString CellID = StringIDForCell(InCell);
	SetInheritanceLink(InStruct, InCell, CellTags());
}

bool UAffinityTable::AreCellsIdentical(const UScriptStruct* Struct, const Cell& CellA, const Cell& CellB) const
{
	if (Struct && Struct->IsValidLowLevel() && Structures.Contains(Struct))
	{
		const uint8* DataA = GetCellData(CellA, Struct);
		const uint8* DataB = GetCellData(CellB, Struct);
		if (DataA && DataB)
		{
			return Struct->CompareScriptStruct(DataA, DataB, PPF_DeepComparison);
		}
	}
	return false;
}

void UAffinityTable::PreSaveTable()
{
	// Fix-up our data: Unreal maps do not necessarily retrieve keys in insertion order, but indexes
	// are always ordered sequentially. We need to store rows/cols in the exact order we want to read them later.
	static auto IndexSort = [](const TagIndex A, const TagIndex B) { return A < B; };
	Rows.ValueSort(IndexSort);
	Columns.ValueSort(IndexSort);

	// Rows
	Rows.GenerateKeyArray(RowTags);

	// Columns
	Columns.GenerateKeyArray(ColumnTags);
}

void UAffinityTable::SaveTable(FArchive& Ar)
{
	// Runtime data
	//////////////////////////////////////////////////////////////////////////

	// Format version
	uint32 CurrentFormat = FileFormatVersion;
	Ar << CurrentFormat;

	// Per-page data in row-major order, following insertion. Be pedantic and only save structures that have memory pages
	// [Structure pathname, R0{data 0, ...data n}, ...Rm], ...
	using ScriptPagePair = TPair<UScriptStruct*, FAffinityTablePage*>;
	TArray<ScriptPagePair> StructsToSave;
	for (UScriptStruct* Struct : Structures)
	{
		FAffinityTablePage* Page = GetPageForStruct(Struct);
		if (Page && Struct)
		{
			ScriptPagePair Pair(Struct, Page);
			StructsToSave.Add(Pair);
		}
	}

	int32 PagesToSave = StructsToSave.Num();
	Ar << PagesToSave;

	for (const ScriptPagePair& Pair : StructsToSave)
	{
		FString StructName = Pair.Key->GetFName().ToString();
		int32 StructFootprint = Pair.Value->GetStructSize();

		Ar << StructName;
		Ar << StructFootprint;
		SerializePage(Ar, Pair.Value, Pair.Key);
	}

	// Editor-only data
	//////////////////////////////////////////////////////////////////////////

	// Row and column colors
	Ar << RowColors;
	Ar << ColumnColors;

	// Inheritance map:
	// let Links(page) = [ (Key, Row, Col), ... ] for each element of Map(page)
	// let n = PagesToSave
	// Then, serialized maps = Page 0 { Struct name, link count, Links }, ... Page n
	Ar << PagesToSave;
	for (const ScriptPagePair& Pair : StructsToSave)
	{
		FName StructName = Pair.Key->GetFName();
		Ar << StructName;

		int32 LinkCount = InheritanceMaps.Contains(StructName) ? InheritanceMaps[StructName].Num() : 0;
		Ar << LinkCount;

		if (LinkCount)
		{
			for (TPair<FString, CellTags>& CellLink : InheritanceMaps[StructName])
			{
				Ar << CellLink.Key;
				Ar << CellLink.Value.Row;
				Ar << CellLink.Value.Column;
			}
		}
	}
}

void UAffinityTable::EnsureTagHierarchy()
{
	// Tag hierarchies break if we delete non-leaf tags, leaving their children dangling. Because
	// ATs assume tags are continuous, we must ensure the taxonomy is safe.
	auto FindOrphans = [](TSet<FGameplayTag>& Orphans, TArray<FGameplayTag>& Tags) {
		TSet<FGameplayTag> Tails;
		for (const FGameplayTag& Tag : Tags)
		{
			FGameplayTag ParentTag = Tag.RequestDirectParent();

			bool bFoundOrphan{ false };
			while (ParentTag.IsValid() && !bFoundOrphan)
			{
				// If our tails contain this sequence, we are guaranteed to be continuous
				if (Tails.Contains(ParentTag))
				{
					ParentTag = FGameplayTag::EmptyTag;
				}
				// If we know about this parent, it is safe to keep on going
				else if (Tags.Contains(ParentTag))
				{
					ParentTag = ParentTag.RequestDirectParent();
				}
				// Otherwise this tag is broken upstream
				else
				{
					Orphans.Add(Tag);
					bFoundOrphan = true;
				}
			};

			if (!bFoundOrphan)
			{
				Tails.Add(Tag);
			}
		}
	};

	TSet<FGameplayTag> OrphanRows;
	FindOrphans(OrphanRows, RowTags);
	for (const FGameplayTag& Row : OrphanRows)
	{
		UE_LOG(LogAffinityTable, Error, TEXT("The row tag %s on affinity table %s has a broken hierarchy and will be deleted."
											 "Please rename this tag to an appropriate value, restore the table from source control, and try again"),
			*Row.ToString(), *GetPathName());
		DeleteRow(Row);
	}

	TSet<FGameplayTag> OrphanColumns;
	FindOrphans(OrphanColumns, ColumnTags);
	for (const FGameplayTag& Column : OrphanColumns)
	{
		UE_LOG(LogAffinityTable, Error, TEXT("The column tag %s on affinity table %s has a broken hierarchy and will be deleted."
											 "Please rename this tag to an appropriate value, restore the table from source control, and try again"),
			*Column.ToString(), *GetPathName());
		DeleteColumn(Column);
	}

	bHasLoadingErrors |= OrphanRows.Num() > 0 || OrphanColumns.Num() > 0;
}
#endif

void UAffinityTable::LoadTable(FArchive& Ar)
{
	uint32 ArchiveFormat = 0;

	ClearTable();

	// Version check
	//////////////////////////////////////////////////////////////////////////

	Ar << ArchiveFormat;
	if (ArchiveFormat < FileFormatVersion)
	{
		UE_LOG(LogAffinityTable, Log, TEXT("Upgrading Affinity table %s from format version %d to latest format version (%d)"), *GetPathName(), ArchiveFormat, FileFormatVersion);

		// Supported migrations
		switch (ArchiveFormat)
		{
			case 3:
				LoadTable_V3(Ar);
				break;

			default:
				UE_LOG(LogAffinityTable, Error, TEXT("Unsupported file format on table %s: version (%d) cannot be converted to version (%d)"),
					*GetPathName(), ArchiveFormat, FileFormatVersion);
		}
		return;
	}

	// Runtime data
	//////////////////////////////////////////////////////////////////////////

	// Populate our row and column map lookup table
	GenerateRowAndColumnMaps();

#if UE_BUILD_DEVELOPMENT
	// Don't continue if we have errors at this point: our memory footprints will not match
	if (bHasLoadingErrors)
	{
		UE_LOG(LogAffinityTable, Error, TEXT("Row or column number mismatch in affinity table %s. Cannot reload from disk. "
											 "Please revert to a version of the table where the tags were stable, and redo modifications carefully."),
			*GetPathName());
		return;
	}
#endif

	const uint32 RowCount = Rows.Num();
	const uint32 ColCount = Columns.Num();

	int32 PagesToLoad;
	FString StructureName;
	int32 StructureFootprint;

	// An array to verify the footprint versions
	TArray<int32> OldFootprints;

	// Structures and structure memory
	Ar << PagesToLoad;
	while (PagesToLoad)
	{
		Ar << StructureName;
		Ar << StructureFootprint;
		OldFootprints.Add(StructureFootprint);

		UScriptStruct** FoundStruct = Structures.FindByPredicate([&StructureName](const UScriptStruct* Struct) { return Struct && Struct->GetFName().ToString() == StructureName; });
		if (!FoundStruct)
		{
			UE_LOG(LogAffinityTable, Error, TEXT("The Affinity table %s does not contain the requested structure %s"), *GetPathName(), *StructureName);
			bHasLoadingErrors = true;
			return;
		}
		UScriptStruct* ScriptStruct = *FoundStruct;

		// Loading a structure is not enough to get its internals properly set-up. You may need
		// to manually link it, with its very own linker.
		EnsureStructIsLoaded(ScriptStruct);

		TSharedRef<FAffinityTablePage> Page(new FAffinityTablePage(ScriptStruct, RowCount, ColCount, bFixedModeActive));
		Pages.Add(Page);

		SerializePage(Ar, &Page.Get(), ScriptStruct);

		PagesToLoad--;
	}

#if !UE_BUILD_SHIPPING && !UE_SERVER
	// Verify page integrity. Do this only for dev builds, as production/final builds will contain a smaller footprint
	// regardless, and this will create unnecessary log spam.
	for (int i = 0; i < OldFootprints.Num(); ++i)
	{
		if (Pages[i]->GetStructSize() != OldFootprints[i])
		{
			UE_LOG(LogAffinityTable, Warning, TEXT("The structure %s footprint on AffinityTable %s changed from %d to %d since the last time it was saved, "
												   "please ensure to re-save and submit the table to correct this and prevent unexpected data."),
				*Pages[i]->GetStruct()->GetFName().ToString(), *GetPathName(), OldFootprints[i], Pages[i]->GetStructSize());
		}
	}
#endif

	// Editor-only data
	//////////////////////////////////////////////////////////////////////////

	// Row and column colors
	Ar << RowColors;
	Ar << ColumnColors;

	// Inheritance graph
	Ar << PagesToLoad;
	while (PagesToLoad)
	{
		FName StructName;
		Ar << StructName;

		int32 LinkCount = 0;
		Ar << LinkCount;

		if (LinkCount)
		{
			InheritanceMap& Map = InheritanceMaps.FindOrAdd(StructName);
			FString CellID;
			CellTags ParentCell;

			for (int32 i = 0; i < LinkCount; ++i)
			{
				Ar << CellID;
				Ar << ParentCell.Row;
				Ar << ParentCell.Column;
				Map.Add(CellID, ParentCell);
			}
		}
		PagesToLoad--;
	}

#if WITH_EDITOR
	// Fixup our tags
	EnsureTagHierarchy();
#endif
}

void UAffinityTable::GenerateRowAndColumnMaps()
{
	// Must check, because calling this function twice during the lifetime of the table is an
	// error that will likely take us to out of bound index access.
	check(NextRowIndex == 0 && Rows.Num() == 0);
	check(NextColumnIndex == 0 && Columns.Num() == 0);

	// Rows
	for (FGameplayTag Tag : RowTags)
	{
#if UE_BUILD_DEVELOPMENT
		if (Rows.Contains(Tag))
		{
			UE_LOG(LogAffinityTable, Error, TEXT("Duplicated row tag %s found on Affinity Table %s, only the first one will be added. "
												 "Verify the integrity of the table's information before saving and submitting to source control"),
				*Tag.ToString(), *GetPathName());
			bHasLoadingErrors = true;
			continue;
		}
#endif
		Rows.Add(Tag, NextRowIndex++);
	}

	// Columns
	for (FGameplayTag Tag : ColumnTags)
	{
#if UE_BUILD_DEVELOPMENT
		if (Columns.Contains(Tag))
		{
			UE_LOG(LogAffinityTable, Error, TEXT("Duplicated column tag %s found on Affinity Table %s, only the first one will be added. "
												 "Verify the integrity of the table's information before saving and submitting to source control"),
				*Tag.ToString(), *GetPathName());
			bHasLoadingErrors = true;
			continue;
		}
#endif
		Columns.Add(Tag, NextColumnIndex++);
	}
}

// AT's did not remember their page's structure footprint at v3
void UAffinityTable::LoadTable_V3(FArchive& Ar)
{
	// Runtime data
	//////////////////////////////////////////////////////////////////////////

	// Populate our row and column map lookup table
	GenerateRowAndColumnMaps();

#if UE_BUILD_DEVELOPMENT
	// Don't continue if we have errors at this point: our memory footprints will not match
	if (bHasLoadingErrors)
	{
		UE_LOG(LogAffinityTable, Error, TEXT("Row or column number mismatch in affinity table %s. Cannot reload from disk. "
											 "Please revert to a version of the table where the tags were stable, and redo modifications carefully."),
			*GetPathName());
		return;
	}
#endif

	const uint32 RowCount = Rows.Num();
	const uint32 ColCount = Columns.Num();

	int32 PagesToLoad;
	FString StructureName;

	// Structures and structure memory
	Ar << PagesToLoad;
	while (PagesToLoad)
	{
		Ar << StructureName;
		UScriptStruct** FoundStruct = Structures.FindByPredicate([&StructureName](const UScriptStruct* Struct) { return Struct && Struct->GetFName().ToString() == StructureName; });
		if (!FoundStruct)
		{
			UE_LOG(LogAffinityTable, Error, TEXT("The Affinity table %s does not contain the requested structure %s"), *GetPathName(), *StructureName);
			bHasLoadingErrors = true;
			return;
		}
		UScriptStruct* ScriptStruct = *FoundStruct;

		// Loading a structure is not enough to get its internals properly set-up. You may need
		// to manually link it, with its very own linker.
		EnsureStructIsLoaded(ScriptStruct);

		TSharedRef<FAffinityTablePage> Page(new FAffinityTablePage(ScriptStruct, RowCount, ColCount, bFixedModeActive));
		Pages.Add(Page);

		SerializePage(Ar, &Page.Get(), ScriptStruct);

		PagesToLoad--;
	}

	// Editor-only data
	//////////////////////////////////////////////////////////////////////////

	// Row and column colors
	Ar << RowColors;
	Ar << ColumnColors;

	// Inheritance graph
	Ar << PagesToLoad;
	while (PagesToLoad)
	{
		FName StructName;
		Ar << StructName;

		int32 LinkCount = 0;
		Ar << LinkCount;

		if (LinkCount)
		{
			InheritanceMap& Map = InheritanceMaps.FindOrAdd(StructName);
			FString CellID;
			CellTags ParentCell;

			for (int32 i = 0; i < LinkCount; ++i)
			{
				Ar << CellID;
				Ar << ParentCell.Row;
				Ar << ParentCell.Column;
				Map.Add(CellID, ParentCell);
			}
		}
		PagesToLoad--;
	}

#if WITH_EDITOR
	// Fixup our tags
	EnsureTagHierarchy();
#endif
}

void UAffinityTable::ClearTable()
{
	// Destroy any existing memory pages, reset our rows, columns, and index counters.
	Pages.Empty();
	Rows.Empty();
	Columns.Empty();
	RowColors.Empty();
	ColumnColors.Empty();
	InheritanceMaps.Empty();

	NextRowIndex = 0;
	NextColumnIndex = 0;
}

void UAffinityTable::Serialize(FArchive& Ar)
{
#if WITH_EDITOR
	if (Ar.IsSaving())
	{
		PreSaveTable();
	}
#endif

	Super::Serialize(Ar);

	if (Ar.IsLoading())
	{
		LoadTable(Ar);
	}

#if WITH_EDITOR
	if (Ar.IsSaving())
	{
		SaveTable(Ar);
	}
#endif
}

void UAffinityTable::SerializePage(FArchive& Ar, const FAffinityTablePage* Page, UScriptStruct* Struct)
{
	for (const TPair<FGameplayTag, TagIndex> Row : Rows)
	{
		for (const TPair<FGameplayTag, TagIndex> Column : Columns)
		{
			const FStructDatablock::DatablockPtr DataPtr = Page->GetDatablockPtr(Row.Value, Column.Value);
			if (!DataPtr)
			{
				UE_LOG(LogAffinityTable, Error, TEXT("Missing memory location for row %s and column %s on page %s for table %s"), *Row.Key.GetTagName().ToString(), *Column.Key.GetTagName().ToString(), *Struct->GetName(), *GetPathName());
				continue;
			}
			Struct->SerializeItem(Ar, DataPtr, nullptr);
		}
	}
}

// Allocates space for a number of blocks, but doesn't commit cell handles.
void UAffinityTable::AllocatePageMemory(const uint32 InRows, const uint32 InColumns)
{
	// Add new structures
	for (const UScriptStruct* ScriptStruct : Structures)
	{
		if (ScriptStruct && !GetPageForStruct(ScriptStruct))
		{
			TSharedRef<FAffinityTablePage> NewPage(new FAffinityTablePage(ScriptStruct, InRows, InColumns, bFixedModeActive));
			Pages.Add(NewPage);
		}
	}

	// Remove orphan structures
	for (auto PageIt = Pages.CreateIterator(); PageIt; ++PageIt)
	{
		const UScriptStruct* PageStruct = (*PageIt)->GetStruct();
		if (!Structures.FindByPredicate([PageStruct](const UScriptStruct* Struct) { return PageStruct == Struct; }))
		{
			PageIt.RemoveCurrent();
		}
	}
}

FString UAffinityTable::StringIDForCell(const CellTags& InCell)
{
	return FString::Printf(TEXT("%s|%s"), *InCell.Row.ToString(), *InCell.Column.ToString());
}

UAffinityTable::TagIndex UAffinityTable::GetIndex(const TMap<FGameplayTag, TagIndex>& InMap, const FGameplayTag& InTag, const bool ExactMatch)
{
	TagIndex Index = InvalidIndex;
	if (InTag.IsValid())
	{
		// Try and find an exact match
		if (InMap.Contains(InTag))
		{
			Index = InMap[InTag];
		}
		// Otherwise, if we allow closest match, go up one level
		else if (!ExactMatch)
		{
			Index = GetIndex(InMap, InTag.RequestDirectParent(), ExactMatch);
		}
	}
	return Index;
}

void UAffinityTable::EnsureStructIsLoaded(UScriptStruct* ScriptStruct) const
{
	check(ScriptStruct);
	if (!ScriptStruct->GetStructureSize() && ScriptStruct->HasAnyFlags(RF_NeedLoad))
	{
		if (const auto Linker = ScriptStruct->GetLinker();
			Linker && (!GEventDrivenLoaderEnabled || !EVENT_DRIVEN_ASYNC_LOAD_ACTIVE_AT_RUNTIME))
		{
			Linker->Preload(ScriptStruct);
		}
		else
		{
			UE_LOG(LogAffinityTable, Error, TEXT("Structure %s on table %s failed to load on time"), *ScriptStruct->GetFName().ToString(), *GetPathName());
		}
	}

	if (!IsValid(ScriptStruct))
	{
		UE_LOG(LogAffinityTable, Error, TEXT("AffinityTable::EnsureStructIsLoaded(...) failed to load a UScriptStruct."));
	}
}

FAffinityTablePage* UAffinityTable::GetPageForStruct(const UScriptStruct* InScriptStruct) const
{
	if (InScriptStruct != nullptr)
	{
		const TSharedRef<FAffinityTablePage>* RequestedPage = Pages.FindByPredicate([InScriptStruct](const TSharedRef<FAffinityTablePage>& Page) {
			return Page->GetStruct() == InScriptStruct;
		});
		return RequestedPage ? &RequestedPage->Get() : nullptr;
	}
	return nullptr;
}
