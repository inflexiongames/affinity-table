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
#include "AffinityTableEditor.h"
#include "AffinityTable.h"
#include "AffinityTableCell.h"
#include "AffinityTableEditorModule.h"
#include "AffinityTableHeader.h"
#include "AffinityTableListViewRow.h"
#include "AffinityTableStyles.h"
#include "DataTableUtils.h"
#include "Editor.h"
#include "EditorStyleSet.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GameplayTagsManager.h"
#include "IDetailsView.h"
#include "IStructureDetailsView.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "Styling/SlateIconFinder.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"

#include <functional>

DEFINE_LOG_CATEGORY(LogAffinityTableEditor);

#define LOCTEXT_NAMESPACE "AffinityTableEditor"

/*
 *	Tab identifiers for our main editor window
 */
struct FAffinityTableEditorTabs
{
	static const FName CellPropertiesID;
	static const FName TableViewportID;
	static const FName TablePropertiesID;
};

const FName FAffinityTableEditorTabs::CellPropertiesID(TEXT("CellProperties"));
const FName FAffinityTableEditorTabs::TableViewportID(TEXT("Viewport"));
const FName FAffinityTableEditorTabs::TablePropertiesID(TEXT("TableProperties"));
const FName FAffinityTableEditor::ColumnHeaderName("AffinityTableRoot");

namespace FAffinityTableEditorPrivate
{
const FString PreferenceSectionName(TEXT("AffinityTableEditor"));

}

// Tag Node Walkers
//////////////////////////////////////////////////////////////////////////////////////

/**
 * Gathers all visible nodes in the provided tree.
 * Visible nodes are added to the provided array. The root node is assumed visible.
 */
class FVisibleNodeWalker : public IAffinityTableNodeWalker
{
public:
	FVisibleNodeWalker(TArray<FAffinityTableNode::NodeSharedPtr>& InAvailableNodes)
		: AvailableNodes(InAvailableNodes)
	{
	}

	virtual ~FVisibleNodeWalker() override = default;

protected:
	virtual bool Visit(TWeakPtr<FAffinityTableNode>& InNode) override
	{
		const FAffinityTableNode::NodeSharedPtr Ptr = InNode.Pin();
		if (Ptr->GetTag().IsValid())
		{
			AvailableNodes.Add(Ptr);
		}
		return !Ptr->IsCollapsed();
	}

private:
	TArray<FAffinityTableNode::NodeSharedPtr>& AvailableNodes;
};

/**
 * Finds a node that contains the specified tag
 */
class FFindNodeWalker : public IAffinityTableNodeWalker
{
public:
	FFindNodeWalker(const FGameplayTag& InTag)
		: FoundNode(nullptr)
		, Tag(InTag)
	{
	}

	/** Retrieves the search result, if any. Null otherwise */
	const FAffinityTableNode* GetFoundNode() const
	{
		return FoundNode;
	}

protected:
	virtual bool Visit(FAffinityTableNode* Node) override
	{
		if (!FoundNode && Node->GetTag() == Tag)
		{
			FoundNode = Node;
			return false;
		}
		return true;
	}

private:
	const FAffinityTableNode* FoundNode;
	const FGameplayTag& Tag;
};

/**
 * Fills a list with cells underneath the provided column, following data inheritance rules.
 */
class FFillColumnDownWalker : public IAffinityTableNodeWalker
{
public:
	FFillColumnDownWalker(FAffinityTableEditor* InEditor, const FAffinityTableNode* InColumn, TArray<TWeakPtr<FAffinityTableEditor::Cell>>& OutCells, bool InForce)
		: Editor(InEditor)
		, Column(InColumn)
		, Cells(OutCells)
		, Force(InForce)
	{
	}

	virtual ~FFillColumnDownWalker() override = default;

protected:
	virtual bool Visit(FAffinityTableNode* Row) override
	{
		check(Row);

		const TSharedPtr<FAffinityTableEditor::Cell> ThisCell = Editor->GetCell(Row, Column).Pin();
		check(ThisCell.IsValid());

		// This cell is fair game if it is open (inheriting) and previously linked to our parent, or NOT linked to a cell in this row
		const FAffinityTableEditor::Cell* InheritedCell = ThisCell->InheritsData() ? ThisCell->InheritedCell.Pin().Get() : nullptr;
		if (Force || (InheritedCell && (InheritedCell->Row->GetTag() != Row->GetTag())))
		{
			Cells.Add(ThisCell);
			return true;
		}
		return false;
	}

private:
	FAffinityTableEditor* Editor;
	const FAffinityTableNode* Column;
	TArray<TWeakPtr<FAffinityTableEditor::Cell>>& Cells;
	bool Force;
};

/**
 * Performs some sync operations over our tag tree:
 *	- Finds stale nodes no longer contained in the asset
 *	- Syncs some properties for nodes still valid
 */
class FStaleNodeWalker : public IAffinityTableNodeWalker
{
public:
	FStaleNodeWalker(TArray<TWeakPtr<FAffinityTableNode>>& InResults,
		FAffinityTableNode::IndexGenerator InIndexCallback,
		FAffinityTableNode::NewNodeCallback InUpdateCallback,
		TWeakPtr<FAffinityTableNode> StartNode)
		: IndexCallback(InIndexCallback)
		, UpdateCallback(InUpdateCallback)
		, Results(InResults)
	{
		Walk(StartNode);
	}

	virtual bool Visit(TWeakPtr<FAffinityTableNode>& InNode) override
	{
		FAffinityTableNode::NodeSharedPtr Ptr = InNode.Pin();
		if (Ptr->GetTag().IsValid())
		{
			// Stale node
			if (IndexCallback(Ptr->GetTag()) == UAffinityTable::InvalidIndex)
			{
				Results.Add(Ptr);
				return false;
			}

			// Valid node. Sync properties
			UpdateCallback(InNode);
		}
		return true;
	}

	virtual ~FStaleNodeWalker() override = default;

private:
	FAffinityTableNode::IndexGenerator IndexCallback;
	FAffinityTableNode::NewNodeCallback UpdateCallback;
	TArray<TWeakPtr<FAffinityTableNode>>& Results;
};

//
// UI Classes
//////////////////////////////////////////////////////////////////////////////////////

/**
 * Manages the behavior and appearance of column headers
 */
class SAffinityTableColumnHeader : public SAffinityTableHeader
{
protected:
	virtual void OnConstruct() override
	{
		FAffinityTableNode::NodeSharedPtr Ptr = Node.Pin();

		// Gather colors from our parents
		TArray<FLinearColor> Colors;
		GatherUpstreamColors(Colors);

		const int32 HandleCount = Colors.Num();
		const float CellMargin = FAffinityTableStyles::ColCellMargin;

		// Vertical box with colors, plus that of our parents
		const TSharedRef<SVerticalBox> HandleHolder = SNew(SVerticalBox);
		while (Colors.Num())
		{
			HandleHolder->AddSlot()
				.MaxHeight(FAffinityTableStyles::ColHeaderColorHeight)
					[SNew(SColorBlock).Color(Colors.Pop())];
		}

		// An overlay to contain all of our components
		const TSharedRef<SOverlay> ColOverlay = SNew(SOverlay);

		// Vertical bar if we are open
		if (!Ptr->IsCollapsed() && Ptr->HasChildren())
		{
			ColOverlay->AddSlot()
				[SNew(SHorizontalBox) + SHorizontalBox::Slot()
											.MaxWidth(FAffinityTableStyles::ColHeaderColorHeight)
												[SNew(SColorBlock).Color(Ptr->GetColor())]];
		}

		// Top-level handles
		ColOverlay->AddSlot()[HandleHolder];

		// Description
		ColOverlay->AddSlot()[SNew(SBox)
								  .HAlign(HAlign_Fill)
								  .VAlign(VAlign_Fill)[SNew(STextBlock)
														   .Margin(FMargin(CellMargin * 2, CellMargin + FAffinityTableStyles::ColHeaderColorHeight * HandleCount, CellMargin, CellMargin))
														   .Text(MakeHeaderName())]];

		ChildSlot[ColOverlay];
	}

	virtual void OnDeleteHeader() override
	{
		const FText WarningMessage(LOCTEXT("AT_Warning_DeleteCol", "Are you sure you want to delete this column?"));
		if (EAppReturnType::Yes == FMessageDialog::Open(EAppMsgType::YesNo, WarningMessage))
		{
			const FScopedTransaction Transaction(LOCTEXT("AT_Transaction_DeleteColumn", "Delete Column"));
			const TSharedPtr<FAffinityTableEditor> EditorPtr = Editor.Pin();
			EditorPtr->GetTableBeingEdited()->Modify();
			EditorPtr->DeleteColumn(Node);
		}
	}

	virtual void OnSetColor() override
	{
		Editor.Pin()->PickColorForHeader(this, false);
	}
};

/**
 * Hosts the AffinityTable list view, and intercepts some necessary mouse and keyboard messages.
 */
class STableListView : public SListView<FAffinityTableNode::NodeSharedPtr>
{
public:
	void Construct(const FArguments& Args, FAffinityTableEditor* InEditor)
	{
		Editor = InEditor;
		SListView<FAffinityTableNode::NodeSharedPtr>::Construct(Args);
	}

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override
	{
		return Editor->OnSelectedCellKeyDown(InKeyEvent);
	}

	virtual FReply OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override
	{
		return Editor->OnSelectedCellKeyUp(InKeyEvent);
	}

	virtual void OnFocusLost(const FFocusEvent& InFocusEvent) override
	{
		return Editor->OnTableFocusLost();
	};

private:
	FAffinityTableEditor* Editor{ nullptr };
};

//////////////////////////////////////////////////////////////////////////////////////

const FAffinityTablePreferences* UAffinityTableEditorPreferences::GetPreferencesForTable(const FName& TableName) const
{
	if (const FAffinityTablePreferences* Preferences = TablePreferences.Find(TableName))
	{
		return Preferences;
	}
	return nullptr;
}

void UAffinityTableEditorPreferences::SetPreferencesForTable(const FName& TableName, const FAffinityTablePreferences& Preferences)
{
	TablePreferences.Add(TableName, Preferences);
}

FAffinityTableEditor::FAffinityTableEditor()
	: TableBeingEdited(nullptr)
	, CellSelectionType(ECellSelectionType::Single)
	, SelectedTagIsRow(false)
{
	GEditor->RegisterForUndo(this);

	// New/restored preferences
	EditorPreferences = TStrongObjectPtr(NewObject<UAffinityTableEditorPreferences>());
	EditorPreferences->LoadConfig();
}

FAffinityTableEditor::~FAffinityTableEditor()
{
	SaveTablePreferences();

	check(EditorPreferences);
	EditorPreferences->SaveConfig();

	GEditor->UnregisterForUndo(this);
}

FName FAffinityTableEditor::GetToolkitFName() const
{
	return FName("AffinityTableEditor");
}

FText FAffinityTableEditor::GetBaseToolkitName() const
{
	return LOCTEXT("AT_AppLabel", "Affinity Table Editor");
}

FText FAffinityTableEditor::GetToolkitToolTipText() const
{
	return LOCTEXT("AT_ToolTip", "Affinity Table Editor");
}

FString FAffinityTableEditor::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("AT_WorldCentricTabPrefix", "AffinityTable").ToString();
	return LOCTEXT("AT_WorldCentricTabPrefix", "AffinityTable").ToString();
}

