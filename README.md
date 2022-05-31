# Affinity Table Plugin

Affinity Tables define and query structured information based on intersections and relationships between gameplay tags. This information can be used to data-drive entire systems using contextual data. 

Tags define the rows and columns of your table, while generic Unreal Structures define the type of information at their cell intersections. 

Each structure type defines a page in the table. A single query operation can return any number of pages describing the relationship between two given tags. 

Queries and data can be taxonomic, meaning that information set for one tag can be inherited downstream to other tags in the same family, and queries can look up for the closest match, thereby allowing a system to work with closest matches if required.

## Creating an Affinity Table

You will find the Affinity Table asset under the Miscellaneous classification. For example, to create a new table:

1. Click Add New on the Content Browser or right-click on an empty area of the browser.
2. Under Miscellaneous, select Affinity Table.

## Authoring an Affinity Table

### Pages

The first thing to do with a fresh AffinityTable is to define the type of data it will hold. We use Unreal structures to determine the format of the table's cells, and each structure creates an independent page. For example, a single AT can have one page for sound data, another for material parameters, and another for impact effects. 

All pages share the same rows and columns, but the data on its cells will reflect the structure chosen for that particular page. Querying a multi-page AT will return data sets for the matching cell across all pages or just on the requested pages. 

To add pages modify the Structures list on the _Cells_ portion of the properties panel (1). The editor creates a page for each entry on the Structures list and makes it available for selection on the page dropdown (2).  

![Adding Pages](/Docs/images/AddingPages.jpg)

### Rows and Columns

All pages share the same rows and columns, which reflect the taxonomy of Gameplay tags. Click on the Add Row or Add Column buttons in the table's menu (1) and pick a tag. Observe that the table creates a new row or column for each tag's parts. For example, choosing "World.Impact.Earth" will create three columns: World, World.Impact, and World.Impact.Earth (2).

Having at least one row and one column creates data cells. 

The selected page reflects the data available on each cell. For example, with the DamageData page chosen (3), we can edit the damage data on each cell (4) by clicking on it and editing the Cell Properties panel (5). 

![Rows and Columns](/Docs/images/RowsAndColumns.jpg)

The Visibility option at the right of the page selector lets you pick the data displayed on each cell. You can either select a combination of the parameters in the page's structure, or the row/column inheritance. For example, in the following image, we display each cell's damage type and strength. 

![Visibility](/Docs/images/visibility.jpg)

### Inheritance

Any given cell in the table can either have data of its own or inherit the data from an upstream cell. This inheritance system allows you to add any number of tags to a table and fine-tune only those requiring specific data. Each page has its inheritance map. 

Consider these three tags:

A
A.B
A.B.C

In the beginning, cell A will overwrite the data in A.B and A.B.C. This relationship means that cell A is independent, and cells A.B and A.B.C are inheriting.

Editing any parameter in cells A.B or A.B.C will make them independent (you can also mark them without changing their data by selecting the Make Independent option on the contextual menu). For example, making A.B independent will prevent it from receiving values from A and will propagate its values downstream to A.B.C.

Making a cell independent re-links their data inheritance relationship downstream through rows and columns. A cell will inherit another one if:

- It is not the top-level tag in a hierarchy.
- Its value is unchanged, or the cell has not been manually marked independent.

When a cell is independent, its inheriting descendants are the cells that:

- Are downstream through row and column taxonomies.
- Are not already inheriting data from another independent cell in their row or column.

We visualize a page's inheritance by unchecking all structure parameters in the Visibility dropdown. Recall the illustration two images above:

- All cells marked as `[independent]` own their data. 
- Any other cells display the name of the Row/Column they inherit data from. 
- The top-level cell is always independent when you add new rows and columns. 

Changing an independent cell will change the data for all its dependent cells downstream. For example, changing the damage type on the Creature Ability / World cell to _Slash_ will immediately change it to all its other cells downstream:

![top level inheritance](/Docs/images/inheritance.jpg)

However, editing the cell at CreatureAbility.Attack / World.Impact to _Pierce_ will make it independent and inherit its values downstream:

![inheritance](/Docs/images/inheritance2.jpg)
![inheritance](/Docs/images/inheritance3.jpg)

This feature gives you incredible flexibility in how your data is defined, but it can take some time to get used to. For example, here is the damage type result of several queries assuming the inheritance map above:

- CreatureAbility.Attack Vs World.Impact.Water = Pierce, since World.Impact.Water doesn't exist, this query becomes CreatureAbility.Attack Vs World.Impact, which throws Pierce. 
- CreatureAbility Vs World.Impact = Slash
- CreatureAbility.Attack.Fire Vs World = Slash, since CreatureAbility.Attack.Fire does not exist, this query becomes CreatureAbility.Attack Vs World, which throws Slash.
- CreatureAbility.Attack.Fire Vs World.Impact.Earth.Wet = Pierce

One of the advantages of an AffinityTable is that it will provide a default result for almost any query with just a handful of entries, allowing content creators to refine as needed. However, we've found that most ATs will have one data page where most cells are independent and additional data pages where most cells are inherited. 

## Editing Data

Modifying the data of any cell is simple: click on it and change the structure parameters on the properties panel. But there are a couple of handy operations that will speed up your workflows:

### Copy and Paste

To copy data from one cell and paste it into several other cells, select an initial cell, right-click, and select Copy Cell. This cell's background becomes yellow, and its data recorded.

Select an arbitrary set of destination cells by ctrl-clicking them or a square region by clicking one cell and shift-clicking on another cell. Of course, you can combine both methods, but clicking without shift or ctrl on a new cell will de-select previously selected ones. 

Paste data by right-clicking over any highlighted cell. You have two options:

1. Paste Cell: This option overrides all the data in the selected cells. 
2. Paste visible data only: This option pastes only the fields you are currently visualizing, preserving all other data already on each highlighted cell. This option is the most useful in practice. 

Any copy/paste actions will render the destination cells independent. 

## Querying Affinity Tables

You can query any affinity table asset by providing a pair of tags. The system will find data sitting at the intersection and make it available. There are two types of queries:

1. Closest match: The table will find the closest defined row and column tags. For example, if the table contains A and A.B:
    - Querying for A.C will yield the data on A. 
    - Querying for A.B.C will yield the data on A.B.

2. Exact match: The table will only return a match if it contains the exact tags in the query. In the above example, both queries would fail.

In a blueprint, add a Query Affinity Table node and Select a table from the Table input pin. Picking a table generates specialized output nodes based on its available pages. Note that you cannot connect a variable to this pin because the blueprint compiler needs to extract data from the table at compile time.

![querying](/Docs/images/query.jpg)

Once the table is selected, you can connect its structure output pins to variables that will hold the query result. If you add or remove structures (pages) on the asset, please refresh the node on your blueprint to update its outputs and re-connect as necessary.

In C++, you can directly use any of the querying functions defined on `AffinityTable.h`

## Contributions

We welcome community contributions to this project. Please read our [Contributor Guide](CONTRIBUTING.md) for important workflows and information before you make any contribution.

All contributions intentionally submitted for inclusion in this project shall comply with the Apache 2.0 license as described below:

## License

This project is licensed under the Apache License, Version 2.0 ([LICENSE](LICENSE.md) or <http://www.apache.org/licenses/LICENSE-2.0>)

