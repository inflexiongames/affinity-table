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


#include "AffinityTable.h"
#include "AffinityTablePage.h"

#include "EditorFramework/AssetImportData.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/LinkerLoad.h"

DEFINE_LOG_CATEGORY(LogAffinityTable);

// CHANGELOG
// V: Change
// ----------------------------------------------------------------------------------------------------
// 1: Initial AT version
// 2: Structures are no longer transient since they must be loaded before this table can serialize.
// 3: Per-structure inheritance maps
const uint32 UAffinityTable::FileFormatVersion = 3;

// AffinityTable
//////////////////////////////////////////////////////////////////////////

UAffinityTable::UAffinityTable(const FObjectInitializer& ObjectInitializer) :
	Super(ObjectInitializer),
	NextRowIndex(0),
	NextColumnIndex(0),
// Normally, fixed mode is only for gameplay, but we can easily change this later if required
#if WITH_EDITOR
	FixedModeActive(false)
#else
	FixedModeActive(true)
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

bool UAffinityTable::Query(const CellTags& InCellTags, bool ExactMatch, TArray<const UScriptStruct*>& InStructureTypes, TArray<FAffinityTableCellDataWrapper>& OutMemoryPtrs)
{
	bool QueryResult = false;
	if (Structures.Num())
	{
		Cell QueriedCell{ GetRowIndex(InCellTags.Row, ExactMatch), GetColumnIndex(InCellTags.Column, ExactMatch) };

		// Make sure we have a result. Cell indexes will be invalid if we didn't find an exact match or a closest match.
		if (QueriedCell.Row != InvalidIndex && QueriedCell.Column != InvalidIndex)
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

bool UAffinityTable::QueryForRow(const FGameplayTag& RowTag, bool ExactMatch, TArray<const UScriptStruct*>& InStructureTypes, TArray<FCellDataArrayWrapper>& OutMemoryPtrs)
{
	bool QueryResult = false;
	if (Structures.Num())
	{
		TagIndex RowIndex = GetRowIndex(RowTag, ExactMatch);

		// Make sure we have a result. Will be invalid if we didn't find an exact match or a closest match.
		if (RowIndex != InvalidIndex)
		{
			TArray<uint8*> Data;
			// Insert data locations for all known requested structures. At this point, it is
			// an error to query a structure we don't know about.
			for (const UScriptStruct* Struct : InStructureTypes)
			{
				GetRowData(RowIndex, Struct, Data);
				if (Data.Num() > 0)
				{
					int CurrIndex = OutMemoryPtrs.Num();
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

uint8* UAffinityTable::GetCellData(const Cell& InCell, const UScriptStruct* InScriptStruct)
{
	FStructDatablock::DatablockPtr Data = nullptr;
	FAffinityTablePage* Page = GetPageForStruct(InScriptStruct);
	if (Page)
	{
		Data = Page->GetDatablockPtr(InCell.Row, InCell.Column);
	}
	return Data;
}

void UAffinityTable::GetRowData(TagIndex RowIndex, const UScriptStruct* InScriptStruct, TArray<uint8*>& OutData)
{
	FAffinityTablePage* Page = GetPageForStruct(InScriptStruct);
	if (Page)
	{
		Page->GetDatablockPtrsForRow(RowIndex, OutData);
	}
}

#if WITH_EDITOR

void UAffinityTable::SetStructureChangeCallback(StructureChangeCallback InCallback)
{
	ChangeCallback = InCallback;
}

void UAffinityTable::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Respond to Structure changes
	const FName PropertyName = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UAffinityTable, Structures))
	{
		static EPropertyChangeType::Type ObservedChanges = EPropertyChangeType::ValueSet | EPropertyChangeType::ArrayRemove;
		if (PropertyChangedEvent.ChangeType & ObservedChanges)
		{
			// Allocate memory for new pages. We must keep the dimensionality of other pages, which might include currently unused (deleted) rows.
			// On the other hand, if we have no pages yet, go with the number of registered rows/columns
			uint32 RowCount = Rows.Num();
			uint32 ColumnCount = Columns.Num();
			if (Pages.Num())
			{
				TSharedRef<FAffinityTablePage> ExistingPage = Pages[0];
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

bool UAffinityTable::AddRow(FGameplayTag& InTag)
{
	if (InTag.IsValid() && !Rows.Contains(InTag))
	{
		MarkPackageDirty();
		for (TSharedRef<FAffinityTablePage>& Page : Pages)
		{
			Page->AddRow();
		}
		Rows.Add(InTag, NextRowIndex++);

		// Recursive add
		FGameplayTag Parent = InTag.RequestDirectParent();
		AddRow(Parent);
		return true;
	}
	return false;
}

bool UAffinityTable::AddColumn(FGameplayTag& InTag)
{
	if (InTag.IsValid() && !Columns.Contains(InTag))
	{
		MarkPackageDirty();
		for (TSharedRef<FAffinityTablePage>& Page : Pages)
		{
			Page->AddColumn();
		}
		Columns.Add(InTag, NextColumnIndex++);

		FGameplayTag Parent = InTag.RequestDirectParent();
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
		TagIndex RowIndex = Rows[InTag];
		for (TSharedRef<FAffinityTablePage>& Page : Pages)
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
		TagIndex ColIndex = Columns[InTag];
		for (TSharedRef<FAffinityTablePage>& Page : Pages)
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

void UAffinityTable::SetTagColor(const FGameplayTag& InTag, const FLinearColor& Color, bool IsRowTag)
{
	TMap<FGameplayTag, FLinearColor>& ColorMap = IsRowTag ? RowColors : ColumnColors;
	if (!ColorMap.Contains(InTag))
	{
		ColorMap.Add(InTag, Color);
	}
	else
	{
		ColorMap[InTag] = Color;
	}
	MarkPackageDirty();
}

bool UAffinityTable::TryGetTagColor(const FGameplayTag& InTag, FLinearColor& OutColor, bool IsRowTag) const
{
	const TMap<FGameplayTag, FLinearColor>& ColorMap = IsRowTag ? RowColors : ColumnColors;
	if (ColorMap.Contains(InTag))
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
	FString CellID = StringIDForCell(Child);
	if (!Map.Contains(CellID))
	{
		Map.Add(CellID, Parent);
	}
	else
	{
		Map[CellID] = Parent;
	}
}

bool UAffinityTable::TryGetInheritanceLink(const UScriptStruct* InStruct, const CellTags& Child, CellTags& OutParent)
{
	check(InStruct);

	InheritanceMap& Map = InheritanceMaps.FindOrAdd(InStruct->GetFName());
	FString CellID = StringIDForCell(Child);
	if (Map.Contains(CellID))
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

void UAffinityTable::PreSaveTable()
{
	// Fix-up our data: Unreal maps do not necessarily retrieve keys in insertion order, but indexes
	// are always ordered sequentially. We need to store rows/cols in the exact order we want to read them later.
	static auto IndexSort = [](const TagIndex& A, const TagIndex& B) { return A < B; };
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

	for (ScriptPagePair& Pair : StructsToSave)
	{
		FString StructName = Pair.Key->GetFName().ToString();
		Ar << StructName;
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
	for (ScriptPagePair& Pair : StructsToSave)
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
			case 2:
				//LoadTable_V2(Ar);
				//break;

			default:
				UE_LOG(LogAffinityTable, Error, TEXT("Unsupported file format on table %s: version (%d) cannot be converted to version (%d)"),
					*GetPathName(), ArchiveFormat, FileFormatVersion);
		}
		return;
	}

	// Runtime data
	//////////////////////////////////////////////////////////////////////////

	uint32 RowCount, ColCount = 0;
	int32 PagesToLoad;
	FString StructureName;
	bool bIsLoading = Ar.IsLoading();

	// Rows
	for (FGameplayTag Tag : RowTags)
	{
		Rows.Add(Tag, NextRowIndex++);
	}

	// Columns
	for (FGameplayTag Tag : ColumnTags)
	{
		Columns.Add(Tag, NextColumnIndex++);
	}

	RowCount = Rows.Num();
	ColCount = Columns.Num();

	// Structures and structure memory
	Ar << PagesToLoad;
	while (PagesToLoad)
	{
		Ar << StructureName;
		UScriptStruct** FoundStruct = Structures.FindByPredicate([&StructureName](const UScriptStruct* Struct) { return Struct && Struct->GetFName().ToString() == StructureName; });
		if (!FoundStruct)
		{
			UE_LOG(LogAffinityTable, Error, TEXT("The Affinity table %s does not contain the requested structure %s"), *GetPathName(), *StructureName);
			return;
		}
		UScriptStruct* ScriptStruct = *FoundStruct;

		// Loading a structure is not enough to get its internals properly set-up. You may need
		// to manually link it, with its very own linker.
		EnsureStructIsLoaded(ScriptStruct);

		TSharedRef<FAffinityTablePage> Page(new FAffinityTablePage(ScriptStruct, RowCount, ColCount, FixedModeActive));
		Pages.Add(Page);

		SerializePage(Ar, &Page.Get(), ScriptStruct);

		PagesToLoad--;
	}

	// Editor-only data
	// TODO: We can probably cut this section if we are not running the editor.
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
}

//left as comment for example of a version migration.
//void UAffinityTable::LoadTable_V2(FArchive& Ar)
//{
//	uint32 RowCount, ColCount = 0;
//	int32 PagesToLoad;
//	FString StructureName;
//	bool bIsLoading = Ar.IsLoading();
//
//	// Rows
//	for (FGameplayTag Tag : RowTags)
//	{
//		Rows.Add(Tag, NextRowIndex++);
//	}
//
//	// Columns
//	for (FGameplayTag Tag : ColumnTags)
//	{
//		Columns.Add(Tag, NextColumnIndex++);
//	}
//
//	RowCount = Rows.Num();
//	ColCount = Columns.Num();
//
//	// Structures and structure memory
//	Ar << PagesToLoad;
//	while (PagesToLoad)
//	{
//		Ar << StructureName;
//		UScriptStruct** FoundStruct = Structures.FindByPredicate([&StructureName](const UScriptStruct* Struct) { return Struct && Struct->GetFName().ToString() == StructureName; });
//		if (!FoundStruct)
//		{
//			UE_LOG(LogAffinityTable, Error, TEXT("The Affinity table %s does not contain the requested structure %s"), *GetPathName(), *StructureName);
//			return;
//		}
//		UScriptStruct* ScriptStruct = *FoundStruct;
//
//		// Loading a structure is not enough to get its internals properly set-up. You may need
//		// to manually link it, with its very own linker.
//		EnsureStructIsLoaded(ScriptStruct);
//
//		TSharedRef<FAffinityTablePage> Page(new FAffinityTablePage(ScriptStruct, RowCount, ColCount, FixedModeActive));
//		Pages.Add(Page);
//
//		SerializePage(Ar, &Page.Get(), ScriptStruct);
//
//		PagesToLoad--;
//	}
//
//	// Editor-only data
//	// TODO: We can probably cut this section if we are not running the editor.
//	//////////////////////////////////////////////////////////////////////////
//
//	// Row and column colors
//	Ar << RowColors;
//	Ar << ColumnColors;
//
//	// Inheritance map: create a default graph and add for all valid structures
//	int32 LinkCount = 0;
//	FString CellID;
//	CellTags ParentCell;
//	InheritanceMap DefaultMap;
//
//	Ar << LinkCount;
//	for (int32 i = 0; i < LinkCount; ++i)
//	{
//		Ar << CellID;
//		Ar << ParentCell.Row;
//		Ar << ParentCell.Column;
//		DefaultMap.Add(CellID, ParentCell);
//	}
//
//	for (const UScriptStruct* ThisStruct : Structures)
//	{
//		InheritanceMap& ThisMap = InheritanceMaps.FindOrAdd(ThisStruct->GetFName());
//		ThisMap = DefaultMap;
//	}
//}

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

void UAffinityTable::SerializePage(FArchive& Ar, FAffinityTablePage* Page, UScriptStruct* Struct)
{
	for (const TPair<FGameplayTag, TagIndex> Row : Rows)
	{
		for (const TPair<FGameplayTag, TagIndex> Column : Columns)
		{
			FStructDatablock::DatablockPtr DataPtr = Page->GetDatablockPtr(Row.Value, Column.Value);
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
void UAffinityTable::AllocatePageMemory(uint32 InRows, uint32 InColumns)
{
	// Add new structures
	for (const UScriptStruct* ScriptStruct : Structures)
	{
		if (ScriptStruct && !GetPageForStruct(ScriptStruct))
		{
			TSharedRef<FAffinityTablePage> NewPage(new FAffinityTablePage(ScriptStruct, InRows, InColumns, FixedModeActive));
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

UAffinityTable::TagIndex UAffinityTable::GetIndex(const TMap<FGameplayTag, TagIndex>& InMap, const FGameplayTag& InTag, bool ExactMatch) const
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

void UAffinityTable::EnsureStructIsLoaded(UScriptStruct* ScriptStruct)
{
	check(ScriptStruct);
	if (!ScriptStruct->GetStructureSize() && ScriptStruct->HasAnyFlags(RF_NeedLoad))
	{
		auto Linker = ScriptStruct->GetLinker();
		if (Linker && (!GEventDrivenLoaderEnabled || !EVENT_DRIVEN_ASYNC_LOAD_ACTIVE_AT_RUNTIME))
		{
			Linker->Preload(ScriptStruct);
		}
		else
		{
			UE_LOG(LogAffinityTable, Error, TEXT("Structure %s on table %s failed to load on time"), *ScriptStruct->GetFName().ToString(), *GetPathName());
		}
	}
}

FAffinityTablePage* UAffinityTable::GetPageForStruct(const UScriptStruct* InScriptStruct)
{
	if (InScriptStruct != nullptr)
	{
		TSharedRef<FAffinityTablePage>* RequestedPage = Pages.FindByPredicate([InScriptStruct](const TSharedRef<FAffinityTablePage>& Page) {
			return Page->GetStruct() == InScriptStruct;
		});
		return RequestedPage ? &RequestedPage->Get() : nullptr;
	}
	return nullptr;
}