FLinearColor FAffinityTableEditor::GetWorldCentricTabColorScale() const
{
	return FLinearColor(1.0f, 0.0f, 0.2f, 0.5f);
}

void FAffinityTableEditor::RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("AT_WorkspaceMenu", "Affinity Table Editor"));
	const auto WorkspaceMenuCategoryRef = WorkspaceMenuCategory.ToSharedRef();

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(FAffinityTableEditorTabs::TableViewportID, FOnSpawnTab::CreateSP(this, &FAffinityTableEditor::SpawnTableWidgetTab))
		.SetDisplayName(LOCTEXT("AT_PropertiesTabViewport", "Table"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports"));

	InTabManager->RegisterTabSpawner(FAffinityTableEditorTabs::CellPropertiesID, FOnSpawnTab::CreateSP(this, &FAffinityTableEditor::SpawnCellPropertiesTab))
		.SetDisplayName(LOCTEXT("AT_PropertiesTabCell", "Cell Properties"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));

	InTabManager->RegisterTabSpawner(FAffinityTableEditorTabs::TablePropertiesID, FOnSpawnTab::CreateSP(this, &FAffinityTableEditor::SpawnTablePropertiesTab))
		.SetDisplayName(LOCTEXT("AT_PropertiesTabTable", "Table Properties"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));
}

void FAffinityTableEditor::UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);

	InTabManager->UnregisterTabSpawner(FAffinityTableEditorTabs::TableViewportID);
	InTabManager->UnregisterTabSpawner(FAffinityTableEditorTabs::CellPropertiesID);
	InTabManager->UnregisterTabSpawner(FAffinityTableEditorTabs::TablePropertiesID);

	DetailsView.Reset();
	TableDetailsView.Reset();
	TableViewport.Reset();
}

void FAffinityTableEditor::PostUndo(bool bSuccess)
{
	if (bSuccess)
	{
		ResyncAsset();
	}
}

void FAffinityTableEditor::PostRedo(bool bSuccess)	  // -V524 (Equal to PostUndo, but responds to different events)
{
	if (bSuccess)
	{
		ResyncAsset();
	}
}

void FAffinityTableEditor::InsertTag(const FGameplayTag& InTag, TWeakPtr<FAffinityTableNode> InNode, FAffinityTableNode::IndexGenerator OnIndexForTag, FAffinityTableNode::NewNodeCallback OnNewNode)
{
	TArray<FGameplayTag> Tags;
	InTag.GetGameplayTagParents().GetGameplayTagArray(Tags);
	InNode.Pin()->Insert(Tags, OnIndexForTag, OnNewNode);
}

void FAffinityTableEditor::InitAffinityTableEditor(const EToolkitMode::Type Mode, const TSharedPtr<class IToolkitHost>& InitToolkitHost, UAffinityTable* Table)
{
	// Register this asset and make our editor reflect its contained data
	TableBeingEdited = Table;

	RowRoot = MakeShared<FAffinityTableNode>(FGameplayTag());
	ColumnRoot = MakeShared<FAffinityTableNode>(FGameplayTag());

	CellTable.Empty();
	ColumnNodes.Empty();
	PageViews.Empty();

	ActivePageView.Reset();
	AvailableRows.Empty();
	SelectedCells.Empty();
	ReferenceCell.Reset();

	// Create pages for existing structures before we load the table. This enables the system
	// to find the right inheritance links. Once cells are created, the 'Handle initial page update' below
	// makes sure to do an initial 'page selection' change.
	UpdatePageSet();

	// Create a hierarchical tree with our asset tags. Populate a table representation
	for (const TPair<FGameplayTag, UAffinityTable::TagIndex>& Row : TableBeingEdited->GetRows())
	{
		InsertRow(Row.Key);
	}
	for (const TPair<FGameplayTag, UAffinityTable::TagIndex>& Column : TableBeingEdited->GetColumns())
	{
		InsertColumn(Column.Key);
	}

	// Load settings for this table
	LoadTablePreferences();

	// Handle initial page update
	if (PageViews.Num())
	{
		ActivePageView.Reset();
		HandlePageComboChanged(PageViews[0], ESelectInfo::Direct);
	}

	// Register asset callback hooks
	TableBeingEdited->SetStructureChangeCallback([this](uint32 ChangeType) { this->UpdatePageSet(); });
	// Create the layout of our custom asset editor
	const TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Standalone_AffinityTableEditor_Layout_v1")
																		 ->AddArea(
																			 // Toolbar area
																			 FTabManager::NewPrimaryArea()
																				 ->SetOrientation(Orient_Vertical)
																				 // ->Split(
																				 //  FTabManager::NewStack()
																				 // 	 ->SetSizeCoefficient(0.1f)
																				 // 	 ->SetHideTabWell(true)
																				 // 	 ->AddTab(GetToolbarTabId(), ETabState::OpenedTab))
																				 // Editor content area
																				 ->Split(
																					 // Split the tab and pass the tab id to the tab spawner
																					 FTabManager::NewSplitter()
																						 ->SetOrientation(Orient_Horizontal)
																						 ->SetSizeCoefficient(0.9f)
																						 ->Split(
																							 FTabManager::NewSplitter()
																								 ->SetOrientation(Orient_Horizontal)
																								 ->SetSizeCoefficient(0.9f)
																								 ->Split(
																									 // Viewport area
																									 FTabManager::NewStack()
																										 ->SetSizeCoefficient(0.8f)
																										 ->SetHideTabWell(true)
																										 ->AddTab(FAffinityTableEditorTabs::TableViewportID, ETabState::OpenedTab))
																								 ->Split(
																									 FTabManager::NewSplitter()
																										 ->SetOrientation(Orient_Vertical)
																										 ->SetSizeCoefficient(0.2f)
																										 ->Split(
																											 FTabManager::NewStack()
																												 ->SetSizeCoefficient(0.25f)
																												 ->SetHideTabWell(true)
																												 ->AddTab(FAffinityTableEditorTabs::TablePropertiesID, ETabState::OpenedTab))
																										 ->Split(
																											 FTabManager::NewStack()
																												 ->SetSizeCoefficient(0.75f)
																												 ->AddTab(FAffinityTableEditorTabs::CellPropertiesID, ETabState::OpenedTab))))));

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs TableDetailsViewArgs;
	TableDetailsViewArgs.bAllowSearch = false;
	TableDetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	const FStructureDetailsViewArgs StructureDetailsViewArgs;

	DetailsView = PropertyEditorModule.CreateStructureDetailView(TableDetailsViewArgs, StructureDetailsViewArgs, nullptr);
	DetailsView->GetOnFinishedChangingPropertiesDelegate().AddSP(this, &FAffinityTableEditor::OnCellPropertyValueChanged);

	TableDetailsView = PropertyEditorModule.CreateDetailView(TableDetailsViewArgs);
	TableDetailsView->SetObject((UObject*) TableBeingEdited);

	ExtendToolbar();

	// Initialize our custom asset editor
	FAssetEditorToolkit::InitAssetEditor(
		Mode,
		InitToolkitHost,
		FAffinityTableEditorModule::AffinityTableEditorAppIdentifier,
		StandaloneDefaultLayout,
		true,
		true,
		(UObject*) Table);

	// After loading, check if there were any errors
	if (Table->HasLoadingErrors())
	{
		const FText Message(LOCTEXT("AT_Loading_Errors",
			"This table contains one or more important data errors or warnings, and its integrity is compromised. Some rows and columns could now be wrong or missing. "
			"Please consult the logs to review the problems and its possible solutions"));
		FMessageDialog::Open(EAppMsgType::Ok, Message);
	}
}

void FAffinityTableEditor::ExtendToolbar()
{
	const TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);
	ToolbarExtender->AddToolBarExtension(
		"Asset",
		EExtensionHook::After,
		GetToolkitCommands(),
		FToolBarExtensionDelegate::CreateSP(this, &FAffinityTableEditor::FillToolbar));

	AddToolbarExtender(ToolbarExtender);
}

void FAffinityTableEditor::FillToolbar(FToolBarBuilder& ToolbarBuilder)
{
	check(TableBeingEdited);
	ToolbarBuilder.BeginSection("AffinityTableEditorCommands");
	{
		// You must have columns and a valid page before adding rows.
		ToolbarBuilder.AddToolBarButton(
			FUIAction(
				FExecuteAction::CreateLambda([this]() {
					DisplayGameTagPicker(true);
				}),
				FCanExecuteAction::CreateLambda([this]() {
					return TableBeingEdited && TableBeingEdited->GetColumns().Num() > 0 && ActivePageView.IsValid();
				})),
			NAME_None,
			LOCTEXT("AT_AddRow_Text", "Add Row"),
			LOCTEXT("AT_AddRow_Tooltip", "Add a new row to the grid"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "DataTableEditor.Add"));

		// You must have a valid page before adding columns
		ToolbarBuilder.AddToolBarButton(
			FUIAction(
				FExecuteAction::CreateLambda([this]() {
					DisplayGameTagPicker(false);
				}),
				FCanExecuteAction::CreateLambda([this]() {
					return TableBeingEdited && ActivePageView.IsValid();
				})),
			NAME_None,
			LOCTEXT("AT_AddColumn_Text", "Add Column"),
			LOCTEXT("AT_AddColumn_Tooltip", "Add a new column to the grid"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "DataTableEditor.Add"));

		ToolbarBuilder.AddToolBarButton(
			FUIAction(FExecuteAction::CreateLambda([this]() { ResetTableInheritance(); })),
			NAME_None,
			LOCTEXT("AT_ResetInheritance_Text", "Reset Inheritance"),
			LOCTEXT("AT_ResetInheritance_Tooltip", "(Debug) Resets inheritance data"),
			FSlateIcon());
	}
	ToolbarBuilder.EndSection();
}

TSharedRef<ITableRow> FAffinityTableEditor::OnGenerateRow(TSharedPtr<FGameplayTagNode> InItem, const TSharedRef<STableViewBase>& OwnerTable)	// -V813 (cannot change UE-defined signature)
{
	const FText TooltipText;
	return SNew(STableRow<TSharedPtr<FGameplayTagNode>>, OwnerTable)
		.Style(FAppStyle::Get(), "GameplayTagTreeView")
			[SNew(SHorizontalBox)

				// Normal Tag Display (management mode only)
				+ SHorizontalBox::Slot()
					  .FillWidth(1.0f)
					  .HAlign(HAlign_Left)
						  [SNew(STextBlock)
								  .ToolTip(FSlateApplication::Get().MakeToolTip(TooltipText))
								  .Text(FText::FromName(InItem->GetSimpleTagName()))
								  .ColorAndOpacity(FLinearColor::White)
								  .Visibility(EVisibility::Visible)]];
}

void FAffinityTableEditor::OnGetChildren(TSharedPtr<FGameplayTagNode> InItem, TArray<TSharedPtr<FGameplayTagNode>>& OutChildren)	// -V813 (cannot change UE-defined signature)
{
	// OutChildren gets the filtered version of its children here
	for (TSharedPtr<FGameplayTagNode>& ThisNode : InItem->GetChildTagNodes())
	{
		if (FilterTag(ThisNode))
		{
			OutChildren.Add(ThisNode);
		}
	}
}

