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
#include "Editor/PropertyEditor/Public/PropertyEditorDelegates.h"
#include "EditorUndoClient.h"
#include "Logging/LogMacros.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Toolkits/IToolkitHost.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Views/STreeView.h"

#include "AffinityTable.h"
#include "AffinityTableNode.h"

#include "AffinityTableEditor.generated.h"

class SAffinityTableCell;
class SAffinityTableHeader;

DECLARE_LOG_CATEGORY_EXTERN(LogAffinityTableEditor, Log, All);

/**
 * Keeper of per-affinity table preferences
 */
USTRUCT()
struct FAffinityTablePreferences
{
	GENERATED_BODY()

	/** List of collapsed rows */
	UPROPERTY()
	TSet<FName> CR;

	/** List of collapsed columns */
	UPROPERTY()
	TSet<FName> CC;
};

/**
 * Keeper of per-user local preferences for the AffinityTable Editor
 */
UCLASS(Config = EditorPerProjectUserSettings)
class UAffinityTableEditorPreferences : public UObject
{
	GENERATED_BODY()
public:
	/**
	 * Retrieve existing table preferences
	 *
	 * @param TableName Unique name of a table asset
	 * @return A pointer to the preferences of this table, if any exist
	 */
	const FAffinityTablePreferences* GetPreferencesForTable(const FName& TableName) const;

	/**
	 * Assigns preferences for a given table asset
	 *
	 * @param TableName Unique name of a table asset
	 * @param Preferences Desired preferences for this table
	 */
	void SetPreferencesForTable(const FName& TableName, const FAffinityTablePreferences& Preferences);

private:
	/** Preferences per table asset */
	UPROPERTY(Config)
	TMap<FName, FAffinityTablePreferences> TablePreferences;
};

/**
 *	Defines the base editor for UAffinityTable assets.
 *
 *	It implements a traditional editor toolkit interface and maintains three panels: one for asset options,
 *	one for cell options, and a main widget for the actual table. this last one is an SListView with
 *  a custom row class (SAffinityTableListViewRow)
 */
class FAffinityTableEditor : public FAssetEditorToolkit, FEditorUndoClient
{
public:
	FAffinityTableEditor();
	virtual ~FAffinityTableEditor();

	/**
	 * PageView contains the data required to render the grid of each script structure contained in our asset,
	 * it should cache anything that changes (and should be remembered) when the user picks a different page.
	 */
	struct PageView
	{
		/** Structure linked to this page (simpler for UI purposes) */
		const UScriptStruct* PageStruct;

		/** Properties and their visibility */
		TMap<const FProperty*, bool> CellVisibility;

		/** Cached cell visibility */
		TArray<const FProperty*> VisibleProperties;

		/** Inheritance drawing (True displays row inheritance. False displays column inheritance) */
		bool DisplayRowInheritance;

		/** Background color based on cell taxonomy (true draws a color based on our top-level parent) */
		bool DisplayTaxonomyColor;
	};

	/**
	 * Cell contains the data required to represent a single cell in our table.
	 */
	struct Cell
	{
		/** location of our structure in memory */
		UAffinityTable::Cell TableCell;

		/** Assigned row */
		FAffinityTableNode* Row;

		/** Assigned column */
		FAffinityTableNode* Column;

		/** Cell we inherit data from, if available */
		TWeakPtr<Cell> InheritedCell;

		/** The UI-managed cell we are currently assigned, if any. UI cells come and go based on the */
		/** editor's underlying mechanisms, therefore the UICell is not always a valid instance.     */
		TWeakPtr<SAffinityTableCell> UICell;

		/**
		 * Returns true if we inherit data from another cell
		 */
		FORCEINLINE bool InheritsData() const
		{
			return InheritedCell.IsValid();
		}

		/** Shorthand for a CellTags version of this cell */
		FORCEINLINE UAffinityTable::CellTags AsCellTags() const
		{
			return UAffinityTable::CellTags{ Row->GetTag(), Column->GetTag() };
		}

		/** The UI cell if there is one. Nullptr otherwise */
		FORCEINLINE SAffinityTableCell* GetUICell()
		{
			return UICell.IsValid() ? UICell.Pin().Get() : nullptr;
		}
	};

	/**
	 * Types of cell selection behavior
	 */
	enum ECellSelectionType
	{
		// Select one cell
		Single,
		// Add a cell to our selection list
		Additive,
		// Add a region of cells our selection list
		Area
	};

	/** A map of cells by (node) tag */
	using CellMap = TMap<FAffinityTableNode*, TSharedRef<Cell>>;