void FAffinityTableEditor::InsertRow(const FGameplayTag& Tag)
{
	check(TableBeingEdited);

	TArray<TSharedRef<Cell>> NewCells;
	InsertTag(
		Tag, RowRoot,
		[this](const FGameplayTag& Tag) { return TableBeingEdited->GetRowIndex(Tag); },
		[this, &NewCells](TWeakPtr<FAffinityTableNode> NewNode) {
			FAffinityTableNode* NewNodePtr = NewNode.Pin().Get();

			// New tags appear collapsed
			NewNodePtr->SetCollapsed(false);

			// Add a row of cells to our grid
			CellTable.Add(NewNodePtr);
			FLambdaWalker AddColumnsWalker([this, NewNodePtr, &NewCells](TWeakPtr<FAffinityTableNode> Column) {
				FAffinityTableNode* ColumnPtr = Column.Pin().Get();

				const TSharedRef<Cell> NewCell = MakeShareable(new Cell());
				NewCell->TableCell = UAffinityTable::Cell{ NewNodePtr->GetTagIndex(), ColumnPtr->GetTagIndex() };
				NewCell->Row = NewNodePtr;
				NewCell->Column = ColumnPtr;

				NewCells.Add(NewCell);

				CellTable[NewNodePtr].Add(ColumnPtr, NewCell);	  // caching the de-referenced row leads to side-effects here.
				return true;
			});
			AddColumnsWalker.WalkChildren(ColumnRoot);

			// Register this color in our asset
			if (!TableBeingEdited->TryGetTagColor(NewNodePtr->GetTag(), NewNodePtr->GetColorRef(), true))
			{
				TableBeingEdited->SetTagColor(NewNodePtr->GetTag(), NewNodePtr->GetColor(), true);
			}
		});

	// Now that all dependents have been added, link them to their inheriting cells
	TArray<TWeakPtr<Cell>> CellsToUpdate;
	CellsToUpdate.Reserve(NewCells.Num());
	for (TSharedRef<Cell>& ThisCell : NewCells)
	{
		AssignInheritance(ThisCell, false, true);
		CellsToUpdate.Add(ThisCell);
	}
	// Finally, refresh changes
	UpdateCells(CellDataInheritance | CellDescription, &CellsToUpdate);
}

void FAffinityTableEditor::InsertColumn(const FGameplayTag& Tag)
{
	check(TableBeingEdited);

	TArray<TSharedRef<Cell>> NewCells;
	InsertTag(
		Tag, ColumnRoot,
		[this](const FGameplayTag& Tag) { return TableBeingEdited->GetColumnIndex(Tag); },
		[this, &NewCells](TWeakPtr<FAffinityTableNode> NewNode) {
			FAffinityTableNode* NewNodePtr = NewNode.Pin().Get();
			NewNodePtr->SetCollapsed(false);

			// Add this new column to all of our existing rows.
			for (TPair<FAffinityTableNode*, CellMap>& Row : CellTable)
			{
				TSharedRef<Cell> NewCell = MakeShareable(new Cell());
				NewCell->TableCell = UAffinityTable::Cell{ Row.Key->GetTagIndex(), NewNodePtr->GetTagIndex() };
				NewCell->Row = Row.Key;
				NewCell->Column = NewNodePtr;

				NewCells.Add(NewCell);
				Row.Value.Add(NewNodePtr, NewCell);
			}

			// Columns must cache their indexes for quicker lookups when re-generating our table.
			const FName Name = NewNodePtr->GetTag().GetTagName();
			if (!this->ColumnNodes.Contains(Name))
			{
				this->ColumnNodes.Add(Name, NewNode);
			}
			else
			{
				this->ColumnNodes[Name] = NewNode;
			}

			// Register this colour in our asset
			if (!TableBeingEdited->TryGetTagColor(NewNodePtr->GetTag(), NewNodePtr->GetColorRef(), false))
			{
				TableBeingEdited->SetTagColor(NewNodePtr->GetTag(), NewNodePtr->GetColor(), false);
			}
		});

	// Now that all dependents have been added, link them to their inheriting cells
	TArray<TWeakPtr<Cell>> CellsToUpdate;
	CellsToUpdate.Reserve(NewCells.Num());
	for (TSharedRef<Cell>& ThisCell : NewCells)
	{
		AssignInheritance(ThisCell, true, true);
		CellsToUpdate.Add(ThisCell);
	}
	// Finally, refresh changes
	UpdateCells(CellDataInheritance | CellDescription, &CellsToUpdate);
}

FReply FAffinityTableEditor::OnAddTag()
{
	check(AddTagWindow.IsValid());
	check(TableBeingEdited);

	if ((SelectedTagIsRow ? TableBeingEdited->GetRowIndex(SelectedTag) : TableBeingEdited->GetColumnIndex(SelectedTag)) != UAffinityTable::InvalidIndex)
	{
		const FText Message(LOCTEXT("AT_Warning_Existing", "The tag you selected already exists on the table, please pick a different one"));
		FMessageDialog::Open(EAppMsgType::Ok, Message);
		return FReply::Handled();
	}

	if (SelectedTagIsRow)
	{
		const FScopedTransaction Transaction(LOCTEXT("AT_Transaction_AddRow", "Add Row"));
		TableBeingEdited->Modify();
		TableBeingEdited->AddRow(SelectedTag);
		InsertRow(SelectedTag);
	}
	else
	{
		const FScopedTransaction Transaction(LOCTEXT("AT_Transaction_AddColumn", "Add Column"));
		TableBeingEdited->Modify();
		TableBeingEdited->AddColumn(SelectedTag);
		InsertColumn(SelectedTag);
	}

	// Adding successful. Close this window
	AddTagWindow.Pin()->RequestDestroyWindow();

	// Refresh our window
	RefreshTable();
	return FReply::Handled();
}

void FAffinityTableEditor::OnTagSelectionChanged(TSharedPtr<FGameplayTagNode> InTag, ESelectInfo::Type SelectInfo)	  // -V813 (cannot change UE-defined signature)
{
	if (InTag.IsValid())
	{
		SelectedTag = InTag->GetCompleteTag();
	}
}

void FAffinityTableEditor::DisplayGameTagPicker(bool PickRow)
{
	SelectedTagIsRow = PickRow;
	const TSharedRef<SWindow> NewWindow = SNew(SWindow)
											  .Title(LOCTEXT("AT_GametagPicker_Title", "Select a tag"))
											  .SizingRule(ESizingRule::UserSized)
											  .ClientSize(FVector2D(600, 430))
											  .SupportsMaximize(true)
											  .SupportsMinimize(false);

	AddTagWindow = NewWindow;

	if (!TagTreeWidget.IsValid())
	{
		// todo: use Lambda functions to simplify our class
		TagTreeWidget = SNew(STreeView<TSharedPtr<FGameplayTagNode>>)
							.TreeItemsSource(&TagItems)
							.OnGenerateRow(this, &FAffinityTableEditor::OnGenerateRow)
							.OnGetChildren(this, &FAffinityTableEditor::OnGetChildren)
							.OnSelectionChanged(this, &FAffinityTableEditor::OnTagSelectionChanged)
							.SelectionMode(ESelectionMode::Single);

		AddTagWidget = SNew(SBorder)
						   .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
							   [SNew(SVerticalBox)

								   // Tag filter
								   + SVerticalBox::Slot()
										 .AutoHeight()
										 .Padding(FMargin(0, 5, 0, 5))
											 [SNew(SSearchBox)
													 .HintText(LOCTEXT("AT_SearchBox_Hint", "Search Gameplay Tags"))
													 .OnTextChanged(this, &FAffinityTableEditor::OnFilterTagChanged)]

								   // Tag
								   + SVerticalBox::Slot()
										 .AutoHeight()
										 .MaxHeight(360)
										 .VAlign(VAlign_Top)
											 [SNew(SBorder)
													 .Padding(FMargin(4.f))
														 [TagTreeWidget.ToSharedRef()]]

								   //Buttons
								   + SVerticalBox::Slot()
										 .Padding(FMargin(0, 3, 0, 0))
										 .AutoHeight()
											 [SNew(SHorizontalBox) + SHorizontalBox::Slot()
																		 .HAlign(HAlign_Right)
																			 [SNew(SButton)
																					 .OnClicked_Lambda([this]() { return OnAddTag(); })
																					 .Text(FText::FromString(TEXT("Add this tag")))]]];
	}

	// Refresh our tags
	FString Filter;
	UGameplayTagsManager& Manager = UGameplayTagsManager::Get();
	Manager.GetFilteredGameplayRootTags(Filter, TagItems);
	FilterTagTree();

	NewWindow->SetContent(AddTagWidget.ToSharedRef());
	FSlateApplication::Get().AddModalWindow(NewWindow, nullptr);
}

void FAffinityTableEditor::OnFilterTagChanged(const FText& InFilterText)
{
	TagFilterString = InFilterText.ToString();
	FilterTagTree();
}

void FAffinityTableEditor::FilterTagTree()
{
	if (TagFilterString.IsEmpty())
	{
		for (TSharedPtr<FGameplayTagNode>& Tag : TagItems)
		{
			SetTagNodeItemExpansion(Tag, false);
		}
		TagTreeWidget->SetTreeItemsSource(&TagItems);
	}
	else
	{
		FilteredTagItems.Empty();
		for (TSharedPtr<FGameplayTagNode>& Tag : TagItems)
		{
			if (FilterTag(Tag))
			{
				FilteredTagItems.Add(Tag);
				SetTagNodeItemExpansion(Tag, true);
			}
			else
			{
				SetTagNodeItemExpansion(Tag, false);
			}
		}
		TagTreeWidget->SetTreeItemsSource(&FilteredTagItems);
	}
	TagTreeWidget->RequestTreeRefresh();
}

bool FAffinityTableEditor::FilterTag(const TSharedPtr<FGameplayTagNode>& InTag)
{
	if (InTag.IsValid())
	{
		// Show if this we're not filtering, or the item contains our filter text
		if (TagFilterString.IsEmpty() || InTag->GetCompleteTagString().Contains(TagFilterString))
		{
			return true;
		}
		// Show if any sibling has the filter text
		else
		{
			for (TSharedPtr<FGameplayTagNode>& ChildTag : InTag->GetChildTagNodes())
			{
				if (FilterTag(ChildTag))
				{
					return true;
				}
			}
		}
	}
	return false;
}

void FAffinityTableEditor::SetTagNodeItemExpansion(TSharedPtr<FGameplayTagNode>& InTag, bool ShouldExpand)
{
	if (InTag.IsValid() && TagTreeWidget.IsValid())
	{
		TagTreeWidget->SetItemExpansion(InTag, ShouldExpand);
		for (TSharedPtr<FGameplayTagNode>& ChildTag : InTag->GetChildTagNodes())
		{
			SetTagNodeItemExpansion(ChildTag, ShouldExpand);
		}
	}
}

void FAffinityTableEditor::SelectCell(TSharedPtr<SAffinityTableCell>& NewSelectedCell)
{
	// Helper function that empties all of our selected cells
	static auto EmptySelection = [](TArray<TWeakPtr<Cell>>& InCells) {
		for (TWeakPtr<Cell>& OldCell : InCells)
		{
			if (OldCell.IsValid())
			{
				if (SAffinityTableCell* UICell = OldCell.Pin()->GetUICell())
				{
					UICell->SetCellState(SAffinityTableCell::EState::Default);
				}
			}
		}
		InCells.Empty();
	};

	// Single mode de-selects any previous cells regardless of anything else
	if (CellSelectionType == ECellSelectionType::Single)
	{
		EmptySelection(SelectedCells);
	}

	// Having a valid cell unlocks other selection types
	if (NewSelectedCell.IsValid())
	{
		TWeakPtr<Cell> NewCell = NewSelectedCell->GetCell();
		check(NewCell.IsValid());

		// Areas collapse our selection to the cells in between our last selected cell and this one
		if (CellSelectionType == ECellSelectionType::Area && SelectedCells.Num())
		{
			TWeakPtr<Cell> Pivot = SelectedCells[SelectedCells.Num() - 1];

			EmptySelection(SelectedCells);
			GatherCellsBetween(Pivot, NewCell, SelectedCells);
			for (TWeakPtr<Cell>& ThisCell : SelectedCells)
			{
				if (SAffinityTableCell* UICell = ThisCell.Pin()->GetUICell())
				{
					UICell->SetCellState(SAffinityTableCell::EState::Selected);
				}
			}
		}
		// Otherwise, we are just adding or removing a single cell
		else
		{
			SAffinityTableCell* UICell = NewCell.Pin()->GetUICell();
			if (SelectedCells.Contains(NewCell))
			{
				if (UICell)
				{
					UICell->SetCellState(SAffinityTableCell::EState::Default);
				}
				SelectedCells.Remove(NewCell);
			}
			else
			{
				if (UICell)
				{
					UICell->SetCellState(SAffinityTableCell::EState::Selected);
				}
				SelectedCells.Add(NewCell);
			}
		}
	}
	DisplaySelectedCellStruct();
}

FReply FAffinityTableEditor::OnSelectedCellKeyDown(const FKeyEvent& InKeyEvent)
{
	FReply Reply = FReply::Unhandled();

	// Keys that do not require a cell
	FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::LeftControl || Key == EKeys::RightControl)
	{
		CellSelectionType = ECellSelectionType::Additive;
		Reply = FReply::Handled();
	}
	else if (Key == EKeys::LeftShift || Key == EKeys::RightShift)
	{
		CellSelectionType = ECellSelectionType::Area;
		Reply = FReply::Handled();
	}

	// Keys that require a primary selected cell
	TWeakPtr<Cell> SelectedCell = GetPrimarySelectedCell();
	const SAffinityTableCell* TableCell = SelectedCell.IsValid() ? SelectedCell.Pin()->GetUICell() : nullptr;
	if (TableCell)
	{
		EUINavigation Direction = FSlateApplicationBase::Get().GetNavigationDirectionFromKey(InKeyEvent);
		const FAffinityTableNode::NodeSharedPtr& Column = TableCell->GetColumn().Pin();
		const FAffinityTableNode::NodeSharedPtr& Row = TableCell->GetRow().Pin();

		// Move in the direction of (InRow, InColumn) and always handle movement, so we
		// do not lose 'focus' at the edges of our table.
		auto MoveInto = [this](const FAffinityTableNode* InRow, const FAffinityTableNode* InCol) {
			TWeakPtr<Cell> NavCell = GetCell(InRow, InCol);
			if (NavCell.IsValid() && NavCell.Pin()->UICell.IsValid())
			{
				TSharedPtr<SAffinityTableCell> NewCellPtr = NavCell.Pin()->UICell.Pin();
				SelectCell(NewCellPtr);
				return true;
			}
			return false;
		};

		// Gets neighboring nodes to the provided one. This is admittedly kinda hacky because the node tree is not
		// designed for this task, but it is also not part of our critical performance path.
		auto NeighboursOf = [](const FAffinityTableNode* InNode, const FAffinityTableNode*& OutPrev, const FAffinityTableNode*& OutNext) {
			if (InNode->GetParent())
			{
				const FAffinityTableNode* Parent = InNode->GetParent();
				const FGameplayTag& ChildTag = InNode->GetTag();
				for (FAffinityTableNode::NodeSharedPtrConstIt ChildIt = Parent->ChildIterator(); ChildIt; ++ChildIt)
				{
					if (ChildIt->Get()->MatchesExact(ChildTag))
					{
						if (++ChildIt)
						{
							OutNext = (*ChildIt).Get();
						}
						return;
					}
					OutPrev = (*ChildIt).Get();
				}
			}
		};

		// Supported directions.
		const FAffinityTableNode *PrevNode, *NextNode = nullptr;
		switch (Direction)
		{
			case EUINavigation::Up:
				NeighboursOf(Row.Get(), PrevNode, NextNode);
				if (!MoveInto(PrevNode, Column.Get()) && Row->HasValidParent())
				{
					MoveInto(Row->GetParent(), Column.Get());
				}
				Reply = FReply::Handled();
				break;

			case EUINavigation::Down:
				NeighboursOf(Row.Get(), PrevNode, NextNode);
				if (!MoveInto(NextNode, Column.Get()) && Row->HasChildren())
				{
					MoveInto((*Row->ChildIterator()).Get(), Column.Get());
				}
				Reply = FReply::Handled();
				break;

			case EUINavigation::Left:
				NeighboursOf(Column.Get(), PrevNode, NextNode);
				if (!MoveInto(Row.Get(), PrevNode) && Column->HasValidParent())
				{
					MoveInto(Row.Get(), Column->GetParent());
				}
				Reply = FReply::Handled();
				break;

			case EUINavigation::Right:
				NeighboursOf(Column.Get(), PrevNode, NextNode);
				if (!MoveInto(Row.Get(), NextNode) && Column->HasChildren())
				{
					MoveInto(Row.Get(), (*Column->ChildIterator()).Get());
				}
				Reply = FReply::Handled();
				break;
		}
	}
	return Reply;
}

FReply FAffinityTableEditor::OnSelectedCellKeyUp(const FKeyEvent& InKeyEvent)
{
	FReply Reply = FReply::Unhandled();
	FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::LeftControl || Key == EKeys::RightControl || Key == EKeys::LeftShift || Key == EKeys::RightShift)
	{
		CellSelectionType = ECellSelectionType::Single;
		Reply = FReply::Handled();
	}
	return Reply;
}

void FAffinityTableEditor::OnTableFocusLost()
{
	CellSelectionType = ECellSelectionType::Single;
}

TWeakPtr<FAffinityTableEditor::Cell> FAffinityTableEditor::GetCell(const FAffinityTableNode* Row, const FAffinityTableNode* Column)
{
	TWeakPtr<Cell> FoundCell;
	if (Row && Column && CellTable.Contains(Row))
	{
		CellMap& Columns = CellTable[Row];
		if (Columns.Contains(Column))
		{
			FoundCell = Columns[Column];
		}
	}
	return FoundCell;
}

void FAffinityTableEditor::DisplaySelectedCellStruct()
{
	check(TableBeingEdited);
	if (DetailsView.IsValid())
	{
		TWeakPtr<Cell> CellPtr = GetPrimarySelectedCell();
		if (ActivePageView.IsValid() && CellPtr.IsValid())
		{
			const UScriptStruct* PageStruct = ActivePageView->PageStruct;

			uint8* StructData = TableBeingEdited->GetCellData(CellPtr.Pin()->TableCell, PageStruct);
			check(StructData != nullptr);

			DetailsView->SetStructureData(MakeShareable(new FStructOnScope(PageStruct, StructData)));
			return;
		}
		DetailsView->SetStructureData(nullptr);
	}
}

FSlateColor FAffinityTableEditor::GetVisibilityBtnForeground() const
{
	static const FName InvertedForegroundName("InvertedForeground");
	static const FName DefaultForegroundName("DefaultForeground");

	return VisibilityComboButton->IsHovered() ? FAppStyle::GetSlateColor(InvertedForegroundName) : FAppStyle::GetSlateColor(DefaultForegroundName);
}

void FAffinityTableEditor::TogglePropertyVisibility(const FProperty* Property)
{
	check(ActivePageView.IsValid());
	check(ActivePageView->CellVisibility.Contains(Property));

	ActivePageView->CellVisibility[Property] = !ActivePageView->CellVisibility[Property];
	UpdateCells(CellVisibleFields | CellDescription);
}

void FAffinityTableEditor::UpdateCells(CellUpdateType UpdateTypes, TArray<TWeakPtr<Cell>>* CellsToUpdate /* = nullptr */)
{
	check(TableBeingEdited);
	if (ActivePageView.IsValid())
	{
		PageView* CurrentView = ActivePageView.Get();
		bool AssetNeedsSave = false;

		// Cache a list of visible properties
		if (UpdateTypes & CellVisibleFields)
		{
			CurrentView->VisibleProperties.Empty();
			for (const TPair<const FProperty*, bool>& Item : CurrentView->CellVisibility)
			{
				if (Item.Value)
				{
					CurrentView->VisibleProperties.Add(Item.Key);
				}
			}
		}

		// To avoid testing for operations on each cell, make a mini-command buffer.
		//////////////////////////////////////////////////////////////////////////

		using CellUpdateOp = std::function<void(Cell*, TSharedPtr<Cell>&)>;
		TArray<CellUpdateOp> UpdateOps;

		// Refresh the indexes on this cell
		if (UpdateTypes & CellAssetIndexes)
		{
			UpdateOps.Add([this](Cell* ThisCell, TSharedPtr<Cell>& ThisCellPtr) {
				check(ThisCell->Row && ThisCell->Column);
				ThisCell->TableCell = UAffinityTable::Cell{
					TableBeingEdited->GetRowIndex(ThisCell->Row->GetTag()),
					TableBeingEdited->GetColumnIndex(ThisCell->Column->GetTag())
				};
			});
		}

		// Enact cell inheritance
		if (UpdateTypes & CellInheritance)
		{
			UpdateOps.Add([this](Cell* ThisCell, TSharedPtr<Cell>& ThisCellPtr) {
				AssignInheritance(ThisCellPtr, false, true);
			});
		}

		// Enact data inheritance
		if (UpdateTypes & CellDataInheritance)
		{
			UpdateOps.Add([this, CurrentView, &AssetNeedsSave](Cell* ThisCell, TSharedPtr<Cell>& ThisCellPtr) {
				if (ThisCell->InheritsData() &&
					!TableBeingEdited->AreCellsIdentical(CurrentView->PageStruct, ThisCell->InheritedCell.Pin()->TableCell, ThisCell->TableCell))
				{
					const uint8* DataFrom = TableBeingEdited->GetCellData(ThisCell->InheritedCell.Pin()->TableCell, CurrentView->PageStruct);
					uint8* DataTo = TableBeingEdited->GetCellData(ThisCell->TableCell, CurrentView->PageStruct);
					check(DataFrom && DataTo);

					CurrentView->PageStruct->CopyScriptStruct(DataTo, DataFrom);
					AssetNeedsSave = true;
				}
			});
		}

		// Refresh data description
		if (UpdateTypes & CellDescription)
		{
			UpdateOps.Add([CurrentView](Cell* ThisCell, TSharedPtr<Cell>& ThisCellPtr) {
				if (ThisCell->UICell.IsValid())
				{
					ThisCell->UICell.Pin()->UpdateDescription(CurrentView);
				}
			});
		}

		// Enact the command buffer on the requested cells
		//////////////////////////////////////////////////////////////////////////

		// Only refresh the provided cells
		if (CellsToUpdate)
		{
			for (TWeakPtr<Cell>& ThisCell : *CellsToUpdate)
			{
				check(ThisCell.IsValid());
				TSharedPtr<Cell> ThisCellShared = ThisCell.Pin();
				Cell* ThisCellPtr = ThisCellShared.Get();
				for (CellUpdateOp& Op : UpdateOps)
				{
					Op(ThisCellPtr, ThisCellShared);
				}
			}
		}
		// Refresh the whole table. HOLY RECURSION HELL, BATMAN!
		else
		{
			for (TPair<FAffinityTableNode*, CellMap>& Row : CellTable)
			{
				for (TPair<FAffinityTableNode*, TSharedRef<Cell>>& Column : Row.Value)
				{
					Cell& ThisCellPtr = Column.Value.Get();
					TSharedPtr<Cell> ThisCellShared = Column.Value;
					for (CellUpdateOp& Op : UpdateOps)
					{
						Op(&ThisCellPtr, ThisCellShared);
					}
				}
			}
		}

		// Mark the asset as dirty if any of the above operations require a save
		if (AssetNeedsSave)
		{
			GetTableBeingEdited()->MarkPackageDirty();
		}
	}
}