	// IToolkit interface
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FText GetToolkitToolTipText() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	virtual bool IsPrimaryEditor() const override
	{
		return true;
	}
	virtual void RegisterTabSpawners(const TSharedRef<class FTabManager>& TabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<class FTabManager>& TabManager) override;
	// end IToolkitInterface

	// FEditorUndoClient interface
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;
	// End FEditorUndoClient interface

	/**
	 * Standard initialization call
	 * Issued by the AffinityTableEditorModule to configure a new editor instance
	 */
	virtual void InitAffinityTableEditor(const EToolkitMode::Type Mode, const TSharedPtr<class IToolkitHost>& InitToolkitHost, UAffinityTable* Table);

	/**
	 * Adds a cell to our list of selected cells
	 * @param NewSelectedCell The cell we are selecting
	 */
	void SelectCell(TSharedPtr<SAffinityTableCell>& NewSelectedCell);

	/**
	 * Reacts to a key down event in our selected cell
	 * @param InKeyEvent The event that happened to our selected cell
	 */
	FReply OnSelectedCellKeyDown(const FKeyEvent& InKeyEvent);

	/**
	 * Reacts to a key up event in our selected cell
	 * @param InKeyEvent The event that happened to our selected cell
	 */
	FReply OnSelectedCellKeyUp(const FKeyEvent& InKeyEvent);

	/** Reacts to the moment where our table lost its input focus */
	void OnTableFocusLost();

	/**
	 * Retrieves a table cell based on its row and column coordinates, or nullptr if the cell is not found
	 * @param Row Designated row node
	 * @param Column Designated column node
	 */
	TWeakPtr<Cell> GetCell(const FAffinityTableNode* Row, const FAffinityTableNode* Column);

	/** Toggles visibility of a node */
	void ToggleNode(const TWeakPtr<FAffinityTableNode>& Node);

	/**
	 * Deletes the provided row node
	 * @param Row Node with the row to delete
	 */
	void DeleteRow(const TWeakPtr<FAffinityTableNode>& Row);

	/**
	 * Deletes the provided column node
	 * @param Column Node with the column to delete
	 */
	void DeleteColumn(const TWeakPtr<FAffinityTableNode>& Column);

	/**
	 * Changes the color of the provided header.
	 * @param Header Header to modify
	 * @param IsRow true if this header is a row header. False for column
	 */
	void PickColorForHeader(SAffinityTableHeader* Header, bool IsRow);

	/** Read/write access to the resource we are editing */
	FORCEINLINE UAffinityTable* GetTableBeingEdited()
	{
		return TableBeingEdited;
	}

	/**
	 * Returns the cached node for a column based on its name, or InvalidIndex if not found.
	 * @param A column name obtained from one of our asset's tags
	 */
	FORCEINLINE TWeakPtr<FAffinityTableNode> GetNodeForColumn(const FName& ColumnName)
	{
		return ColumnNodes.Contains(ColumnName) ? ColumnNodes[ColumnName] : nullptr;
	}

	/**
	 * Const access to our current page view for referencing.
	 */
	FORCEINLINE const TWeakPtr<PageView> GetActivePageView() const
	{
		return ActivePageView;
	}

	/** Name of our column header */
	static const FName ColumnHeaderName;

private:
	/**
	 * Resets our inner structures to conform to the topology of the
	 * Affinity Table we are currently editing.
	 */
	void ResyncAsset();

	// UI Utility Functions
	//////////////////////////////////////////////////////////////////////////

	/** Initializes the UI components of the structure grid editor panel */
	void InitTableViewport();

	/** (FTabManager) Spawns a panel that displays the properties of the structure in the currently active cell */
	TSharedRef<SDockTab> SpawnCellPropertiesTab(const FSpawnTabArgs& Args);

	/** (FTabManager) Spawns a panel that displays the properties of the current AffinityTable asset */
	TSharedRef<SDockTab> SpawnTablePropertiesTab(const FSpawnTabArgs& Args);

	/** (FTabManager) Spawns our AffinityTable editor widget */
	TSharedRef<SDockTab> SpawnTableWidgetTab(const FSpawnTabArgs& Args);

	/** (SComboBox) Configures the page selector drop-down */
	TSharedRef<SWidget> GeneratePageComboItem(TSharedPtr<PageView> InItem);

	/** (SComboBox) Reacts to the user picking a page structure to view */
	void HandlePageComboChanged(TSharedPtr<PageView> Item, ESelectInfo::Type SelectInfo);