void FAffinityTableEditor::OnCellPropertyValueChanged(const FPropertyChangedEvent& PropertyChangedEvent)
{
	TWeakPtr<Cell> CellRef = GetPrimarySelectedCell();
	check(CellRef.IsValid());

	OnCellValueChanged(CellRef);
}

void FAffinityTableEditor::OnCellValueChanged(TWeakPtr<Cell>& UpdatedCell)
{
	check(UpdatedCell.IsValid());
	TSharedPtr<Cell> UpdatedCellRef = UpdatedCell.Pin();

	// No matter what we do, this is now a modified document
	TableBeingEdited->Modify();

	// If this cell is inheriting data, mark it independent and update the inheritance chain, otherwise
	// update and make sure the data makes it to the inherited cells
	if (UpdatedCellRef->InheritsData())
	{
		UnlinkCell(UpdatedCell);
		PropagateInheritance(UpdatedCell);
	}
	else
	{
		if (UpdatedCellRef->UICell.IsValid() && ActivePageView.IsValid())
		{
			UpdatedCellRef->UICell.Pin()->UpdateDescription(ActivePageView.Get());
		}

		TArray<TWeakPtr<Cell>> InheritingCells;
		GatherInheritedCells(UpdatedCell, InheritingCells);
		UpdateCells(CellDataInheritance | CellDescription, &InheritingCells);
	}
}

bool FAffinityTableEditor::IsPropertyVisible(const FProperty* Property) const
{
	check(ActivePageView.IsValid());
	check(ActivePageView->CellVisibility.Contains(Property));

	return ActivePageView->CellVisibility[Property];
}

TSharedPtr<FAffinityTableEditor::PageView> FAffinityTableEditor::NewPageView(const UScriptStruct* ScriptStruct)
{
	check(ScriptStruct != nullptr);

	TSharedPtr<PageView> NewPage = MakeShareable(new PageView);
	NewPage->PageStruct = ScriptStruct;
	NewPage->DisplayRowInheritance = true;
	NewPage->DisplayTaxonomyColor = true;

	for (TFieldIterator<FProperty> It(ScriptStruct); It; ++It)
	{
		FProperty* Property = *It;
		if (Property != nullptr)
		{
			NewPage->CellVisibility.Add(Property, false);
		}
	}
	return NewPage;
}

void FAffinityTableEditor::LockCell(TWeakPtr<Cell>& InCell)
{
	check(InCell.IsValid());

	if (InCell.Pin()->InheritsData())
	{
		UnlinkCell(InCell);
		PropagateInheritance(InCell);
	}
}

TWeakPtr<FAffinityTableEditor::Cell> FAffinityTableEditor::GetPrimarySelectedCell()
{
	return SelectedCells.Num() == 1 && SelectedCells[0].IsValid() ? SelectedCells[0] : TWeakPtr<Cell>();
}

void FAffinityTableEditor::CopyCellData(TWeakPtr<Cell>& SourceCell)
{
	auto SetRefCellState = [this](SAffinityTableCell::EState State) {
		if (ReferenceCell.IsValid())
		{
			TWeakPtr<SAffinityTableCell> UICell = ReferenceCell.Pin()->UICell;
			if (UICell.IsValid())
			{
				TSharedPtr<SAffinityTableCell> UICellRef = UICell.Pin();
				UICellRef->SetDefaultState(State);
				UICellRef->SetCellState(State);
			}
		}
	};

	// Unmark any previous cell
	SetRefCellState(SAffinityTableCell::Default);

	// Acquire new.
	if (SourceCell.IsValid())
	{
		ReferenceCell = SourceCell;
		SetRefCellState(SAffinityTableCell::Referenced);
	}
}

void FAffinityTableEditor::PasteCellData(TArray<TWeakPtr<Cell>>& TargetCells, bool VisiblePropertiesOnly /*= false*/)
{
	check(ReferenceCell.IsValid());
	check(TableBeingEdited);

	if (ActivePageView.IsValid() && (!VisiblePropertiesOnly || ActivePageView->VisibleProperties.Num()))
	{
		const UScriptStruct* PageStruct = ActivePageView->PageStruct;
		TSharedPtr<Cell> SourceCellPtr = ReferenceCell.Pin();
		uint8* SourceData = TableBeingEdited->GetCellData(SourceCellPtr->TableCell, PageStruct);
		check(SourceData);

		// Clarity over performance. We are likely pasting a human-countable number of cells
		for (TWeakPtr<Cell>& TargetCell : TargetCells)
		{
			check(TargetCell.IsValid());
			TSharedPtr<Cell> TargetCellPtr = TargetCell.Pin();

			if (SourceCellPtr != TargetCellPtr)
			{
				uint8* DestData = TableBeingEdited->GetCellData(TargetCellPtr->TableCell, PageStruct);
				check(DestData);

				// Copy a partial dataset (most frequently)
				if (VisiblePropertiesOnly)
				{
					for (const FProperty* Property : ActivePageView->VisibleProperties)
					{
						Property->CopyCompleteValue(
							Property->ContainerPtrToValuePtr<void>(DestData),
							Property->ContainerPtrToValuePtr<void>(SourceData));
					}
					OnCellValueChanged(TargetCell);
				}
				// Copy the whole cell
				else
				{
					PageStruct->CopyScriptStruct(DestData, SourceData);
					OnCellValueChanged(TargetCell);
				}
			}
		}
	}
}

void FAffinityTableEditor::AcquireInheritance(TWeakPtr<Cell>& InCell, bool ColumnStream)
{
	check(InCell.IsValid());

	if (!InCell.Pin()->InheritsData())
	{
		// Gather our currently inherited cells
		TArray<TWeakPtr<Cell>> InheritedCells;
		GatherInheritedCells(InCell, InheritedCells);

		// Break our non-inherited status and acquire new parent
		AssignInheritance(InCell, ColumnStream);

		// Update our children
		for (TWeakPtr<Cell>& ChildCell : InheritedCells)
		{
			LinkCells(ChildCell, InCell.Pin()->InheritedCell);
		}

		// Refresh visuals and data for everyone, including ourselves
		InheritedCells.Add(InCell);
		UpdateCells(CellDataInheritance | CellDescription, &InheritedCells);
	}
}

void FAffinityTableEditor::PropagateInheritance(TWeakPtr<Cell>& InCell, bool Force)
{
	check(InCell.IsValid());

	TArray<TWeakPtr<Cell>> NewInheritedCells;
	GatherInheritedCells(InCell, NewInheritedCells, Force);
	for (TWeakPtr<Cell>& ChildCell : NewInheritedCells)
	{
		LinkCells(ChildCell, InCell);
	}

	// Update the values of the newly inherited cells, and ours
	NewInheritedCells.Add(InCell);
	UpdateCells(CellDataInheritance | CellDescription, &NewInheritedCells);
}

void FAffinityTableEditor::GatherInheritedCells(TWeakPtr<Cell>& InCell, TArray<TWeakPtr<Cell>>& OutCells, bool Force /* = false */)
{
	check(InCell.IsValid());

	Cell* ParentCell = InCell.Pin().Get();
	FAffinityTableNode* RowPtr = ParentCell->Row;
	FAffinityTableNode* ColumnPtr = ParentCell->Column;

	TWeakPtr<Cell> StartCell = GetCell(RowPtr, ColumnPtr);
	check(StartCell.IsValid());

	// This cell should have no inheritance
	if (!StartCell.Pin()->InheritsData())
	{
		// Walk the rows in this column
		FFillColumnDownWalker ThisColumn(this, ColumnPtr, OutCells, Force);
		ThisColumn.WalkChildren(RowPtr);

		// Walk our columns to the right
		FLambdaWalker RightColumns(nullptr, [this, ParentCell, RowPtr, &OutCells, Force](FAffinityTableNode* Column) {
			check(Column);

			TSharedPtr<Cell> ThisCell = GetCell(RowPtr, Column).Pin();
			check(ThisCell.IsValid());

			// This cell is fair game if it is open (inheriting) and either already ours, or NOT parented to another in the same column
			const Cell* InheritedCell = ThisCell->InheritsData() ? ThisCell->InheritedCell.Pin().Get() : nullptr;
			if ((InheritedCell && (InheritedCell == ParentCell || InheritedCell->Column->GetTag() != Column->GetTag())) || Force)
			{
				OutCells.Add(ThisCell);
				FFillColumnDownWalker FillColumnDown(this, Column, OutCells, Force);
				FillColumnDown.WalkChildren(RowPtr);
				return true;
			}
			return false;
		});
		RightColumns.WalkChildren(ColumnPtr);
	}
}

void FAffinityTableEditor::GatherCellsBetween(TWeakPtr<Cell>& CornerA, TWeakPtr<Cell>& CornerB, TArray<TWeakPtr<Cell>>& OutCells)
{
	check(CornerA.IsValid() && CornerB.IsValid());

	const TSharedPtr<Cell> ARef = CornerA.Pin();
	const TSharedPtr<Cell> BRef = CornerB.Pin();

	// Organize coordinates so we go from (left/top) to (right/bottom)
	FAffinityTableNode* Ax = ARef->Column;
	FAffinityTableNode* Ay = ARef->Row;
	FAffinityTableNode* Bx = BRef->Column;
	FAffinityTableNode* By = BRef->Row;

	if (Bx->GetTag() < Ax->GetTag())
	{
		Ax = Bx;
		Bx = ARef->Column;
	}

	if (By->GetTag() < Ay->GetTag())
	{
		Ay = By;
		By = ARef->Row;
	}

	// Gather a list with the current visible columns. This is very inefficient but the selection operation is
	// infrequent and does not justify caching this out. If/when we do, it will happen at RefreshTable
	TArray<FAffinityTableNode::NodeSharedPtr> AvailableColumns;
	FVisibleNodeWalker ColumnWalker(AvailableColumns);
	ColumnWalker.Walk(ColumnRoot);

	// Get selection boundaries
	const int32 Ayi = AvailableRows.IndexOfByPredicate([Ay](const FAffinityTableNode::NodeSharedPtr& ThisRow) { return ThisRow->GetTag() == Ay->GetTag(); });
	const int32 Ayf = AvailableRows.IndexOfByPredicate([By](const FAffinityTableNode::NodeSharedPtr& ThisRow) { return ThisRow->GetTag() == By->GetTag(); });
	const int32 Axi = AvailableColumns.IndexOfByPredicate([Ax](const FAffinityTableNode::NodeSharedPtr& ThisColumn) { return ThisColumn->GetTag() == Ax->GetTag(); });
	const int32 Axf = AvailableColumns.IndexOfByPredicate([Bx](const FAffinityTableNode::NodeSharedPtr& ThisColumn) { return ThisColumn->GetTag() == Bx->GetTag(); });

	if (Axf != INDEX_NONE && Ayf != INDEX_NONE && Axi <= Axf && Ayi <= Ayf)
	{
		for (int32 i = Axi; i <= Axf; ++i)
		{
			check(i < AvailableColumns.Num());
			const FAffinityTableNode* Column = AvailableColumns[i].Get();
			for (int32 j = Ayi; j <= Ayf; ++j)
			{
				check(j < AvailableRows.Num());
				const FAffinityTableNode* Row = AvailableRows[j].Get();

				TWeakPtr<Cell> OutCell = GetCell(Row, Column);
				check(OutCell.IsValid());

				OutCells.Add(OutCell);
			}
		}
	}
}

void FAffinityTableEditor::ResetTableInheritance()
{
	const FText WarningMessage(LOCTEXT("AT_Warning_Reset", "This will delete data in your table and cannot be undone. It is meant for debug purposes only! Proceed?"));
	if (EAppReturnType::Yes == FMessageDialog::Open(EAppMsgType::YesNo, WarningMessage))
	{
		// Each [top-level row, top-level column] is the topmost-lefmost node of a sub-table in our asset
		for (FAffinityTableNode::NodeSharedPtrIt RowIt = RowRoot->ChildIterator(); RowIt; ++RowIt)
		{
			for (FAffinityTableNode::NodeSharedPtrIt ColumnIt = ColumnRoot->ChildIterator(); ColumnIt; ++ColumnIt)
			{
				TWeakPtr<Cell> RootCell = GetCell((*RowIt).Get(), (*ColumnIt).Get());
				check(RootCell.IsValid());

				UnlinkCell(RootCell);
				if (RootCell.Pin()->UICell.IsValid())
				{
					PropagateInheritance(RootCell, true);
				}
			}
		}
	}
}

void FAffinityTableEditor::AssignInheritance(TWeakPtr<Cell> InCell, bool ColumnStream, bool RestoreFromAsset /* = false */)
{
	check(InCell.IsValid());
	check(TableBeingEdited);
	check(ActivePageView.IsValid());

	const TSharedPtr<Cell> InCellPtr = InCell.Pin();
	check(InCellPtr->Row && InCellPtr->Column);

	const FAffinityTableNode* ParentRow = nullptr;
	const FAffinityTableNode* ParentColumn = nullptr;

	// See if we already had a relationship on file for this cell
	if (RestoreFromAsset)
	{
		UAffinityTable::CellTags AssetInheritance;
		if (TableBeingEdited->TryGetInheritanceLink(ActivePageView->PageStruct, InCellPtr->AsCellTags(), AssetInheritance))
		{
			// If row and column point to invalid tags, it means we are unlinked
			if (!AssetInheritance.Row.IsValid() && !AssetInheritance.Column.IsValid())
			{
				// Because the asset link already exists, this is the only case where we set the node's parent directly.
				InCellPtr->InheritedCell.Reset();
				return;
			}

			FFindNodeWalker FindRow(AssetInheritance.Row);
			FindRow.Walk(RowRoot.Get());

			FFindNodeWalker FindColumn(AssetInheritance.Column);
			FindColumn.Walk(ColumnRoot.Get());

			check(FindRow.GetFoundNode() && FindColumn.GetFoundNode());

			const TWeakPtr<Cell> ParentCell = GetCell(FindRow.GetFoundNode(), FindColumn.GetFoundNode());
			if (ParentCell.IsValid())
			{
				// See note above for previous assignation if InheritedCell
				InCellPtr->InheritedCell = ParentCell;
				return;
			}

			// This is an error: it means we have a relationship but the AffinityTable editor hasn't loaded that cell
			UE_LOG(LogAffinityTableEditor, Warning, TEXT("Cell relationship not found in table for parent: [%s, %s]"), *AssetInheritance.Row.ToString(), *AssetInheritance.Column.ToString());
		}
	}

	// Rules:
	// If we have no ancestors, do not inherit. If the primary ancestor is invalid, inherit the secondary.
	// If the primary ancestor is valid:
	//		If the cell above is inheriting, copy its inheritance.
	//		If the cell above is not inheriting, inherit from it
	if (ColumnStream)
	{
		if (InCellPtr->Column->HasValidParent())
		{
			ParentRow = InCellPtr->Row;
			ParentColumn = InCellPtr->Column->GetParent();
		}
		else if (InCellPtr->Row->HasValidParent())
		{
			ParentRow = InCellPtr->Row->GetParent();
			ParentColumn = InCellPtr->Column;
		}
		else
		{
			UnlinkCell(InCell);
			return;
		}
	}
	else
	{
		if (InCellPtr->Row->HasValidParent())
		{
			ParentRow = InCellPtr->Row->GetParent();
			ParentColumn = InCellPtr->Column;
		}
		else if (InCellPtr->Column->HasValidParent())
		{
			ParentRow = InCellPtr->Row;
			ParentColumn = InCellPtr->Column->GetParent();
		}
		else
		{
			UnlinkCell(InCell);
			return;
		}
	}

	TWeakPtr<Cell> ParentCell = GetCell(ParentRow, ParentColumn);
	check(ParentCell.IsValid());

	TSharedPtr<Cell> ParentCellPtr = ParentCell.Pin();
	LinkCells(InCell, (ParentCellPtr->InheritsData() ? ParentCellPtr->InheritedCell : ParentCell));
}

void FAffinityTableEditor::LinkCells(TWeakPtr<Cell>& Child, TWeakPtr<Cell>& Parent)
{
	check(TableBeingEdited);
	check(Child.IsValid() && Parent.IsValid());
	check(ActivePageView.IsValid());

	Cell* ChildPtr = Child.Pin().Get();
	Cell* ParentPtr = Parent.Pin().Get();

	ChildPtr->InheritedCell = Parent;
	TableBeingEdited->SetInheritanceLink(ActivePageView->PageStruct, ChildPtr->AsCellTags(), ParentPtr->AsCellTags());
}

void FAffinityTableEditor::UnlinkCell(TWeakPtr<Cell>& InCell)
{
	check(TableBeingEdited);
	check(InCell.IsValid());
	check(ActivePageView.IsValid());

	Cell* InCellPtr = InCell.Pin().Get();

	InCellPtr->InheritedCell.Reset();
	TableBeingEdited->RemoveInheritanceLink(ActivePageView->PageStruct, InCellPtr->AsCellTags());
}

void FAffinityTableEditor::LoadTablePreferences() const
{
	check(EditorPreferences);
	if (IsValid(TableBeingEdited))
	{
		if (const FAffinityTablePreferences* Preferences =
				EditorPreferences->GetPreferencesForTable(TableBeingEdited->GetFName()))
		{
			const TSet<FName>* CollapsedNodes = nullptr;
			FLambdaWalker ApplyNodePreferences([&CollapsedNodes](TWeakPtr<FAffinityTableNode> Node) {
				check(CollapsedNodes);
				const auto& NodeRef = Node.Pin();
				if (CollapsedNodes->Contains(NodeRef->GetTag().GetTagName()))
				{
					NodeRef->SetCollapsed(true);
				}
				return true;
			});

			CollapsedNodes = &Preferences->CR;
			ApplyNodePreferences.Walk(RowRoot);

			CollapsedNodes = &Preferences->CC; // -V519 We re-assign CollapsedNodes so the lambda walker can re-use it.
			ApplyNodePreferences.Walk(ColumnRoot);
		}
	}
}

void FAffinityTableEditor::SaveTablePreferences() const
{
	check(EditorPreferences);

	if (IsValid(TableBeingEdited))
	{
		TSet<FName> CollapsedNodes;
		FLambdaWalker RecordCollapsedNodes([&CollapsedNodes](TWeakPtr<FAffinityTableNode> Node) {
			const auto& NodeRef = Node.Pin();
			if (NodeRef->IsCollapsed())
			{
				CollapsedNodes.Add(NodeRef->GetTag().GetTagName());
			}
			return true;
		});

		RecordCollapsedNodes.Walk(RowRoot);

		FAffinityTablePreferences Preferences;
		Preferences.CR = CollapsedNodes;

		CollapsedNodes.Empty();
		RecordCollapsedNodes.Walk(ColumnRoot);
		Preferences.CC = CollapsedNodes;

		EditorPreferences->SetPreferencesForTable(TableBeingEdited->GetFName(), Preferences);
	}
}

TSharedRef<SWidget> FAffinityTableEditor::GetVisibilityBtnContent()
{
	FMenuBuilder MenuBuilder(/* Should close after selection */ false, NULL);
	if (ActivePageView.IsValid())
	{
		// Visibility combo boxes for each property in our structure
		for (TPair<const FProperty*, bool>& It : ActivePageView->CellVisibility)
		{
			MenuBuilder.AddMenuEntry(
				It.Key->GetDisplayNameText(),
				LOCTEXT("AT_Visibility_Tooltip", "Displays or hides the value of this property"),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &FAffinityTableEditor::TogglePropertyVisibility, It.Key),
					FCanExecuteAction(),
					FIsActionChecked::CreateSP(this, &FAffinityTableEditor::IsPropertyVisible, It.Key)),
				NAME_None,
				EUserInterfaceActionType::ToggleButton);
		}

		MenuBuilder.AddMenuSeparator();

		MenuBuilder.AddMenuEntry(
			LOCTEXT("AT_RowInheritance", "Display Row Inheritance"),
			LOCTEXT("AT_RowInheritance_Tooltip", "Show row inheritance when rendering cell values"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this]() {
					if (ActivePageView.IsValid() && !ActivePageView->DisplayRowInheritance)
					{
						ActivePageView->DisplayRowInheritance = true;
						UpdateCells(CellDescription);
					}
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this]() { return ActivePageView.IsValid() && ActivePageView->DisplayRowInheritance; })),
			NAME_None,
			EUserInterfaceActionType::RadioButton);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("AT_Colheritance", "Display Column Inheritance"),
			LOCTEXT("AT_ColInheritance_Tooltip", "Show column inheritance when rendering cell values"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this]() {
					if (ActivePageView.IsValid() && ActivePageView->DisplayRowInheritance)
					{
						ActivePageView->DisplayRowInheritance = false;
						UpdateCells(CellDescription);
					}
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this]() { return ActivePageView.IsValid() && !ActivePageView->DisplayRowInheritance; })),
			NAME_None,
			EUserInterfaceActionType::RadioButton);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("AT_Visibility_Taxonomy", "Display Taxonomy"),
			LOCTEXT("AT_Visibility_Taxonomy_Tooltip", "Show a color based on the taxonomy tree of this tag"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this]() {
					if (ActivePageView.IsValid())
					{
						ActivePageView->DisplayTaxonomyColor = !ActivePageView->DisplayTaxonomyColor;
						UpdateCells(CellDescription);
					}
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this]() { return ActivePageView.IsValid() && ActivePageView->DisplayTaxonomyColor; })),
			NAME_None,
			EUserInterfaceActionType::ToggleButton);
	}
	return MenuBuilder.MakeWidget();
}