	/** (SComboBox) Retrieves the human-readable version of a structure page */
	FText GetPageComboText() const;

	/** Extends our editor toolbar with ad-hoc components */
	void ExtendToolbar();

	/** (FExtender) Configures the toolbar builder with custom buttons and actions */
	void FillToolbar(FToolBarBuilder& ToolbarBuilder);

	/** (SComboButton) Returns the desired foreground color for our visibility combo button */
	FSlateColor GetVisibilityBtnForeground() const;

	/** (SComboButton) Creates and returns a widget for our visibility combo button */
	TSharedRef<SWidget> GetVisibilityBtnContent();

	/**
	 * Toggles the visibility of a property in the current structure page.
	 * @param Property Instance of the property to show or hide
	 */
	void TogglePropertyVisibility(const FProperty* Property);

	/**
	 * Returns true if the provided property is currently visible.
	 * @param Property Instance of the property to query
	 */
	bool IsPropertyVisible(const FProperty* Property) const;

	// Tag Tree Management Functions
	//////////////////////////////////////////////////////////////////////////

	/** (STreeView) Creates a widget for a row in the table widget */
	TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FGameplayTagNode> InItem, const TSharedRef<STableViewBase>& OwnerTable);

	/** (STreeView) Populates the Tag Tree selector */
	void OnGetChildren(TSharedPtr<FGameplayTagNode> InItem, TArray<TSharedPtr<FGameplayTagNode>>& OutChildren);

	/** (STreeView) Handles the user picking a tag from the input panel */
	void OnTagSelectionChanged(TSharedPtr<FGameplayTagNode> InTag, ESelectInfo::Type SelectInfo);

	/** Handles the user picking a new tag for a row or a column */
	FReply OnAddTag();

	/**
	 * Inserts a new tag into our table
	 * @param InTag The tag to add
	 * @param InNode The node where the insertion happens (parent node)
	 * @param OnIndexForTag Callback index generator for this tag
	 * @param OnNewNode Callback for any new nodes resulting from this tag
	 */
	static void InsertTag(const FGameplayTag& InTag, TWeakPtr<FAffinityTableNode> InNode, FAffinityTableNode::IndexGenerator OnIndexForTag, FAffinityTableNode::NewNodeCallback OnNewNode = nullptr);

	// AffinityTable Widget Messaging and Maintenance
	//////////////////////////////////////////////////////////////////////////

	/** (STableListView) Creates a new Affinity Table row row */
	TSharedRef<ITableRow> MakeRow(FAffinityTableNode::NodeSharedPtr InNodePtr, const TSharedRef<STableViewBase>& Owner);

	/** (StableListView) The user has picked a different row */
	void OnRowSelectionChanged(FAffinityTableNode::NodeSharedPtr InNewSelection, ESelectInfo::Type InSelectInfoType);

	/** (STableListView) The user has right-clicked on a cell */
	TSharedPtr<SWidget> OnContextMenu();

	/**
	 * Invalidates the current table view.
	 * @param RegenerateTree If true, the whole visual array of rows and columns is reconstructed.
	 */
	void RefreshTable(bool RegenerateTree = true);

	/**
	 * If a single cell is selected, display its properties on the property editor
	 * (The editor window does not support multi-structure editing)
	 */
	void DisplaySelectedCellStruct();

	// Tag Picker
	//
	// The tag picker is a heavily simplified version of SGameplayTagWidget, which
	// is private an inaccessible from here.
	//////////////////////////////////////////////////////////////////////////

	/**
	 * Displays a window to pick a game tag
	 * @param PickRow True if we are picking a row tag. False for a column tag.
	 */
	void DisplayGameTagPicker(bool PickRow);

	/**
	 * Modify the search filter for tags
	 * @param InFilterText Text pattern to filter tags in our tag picking window
	 */
	void OnFilterTagChanged(const FText& InFilterText);

	/**
	 * Filters the existing tag tree based on our search pattern
	 */
	void FilterTagTree();

	/**
	 * Filters the tag tree from this node downwards
	 * @param InTag Tag node to filter
	 */
	bool FilterTag(const TSharedPtr<FGameplayTagNode>& InTag);

	/**
	 * Expands or collapses this node and its descendants.
	 * @param InTag Node to open or close
	 * @param ShouldExpand Determines if this node and its descendants should be expanded
	 */
	void SetTagNodeItemExpansion(TSharedPtr<FGameplayTagNode>& InTag, bool ShouldExpand);

	// Cell Maintenance
	//////////////////////////////////////////////////////////////////////////

	/** Type of update operation to do over the cells. */
	using CellUpdateType = uint8;

	/** Update the description in cells with UI components */
	static constexpr CellUpdateType CellDescription = 1;

	/** Enact data inheritance. This will cause inherited cells to modify values based on their parents */
	static constexpr CellUpdateType CellDataInheritance = 1 << 1;

	/** Refresh the cached list of visible fields before all other updates */
	static constexpr CellUpdateType CellVisibleFields = 1 << 2;

	/** Refresh cell inheritance from our asset */
	static constexpr CellUpdateType CellInheritance = 1 << 3;

	/** Refresh the asset indexes for the row and column of this cell*/
	static constexpr CellUpdateType CellAssetIndexes = 1 << 4;

	/**
	 * Performs update operations over our table cells.
	 *
	 * @param UpdateType Flags with the cell update types to perform
	 * @param CellsToUpdate If provided, refresh only the provided cells. All cells refreshed otherwise.
	 */
	void UpdateCells(CellUpdateType UpdateType, TArray<TWeakPtr<Cell>>* CellsToUpdate = nullptr);

	/** (IStructureDetailsView) React to the user changing a structure value for the currently selected cell */
	void OnCellPropertyValueChanged(const FPropertyChangedEvent& PropertyChangedEvent);

	/**
	 * Perform updates after the value in a cell has changed
	 * @param UpdatedCell The cell that was modified.
	 */
	void OnCellValueChanged(TWeakPtr<Cell>& UpdatedCell);

	/**
	 * Inserts a row into our table. Assumes our asset already has it
	 * @param Tag The tag we are inserting.
	 */
	void InsertRow(const FGameplayTag& Tag);

	/**
	 * Inserts a column into our table. Assumes our asset already has it
	 * @param Tag The tag we are inserting
	 */
	void InsertColumn(const FGameplayTag& Tag);

	/** Updates our set of available pages based on the structures in the current asset */
	void UpdatePageSet();

	/**
	 * Adds a page to our available views
	 * @param ScriptStruct A structure known to our current AffinityTable asset
	 */
	TSharedPtr<PageView> NewPageView(const UScriptStruct* ScriptStruct);

	/**
	 * Marks a cell as independent, propagating its data downstream
	 * @param InCell Cell to mark as independent
	 */
	void LockCell(TWeakPtr<Cell>& InCell);

	/**
	 * If a single cell is selected, return its editor data. Returns invalid if
	 * multiple or no cells are selected
	 */
	TWeakPtr<Cell> GetPrimarySelectedCell();

	/**
	 * Marks this cell as reference for copy/paste operations
	 * @param SourceCell Cell that will provide copy data
	 */
	void CopyCellData(TWeakPtr<Cell>& SourceCell);

	/**
	 * Copies data from the ReferenceCell to the TargetCells. The targets will lose
	 * their inheritance attributes, if they had any.
	 * @param TargetCells Cells that receives data.
	 * @param VisiblePropertiesOnly If true, only copy currently visible properties
	 */
	void PasteCellData(TArray<TWeakPtr<Cell>>& TargetCells, bool VisiblePropertiesOnly = false);

	/**
	 * Creates an inheritance bond between this cell and the next upstream cell with non-inherited data.
	 * This operation will delete the data on this cell and re-propagate data downstream
	 * @param InCell A cell with non-inherited data.
	 * @param ColumnStream If true, we inherit along column values
	 */
	void AcquireInheritance(TWeakPtr<Cell>& InCell, bool ColumnStream);

	/**
	 * Moves inheritance data downstream from the provided cell
	 * @param InCell Cell that becomes parent to the data downstream
	 * @param Force If true, force inheritance over cells that had previous data. This may result in data loss!
	 */
	void PropagateInheritance(TWeakPtr<Cell>& InCell, bool Force = false);

	/**
	 * Gathers a list of cells that inherit data from the provided table cell.
	 * @param InTableCell A cell that provides data to other cells
	 * @param OutCells Array of cells that receive data from this cell
	 * @param Force If true, recursion ignores previous link relationships.
	 */
	void GatherInheritedCells(TWeakPtr<Cell>& InCell, TArray<TWeakPtr<Cell>>& OutCells, bool Force = false);

	/**
	 * Gathers all the cells in between the provided corners
	 * @param CornerA First cell corner
	 * @param CornerB Second cell corner
	 * @param OutCells Array that contains the resulting area, including CornerA and CornerB
	 */
	void GatherCellsBetween(TWeakPtr<Cell>& CornerA, TWeakPtr<Cell>& CornerB, TArray<TWeakPtr<Cell>>& OutCells);

	/**
	 * Set all the sub-tables in this asset to depend from their topmost tags
	 */
	void ResetTableInheritance();

	/**
	 * Assigns new inheritance ancestors for this cell.
	 * @param InCell Cell that gets its inheritance reset
	 * @param ColumnStream True if we inherit across values, false to inherit across rows.
	 * @param RestoreFromAsset If true, try to restore this cell's inheritance from our table asset first. False
	 *			will overwrite any previously existing link.
	 */
	void AssignInheritance(TWeakPtr<Cell> InCell, bool ColumnStream, bool RestoreFromAsset = false);

	/**
	 * Establishes a parent->child data inheritance relationship between two cells
	 * @param Child Cell that receives data
	 * @param Parent Cell that provides data to the child
	 */
	void LinkCells(TWeakPtr<Cell>& Child, TWeakPtr<Cell>& Parent);

	/**
	 * Removes any existing cell linking to this cell, marking it as non-inheriting.
	 * @param InCell Cell that loses data inheritance
	 */
	void UnlinkCell(TWeakPtr<Cell>& InCell);

	/**
	 * Loads the preferences of the editor from local .ini config
	 */
	void LoadTablePreferences() const;

	/**
	 * Saves the current preferences back into a local .ini config
	 */
	void SaveTablePreferences() const;

	/** The AffinityTable we are editing. Assumed to be valid through the lifetime of the editor  */
	UAffinityTable* TableBeingEdited;

	/** Pointer to the root of our column tree */
	FAffinityTableNode::NodeSharedPtr ColumnRoot;

	/** Pointer to the root of our row tree */
	FAffinityTableNode::NodeSharedPtr RowRoot;

	/** Table of cells contained in our loaded asset, mapped by row tag */
	TMap<FAffinityTableNode*, CellMap> CellTable;

	/** Column names to indexes dictionary, cached */
	TMap<FName, TWeakPtr<FAffinityTableNode>> ColumnNodes;

	/** Details view */
	TSharedPtr<class IStructureDetailsView> DetailsView;

	/** Table-specific details view */
	TSharedPtr<class IDetailsView> TableDetailsView;

	/** Table editor section */
	TSharedPtr<class SWidget> TableViewport;

	/** Search box */
	TSharedPtr<class SSearchBox> SearchBoxWidget;

	/** Our table view proper */
	TSharedPtr<SListView<FAffinityTableNode::NodeSharedPtr>> TableView;

	/** Header row for the AffinityTable widget */
	TSharedPtr<SHeaderRow> HeaderRow;

	/** Combo button for the page selection */
	TSharedPtr<SComboBox<TSharedPtr<PageView>>> PageSelectorComboBox;

	/** Combo button for configuring the grid's visible properties */
	TSharedPtr<class SComboButton> VisibilityComboButton;

	/** Array of available page views */
	TArray<TSharedPtr<PageView>> PageViews;

	/** The page view we are currently working with */
	TSharedPtr<PageView> ActivePageView;

	/** Available rows on the selected page. Separate from the cached one for UI purposes */
	TArray<FAffinityTableNode::NodeSharedPtr> AvailableRows;

	/** Currently selected cells */
	TArray<TWeakPtr<Cell>> SelectedCells;

	/** A cell marked for data moving operations */
	TWeakPtr<Cell> ReferenceCell;

	/** Current cell adding mode */
	ECellSelectionType CellSelectionType;

	/** Container for all known tags in our tag selector window */
	TArray<TSharedPtr<FGameplayTagNode>> TagItems;

	/** Array for a culled-out set of tags when TagFilterString is active */
	TArray<TSharedPtr<FGameplayTagNode>> FilteredTagItems;

	/** Tag filtering pattern */
	FString TagFilterString;

	/** Tree widget to show all available tags */
	TSharedPtr<STreeView<TSharedPtr<FGameplayTagNode>>> TagTreeWidget;

	/** Tag selection widget */
	TSharedPtr<SWidget> AddTagWidget;

	/** Tag selection window */
	TWeakPtr<SWindow> AddTagWindow;

	/** Tag we are currently adding */
	FGameplayTag SelectedTag;

	/** AT Editor preferences */
	TStrongObjectPtr<UAffinityTableEditorPreferences> EditorPreferences;

	/** Whether we are currently adding a row or column tag */
	bool SelectedTagIsRow;
};