void FAffinityTableEditor::UpdatePageSet()
{
	if (TableBeingEdited)
	{
		TArray<TSharedPtr<PageView>> NewPageViews;

		// Current page
		const UScriptStruct* ActiveStruct = ActivePageView.IsValid() ? ActivePageView->PageStruct : nullptr;
		ActivePageView.Reset();

		// Add or re-order
		for (const UScriptStruct* ScriptStruct : TableBeingEdited->Structures)
		{
			if (ScriptStruct)
			{
				TSharedPtr<PageView>* PrevView = PageViews.FindByPredicate([ScriptStruct](TSharedPtr<PageView>& Page) { return Page.IsValid() && Page->PageStruct == ScriptStruct; });
				if (PrevView)
				{
					NewPageViews.Add(*PrevView);
					*PrevView = TSharedPtr<PageView>();
				}
				else
				{
					NewPageViews.Add(NewPageView(ScriptStruct));
				}
			}
		}

		// Remove unused
		PageViews.Empty();
		PageViews += NewPageViews;
		if (PageSelectorComboBox.IsValid())
		{
			PageSelectorComboBox->RefreshOptions();
		}

		// Reassign current page: same view, first available view, or none.
		const TSharedPtr<PageView>* NewCurrentView = PageViews.FindByPredicate([ActiveStruct](TSharedPtr<PageView>& Page) { return Page->PageStruct == ActiveStruct; });
		HandlePageComboChanged(NewCurrentView ? *NewCurrentView : PageViews.Num() ? PageViews[0] : TSharedPtr<PageView>(), ESelectInfo::Direct);
	}
}

void FAffinityTableEditor::ResyncAsset()
{
	check(TableBeingEdited);

	// Reshape our tree
	//////////////////////////////////////////////////////////////////////////

	bool HasDeletes = false;

	// Update existing rows
	TArray<TWeakPtr<FAffinityTableNode>> StaleNodes;
	FStaleNodeWalker StaleRows(
		StaleNodes,
		[this](const FGameplayTag& Tag) { return TableBeingEdited->GetRowIndex(Tag); },
		[this](TWeakPtr<FAffinityTableNode> Node) {
			FAffinityTableNode::NodeSharedPtr Ptr = Node.Pin();
			TableBeingEdited->TryGetTagColor(Ptr->GetTag(), Ptr->GetColorRef(), true);
		},
		RowRoot);

	HasDeletes |= StaleNodes.Num() > 0;
	for (TWeakPtr<FAffinityTableNode> Node : StaleNodes)	// -V1078 Container is filled in lambda function above
	{
		DeleteRow(Node);
	}
	StaleNodes.Empty();

	// Update stale columns
	FStaleNodeWalker StaleColumns(
		StaleNodes,
		[this](const FGameplayTag& Tag) { return TableBeingEdited->GetColumnIndex(Tag); },
		[this](TWeakPtr<FAffinityTableNode> Node) {
			FAffinityTableNode::NodeSharedPtr Ptr = Node.Pin();
			TableBeingEdited->TryGetTagColor(Ptr->GetTag(), Ptr->GetColorRef(), false);
		},
		ColumnRoot);

	HasDeletes |= StaleNodes.Num() > 0;
	for (TWeakPtr<FAffinityTableNode> Node : StaleNodes)	// -V1078 Container is filled in lambda function above
	{
		DeleteColumn(Node);
	}
	StaleNodes.Empty();

	// Add new rows and columns. Existing tags will remain unchanged
	for (const TPair<FGameplayTag, UAffinityTable::TagIndex>& Row : TableBeingEdited->GetRows())
	{
		InsertRow(Row.Key);
	}

	for (const TPair<FGameplayTag, UAffinityTable::TagIndex>& Column : TableBeingEdited->GetColumns())
	{
		InsertColumn(Column.Key);
	}

	// Update structures and cell relationships
	//////////////////////////////////////////////////////////////////////////

	// (Probably the most expensive update call in the editor)
	UpdateCells(CellAssetIndexes | CellInheritance | CellDescription | CellDataInheritance);

	UpdatePageSet();

	// Finally, refresh our UI
	if (!HasDeletes)
	{
		RefreshTable();
	}
}

void FAffinityTableEditor::InitTableViewport()
{
	// Scroll bars for vertical and horizontal motion
	TSharedRef<SScrollBar> HorizontalScrollBar = SNew(SScrollBar)
													 .Orientation(Orient_Horizontal)
													 .Thickness(FVector2D(12.0f, 12.0f));

	TSharedRef<SScrollBar> VerticalScrollBar = SNew(SScrollBar)
												   .Orientation(Orient_Vertical)
												   .Thickness(FVector2D(12.0f, 12.0f));

	HeaderRow = SNew(SHeaderRow);

	TableView = SNew(STableListView, this)
					.ListItemsSource(&AvailableRows)
					.HeaderRow(HeaderRow)
					.OnGenerateRow(this, &FAffinityTableEditor::MakeRow)
					.OnSelectionChanged(this, &FAffinityTableEditor::OnRowSelectionChanged)
					.OnContextMenuOpening(this, &FAffinityTableEditor::OnContextMenu)
					.ExternalScrollbar(VerticalScrollBar)
					.ConsumeMouseWheel(EConsumeMouseWheel::Always)
					.SelectionMode(ESelectionMode::Multi)
					.AllowOverscroll(EAllowOverscroll::No);

	RefreshTable();

	TableViewport = SNew(SVerticalBox)
					// Search bar
					+ SVerticalBox::Slot()
						  .AutoHeight()
							  [SNew(SHorizontalBox)

								  // Page selection button
								  + SHorizontalBox::Slot()
										.FillWidth(0.5f)
											[SAssignNew(PageSelectorComboBox, SComboBox<TSharedPtr<PageView>>)
													.OptionsSource(&PageViews)
													.OnGenerateWidget(this, &FAffinityTableEditor::GeneratePageComboItem)
													.OnSelectionChanged(this, &FAffinityTableEditor::HandlePageComboChanged)
														[SNew(STextBlock)
																.Text(this, &FAffinityTableEditor::GetPageComboText)]

	]

								  // Visibility toggle
								  + SHorizontalBox::Slot()
										.AutoWidth()
											[SAssignNew(VisibilityComboButton, SComboButton)
													.ContentPadding(0)
													.ForegroundColor(this, &FAffinityTableEditor::GetVisibilityBtnForeground)
													.ButtonStyle(FAppStyle::Get(), "ToggleButton")
													.OnGetMenuContent(this, &FAffinityTableEditor::GetVisibilityBtnContent)
													.ButtonContent()
														[SNew(SHorizontalBox) + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SImage).Image(FAppStyle::GetBrush("GenericViewButton"))] + SHorizontalBox::Slot().AutoWidth().Padding(2, 0, 0, 0).VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("VisibilityToggle", "Visibility"))]

	]

	]]
					// Main content
					+ SVerticalBox::Slot()
						  [SNew(SHorizontalBox) + SHorizontalBox::Slot().FillWidth(1.0f)[SNew(SScrollBox).Orientation(Orient_Horizontal).ExternalScrollbar(HorizontalScrollBar)
																						 // Actual table content

																						 + SScrollBox::Slot()[TableView.ToSharedRef()]] +
							  SHorizontalBox::Slot()
								  .AutoWidth()
									  [VerticalScrollBar]]
					// Vertical scrollbar
					+ SVerticalBox::Slot()
						  .AutoHeight()
							  [SNew(SHorizontalBox) + SHorizontalBox::Slot()
														  [HorizontalScrollBar]];
}

TSharedRef<SWidget> FAffinityTableEditor::GeneratePageComboItem(TSharedPtr<PageView> InItem)
{
	return SNew(STextBlock).Text(FText::FromString(InItem->PageStruct->GetName()));
}

void FAffinityTableEditor::HandlePageComboChanged(TSharedPtr<PageView> Item, ESelectInfo::Type SelectInfo)	  // -V813 (cannot change UE-defined signature)
{
	if (ActivePageView != Item)
	{
		ActivePageView = Item;
		if (ActivePageView.IsValid())
		{
			DisplaySelectedCellStruct();
			UpdateCells(CellDataInheritance | CellDescription | CellVisibleFields | CellInheritance);
		}
	}
}

FText FAffinityTableEditor::GetPageComboText() const
{
	if (ActivePageView.IsValid())
	{
		return FText::FromString(ActivePageView->PageStruct->GetName());
	}
	return FText::GetEmpty();
}

TSharedRef<ITableRow> FAffinityTableEditor::MakeRow(FAffinityTableNode::NodeSharedPtr InNodePtr, const TSharedRef<STableViewBase>& Owner)	 // -V813 (cannot change UE-defined signature)
{
	return SNew(SAffinityTableListViewRow, Owner)
		.Editor(SharedThis(this))
		.RowNode(InNodePtr);
}

void FAffinityTableEditor::OnRowSelectionChanged(FAffinityTableNode::NodeSharedPtr InNewSelection, ESelectInfo::Type InSelectInfoType)	  // -V813 (cannot change UE-defined signature)
{
	// Here we only detect invalid row selections. Cell selections are done directly by SAffinityTableCell or STableListView
	if (!InNewSelection.IsValid())
	{
		static TSharedPtr<SAffinityTableCell> NoCell;
		SelectCell(NoCell);
	}
}

TSharedPtr<SWidget> FAffinityTableEditor::OnContextMenu()
{
	// (Context menu for cells. Rows and column context menus are handled by the header classes)
	FMenuBuilder MenuBuilder(true, nullptr);
	MenuBuilder.BeginSection("GridContextMenu");
	{
		//
		// Options that require a single cell selection
		//

		TWeakPtr<Cell> SelectedCell = GetPrimarySelectedCell();
		if (SelectedCell.IsValid())
		{
			// Locking a cell marks it as inheriting downstream, and prevents it from inheriting upstream
			// ----------------------------------------------------------------------------------------------
			MenuBuilder.AddMenuEntry(
				FText::FromString(TEXT("Make Independent")),
				FText::FromString(TEXT("Stop inheriting values, and propagate data downstream")),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([this, SelectedCell]() {
						TWeakPtr<Cell> LocalCell = SelectedCell;
						const FScopedTransaction Transaction(LOCTEXT("AT_Transaction_LockCell", "Mark Independent"));
						TableBeingEdited->Modify();
						LockCell(LocalCell);
					}),
					FCanExecuteAction::CreateLambda([this, SelectedCell]() { return SelectedCell.Pin()->InheritsData(); })),
				NAME_None,
				EUserInterfaceActionType::Button);

			// Unlocks this cell, inheriting to the left
			// ----------------------------------------------------------------------------------------------
			MenuBuilder.AddMenuEntry(
				FText::FromString(TEXT("Link Left")),
				FText::FromString(TEXT("Inherit the values of the next upstream row cell with set data")),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([this, SelectedCell]() {
						TWeakPtr<Cell> LocalCell = SelectedCell;
						const FScopedTransaction Transaction(LOCTEXT("AT_Transaction_LinkLeft", "Link Left"));
						TableBeingEdited->Modify();
						AcquireInheritance(LocalCell, true);
					}),
					FCanExecuteAction::CreateLambda([this, SelectedCell]() {
						TSharedPtr<Cell> CellPtr = SelectedCell.Pin();
						// We can inherit left if we are adjacent to a closed cell, or the adjacent cell inherits another in the same row. In other words,
						// we can only merge to another cell if the join maintains a square shape. This once-in-a-click function is not optimized.
						if (CellPtr.IsValid() && !CellPtr->InheritsData() && CellPtr->Column->HasValidParent())
						{
							TWeakPtr<Cell> LeftCell = GetCell(CellPtr->Row, CellPtr->Column->GetParent());
							return LeftCell.IsValid() && (!LeftCell.Pin()->InheritsData() || LeftCell.Pin()->InheritedCell.Pin()->Row->GetTag() == CellPtr->Row->GetTag());
						}
						return false;
					})),
				NAME_None,
				EUserInterfaceActionType::Button);

			// Unlocks this cell, inheriting upwards
			// ----------------------------------------------------------------------------------------------
			MenuBuilder.AddMenuEntry(
				FText::FromString(TEXT("Link Up")),
				FText::FromString(TEXT("Inherit the values of the next upstream column cell with set data")),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([this, SelectedCell]() {
						TWeakPtr<Cell> LocalCell = SelectedCell;
						const FScopedTransaction Transaction(LOCTEXT("AT_Transaction_LinkUp", "Link Up"));
						TableBeingEdited->Modify();
						AcquireInheritance(LocalCell, false);
					}),
					FCanExecuteAction::CreateLambda([this, SelectedCell]() {
						TSharedPtr<Cell> CellPtr = SelectedCell.Pin();
						// We can inherit up if we are adjacent to a closed cell, or the adjacent cell inherits another in the same column. In other words,
						// we can only merge to another cell if the join maintains a square shape. This once-in-a-click function is not optimized.
						if (CellPtr.IsValid() && !CellPtr->InheritsData() && CellPtr->Row->HasValidParent())
						{
							TWeakPtr<Cell> UpCell = GetCell(CellPtr->Row->GetParent(), CellPtr->Column);
							return UpCell.IsValid() && (!UpCell.Pin()->InheritsData() || UpCell.Pin()->InheritedCell.Pin()->Column->GetTag() == CellPtr->Column->GetTag());
						}
						return false;
					})),
				NAME_None,
				EUserInterfaceActionType::Button);

			MenuBuilder.AddMenuSeparator();

			// Mark the data of this cell for copy operations
			// ----------------------------------------------------------------------------------------------
			MenuBuilder.AddMenuEntry(
				FText::FromString(TEXT("Copy cell")),
				FText::FromString(TEXT("Copies all the data contained in this cell")),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([this, SelectedCell]() {
					TWeakPtr<Cell> LocalCell = SelectedCell;
					CopyCellData(LocalCell);
				})),
				NAME_None,
				EUserInterfaceActionType::Button);
		}

		//
		// Options that support multiple cells
		//

		// Paste all copied values into this cell
		// ----------------------------------------------------------------------------------------------
		MenuBuilder.AddMenuEntry(
			FText::FromString(TEXT("Paste cell")),
			FText::FromString(TEXT("Paste all values from the copied cell into this cell")),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this]() {
					const FScopedTransaction Transaction(LOCTEXT("AT_Transaction_PasteCell", "Paste Cell"));
					TableBeingEdited->Modify();
					PasteCellData(SelectedCells);
				}),
				FCanExecuteAction::CreateLambda([this]() { return ReferenceCell.IsValid() && SelectedCells.Num(); })),
			NAME_None,
			EUserInterfaceActionType::Button);

		// Paste only the currently visible parameters into this cell
		// ----------------------------------------------------------------------------------------------
		MenuBuilder.AddMenuEntry(
			FText::FromString(TEXT("Paste visible data only")),
			FText::FromString(TEXT("Only paste parameters currently marked as visible")),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this]() {
					const FScopedTransaction Transaction(LOCTEXT("AT_Transaction_PasteVisibleCell", "Paste Visible Cell"));
					TableBeingEdited->Modify();
					PasteCellData(SelectedCells, true);
				}),
				FCanExecuteAction::CreateLambda([this]() { return ReferenceCell.IsValid() && SelectedCells.Num() && ActivePageView.IsValid() && ActivePageView->VisibleProperties.Num(); })),
			NAME_None,
			EUserInterfaceActionType::Button);
	}
	MenuBuilder.EndSection();
	return MenuBuilder.MakeWidget();

	return TSharedPtr<SWidget>();
}

void FAffinityTableEditor::RefreshTable(bool RegenerateTree)
{
	// RegenerateTree will refresh our visible rows, columns, and cells. The SListView widget will catch the change and re-draw.
	if (RegenerateTree)
	{
		check(TableBeingEdited);

		// Refresh our list of visible rows
		AvailableRows.Empty();
		FVisibleNodeWalker RowWalker(AvailableRows);
		RowWalker.Walk(RowRoot);

		// Refresh our list of visible columns
		HeaderRow->ClearColumns();
		const TMap<FGameplayTag, UAffinityTable::TagIndex>& Columns = TableBeingEdited->GetColumns();
		if (Columns.Num())
		{
			// Top-left corner. Empty,
			HeaderRow->AddColumn(
				SHeaderRow::Column(ColumnHeaderName)
					.HAlignCell(HAlign_Fill)
						[SNew(SBorder)
								.BorderImage(FAppStyle::GetNoBrush())
								.Visibility(EVisibility::HitTestInvisible)
								.Padding(FMargin(0.0f, 0.0f, 0.0f, 3.0f))
								.HAlign(HAlign_Center)
								.VAlign(VAlign_Bottom)
									[SNew(STextBlock)]]);

			// Gather a list of visible columns
			TArray<FAffinityTableNode::NodeSharedPtr> VisibleNodes;
			FVisibleNodeWalker ColumnWalker(VisibleNodes);
			ColumnWalker.Walk(ColumnRoot);

			for (FAffinityTableNode::NodeSharedPtr& Column : VisibleNodes)
			{
				FName TagName = Column->GetTag().GetTagName();
				HeaderRow->AddColumn(
					SHeaderRow::Column(TagName)
						.HAlignCell(HAlign_Fill)
						.HeaderContentPadding(FMargin(0))
						.ManualWidth(FAffinityTableStyles::RowHeaderMinWidth)
							[SNew(SAffinityTableColumnHeader)
									.Editor(SharedThis(this))
									.Node(Column)]);
			}
		}
	}
	TableView->RequestListRefresh();
}

void FAffinityTableEditor::ToggleNode(const TWeakPtr<FAffinityTableNode>& Node)
{
	FAffinityTableNode::NodeSharedPtr NodePtr = Node.Pin();
	if (NodePtr.IsValid())
	{
		NodePtr->SetCollapsed(!NodePtr->IsCollapsed());
		RefreshTable();
	}
}

void FAffinityTableEditor::DeleteRow(const TWeakPtr<FAffinityTableNode>& Row)
{
	check(TableBeingEdited);

	FAffinityTableNode::NodeSharedPtr Node = Row.Pin();
	if (Node.IsValid())
	{
		FLambdaWalker DeleteWalker([this](TWeakPtr<FAffinityTableNode> ThisNode) {
			check(ThisNode.IsValid());

			// Remove from our list of available rows
			FAffinityTableNode* ThisNodePtr = ThisNode.Pin().Get();
			const int32 RowIndex = this->AvailableRows.IndexOfByPredicate([ThisNodePtr](const FAffinityTableNode::NodeSharedPtr& Ptr) {
				return Ptr.IsValid() && Ptr.Get() == ThisNodePtr;
			});
			if (RowIndex != INDEX_NONE)
			{
				AvailableRows.RemoveAt(RowIndex);
			}

			// Remove from our grid
			if (CellTable.Contains(ThisNodePtr))
			{
				CellTable.Remove(ThisNodePtr);
			}

			// Remove from the asset
			TableBeingEdited->DeleteRow(ThisNodePtr->GetTag());
			return true;
		});
		DeleteWalker.Walk(Node);
		// This removes the node from our tree
		Node->Unlink();
		RefreshTable(false);
	}
}

void FAffinityTableEditor::DeleteColumn(const TWeakPtr<FAffinityTableNode>& Column)
{
	check(TableBeingEdited);

	// Remove from asset
	FAffinityTableNode::NodeSharedPtr Node = Column.Pin();
	if (Node.IsValid())
	{
		FLambdaWalker DeleteWalker([this](TWeakPtr<FAffinityTableNode> ThisNode) {
			// Remove from our available columns
			check(ThisNode.IsValid());
			const FAffinityTableNode* NodePtr = ThisNode.Pin().Get();
			const FName TagName = NodePtr->GetTag().GetTagName();
			HeaderRow->RemoveColumn(TagName);
			ColumnNodes.Remove(TagName);

			// Remove from our grid
			for (TPair<FAffinityTableNode*, CellMap>& Row : CellTable)
			{
				if (Row.Value.Contains(NodePtr))
				{
					Row.Value.Remove(NodePtr);
				}
			}

			TableBeingEdited->DeleteColumn(NodePtr->GetTag());
			return true;
		});
		DeleteWalker.Walk(Node);
		Node->Unlink();
		RefreshTable(false);
	}
}

void FAffinityTableEditor::PickColorForHeader(SAffinityTableHeader* Header, bool IsRow)
{
	check(TableBeingEdited);
	FColorPickerArgs PickerArgs;

	PickerArgs.InitialColor = Header->GetNode().Pin()->GetColor();
	PickerArgs.bOnlyRefreshOnOk = true;
	PickerArgs.OnColorCommitted = FOnLinearColorValueChanged::CreateLambda([=](FLinearColor NewColor) {
		FAffinityTableNode* NodePtr = Header->GetNode().Pin().Get();
		if (NodePtr->GetColor() != NewColor)
		{
			NodePtr->GetColorRef() = NewColor;

			const FScopedTransaction Transaction(LOCTEXT("AT_Transaction_PickColor", "Set Color"));
			TableBeingEdited->Modify();
			TableBeingEdited->SetTagColor(NodePtr->GetTag(), NewColor, IsRow);
			RefreshTable(true);
		}
	});

	OpenColorPicker(PickerArgs);
}

TSharedRef<SDockTab> FAffinityTableEditor::SpawnCellPropertiesTab(const FSpawnTabArgs& Args)
{
	TSharedPtr<SDockTab> NewTab = SNew(SDockTab)
									  .TabColorScale(GetTabColorScale())
										  [DetailsView->GetWidget().ToSharedRef()];
	NewTab->SetTabIcon(FAppStyle::GetBrush("GenericEditor.Tabs.Properties"));
	return NewTab.ToSharedRef();
}

TSharedRef<SDockTab> FAffinityTableEditor::SpawnTablePropertiesTab(const FSpawnTabArgs& Args)
{
	TSharedPtr<SDockTab> NewTab = SNew(SDockTab)
									  .TabColorScale(GetTabColorScale())
										  [TableDetailsView.ToSharedRef()];
	NewTab->SetTabIcon(FAppStyle::GetBrush("GenericEditor.Tabs.Properties"));
	return NewTab.ToSharedRef();
}

TSharedRef<SDockTab> FAffinityTableEditor::SpawnTableWidgetTab(const FSpawnTabArgs& Args)
{
	InitTableViewport();
	return SNew(SDockTab)
		.Label(LOCTEXT("AT_ViewportTab", "Table"))
		.TabColorScale(GetTabColorScale())
			[SNew(SBorder)
					.Padding(2)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
						[TableViewport.ToSharedRef()]];
}

#undef LOCTEXT_NAMESPACE
