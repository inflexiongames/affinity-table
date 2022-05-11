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

#include "AffinityTable.h"
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include <functional>

/**
 * Defines a node of an m-ary tree. It holds a tag, which can be
 * searched against the asset's rows or columns to get a corresponding memory location index.
 *
 * We own our children, but we don't own the structured memory they point to.
 */
class FAffinityTableNode
{
public:
	/** Shorthand for a shared node pointer */
	using NodeSharedPtr = TSharedPtr<FAffinityTableNode>;

	/** Shorthand for an iterator over an array of nodes (with const version) */
	using NodeSharedPtrIt = TArray<NodeSharedPtr>::TIterator;
	using NodeSharedPtrConstIt = TArray<NodeSharedPtr>::TConstIterator;

	/** Callback for furnishing new inserted nodes */
	using NewNodeCallback = std::function<void(TWeakPtr<FAffinityTableNode>)>;

	/** Callback for generating indexes out of tags */
	using IndexGenerator = std::function<UAffinityTable::TagIndex(const FGameplayTag& Tag)>;

	/**
	 * Creates a new node
	 * @param InTag Gameplay tag linked to this node
	 * @param InTagIndex Index in the affinity table represented by this tag, for querying speed purposes
	 * @param InParent Optionally, our parent
	 * @param InColor Color for this node
	 */
	FAffinityTableNode(const FGameplayTag& InTag, UAffinityTable::TagIndex InTagIndex = UAffinityTable::InvalidIndex, FAffinityTableNode* InParent = nullptr, const FLinearColor& InColor = FLinearColor::White);

	/** Node destructor */
	~FAffinityTableNode();

	/**
	 * Inserts tags in the provided array from tail to head (FIFO), creating nodes
	 * along the way as necessary.
	 *
	 * This function is designed to work with tags as provided by FGameplayTag::GetGameplayTagParents
	 * (e.g., [a.b.c, a.b, a])
	 *
	 * @param InOutTagQueue Queue of tags to insert. This array will be emptied as part of the process
	 * @param OnIndexForTag Lambda to call when we need to generate an index for a given tag
	 * @param OnNewNode Lambda to call every time a new node is inserted
	 */
	void Insert(TArray<FGameplayTag>& InOutTagQueue, IndexGenerator OnIndexForTag, NewNodeCallback OnNewNode);

	/**
	 * Unlinks this node from our children, if it belongs to us.
	 * @param ChildNode Node to remove 
	 */
	void RemoveChild(const FAffinityTableNode* ChildNode);

	/**
	 * Removes this node from its parent tree. This operation may cause the node to be
	 * deleted along with its children when the last transient reference pointer goes out of scope. 
	 */
	void Unlink();

	/**
	 * Convenience iterator for our children
	 */
	FORCEINLINE NodeSharedPtrIt ChildIterator()
	{
		return Children.CreateIterator();
	}

	/** Const version of our child iterator */
	FORCEINLINE const NodeSharedPtrConstIt ChildIterator() const
	{
		return Children.CreateConstIterator();
	}

	/**
	 * Exactly matches the provided tag against our contained tag
	 */
	FORCEINLINE bool MatchesExact(const FGameplayTag& InTag) const
	{
		return Tag.MatchesTagExact(InTag);
	}

	/**
	 * Tells if this node is collapsed
	 */
	FORCEINLINE bool IsCollapsed() const
	{
		return Collapsed;
	}

	/**
	 * Provides read-only access to our tag
	 */
	FORCEINLINE const FGameplayTag& GetTag() const
	{
		return Tag;
	}

	/**
	 * Access to our tag index
	 */
	FORCEINLINE UAffinityTable::TagIndex GetTagIndex() const
	{
		return TagIndex;
	}

	/**
	 * Assigns the value of our collapsed flag
	 */
	FORCEINLINE void SetCollapsed(bool IsCollapsed)
	{
		Collapsed = IsCollapsed;
	}

	/**
	 * Returns true if this node has one or more children
	 */
	FORCEINLINE bool HasChildren() const
	{
		return Children.Num() > 0;
	}

	/**
	 * Tells if the parent of this node exists, and has a valid tag
	 */
	FORCEINLINE bool HasValidParent() const
	{
		return Parent && Parent->GetTag().IsValid();
	}

	/**
	 * Read-only access to our parent
	 */
	FORCEINLINE const FAffinityTableNode* GetParent() const
	{
		return Parent;
	}

	/**
	 * Access to the color required to render this node
	 */
	FORCEINLINE const FLinearColor& GetColor() const
	{
		return TagColor;
	}

	/**
	 * Read/write access to our color
	 */
	FORCEINLINE FLinearColor& GetColorRef()
	{
		return TagColor;
	}

private:
	/** The tag that identifies the content of this node */
	FGameplayTag Tag;

	/** The cached index of this tag in the editor */
	UAffinityTable::TagIndex TagIndex;

	/** Parent node */
	FAffinityTableNode* Parent;

	/** Color for this node and its immediate children */
	FLinearColor TagColor;

	/** whether this node is collapsed or open */
	bool Collapsed;

	/** This node's children */
	TArray<NodeSharedPtr> Children;
};

/**
 * Base class for node walkers, functional objects that gather, add, or modify data in our tree.
 * We provide templated smart pointers and naked pointer implementations, since different
 * parts of the engine (particularly UI components) are opinionated in different ways. You'll
 * want to implement either given the situation, but usually not both. 
 */
class IAffinityTableNodeWalker
{
public:
	/**
	 * Walks the tree in pre-order mode starting with the provided node
	 * @param InNode Root node to start walk recursion
	 */
	bool Walk(TWeakPtr<FAffinityTableNode> InNode);
	bool Walk(FAffinityTableNode* InNode);

	/**
	 * Walks the tree in pre-order mode ignoring the provided node
	 * @param InNode Node to start recursion from
	 */
	void WalkChildren(TWeakPtr<FAffinityTableNode> InNode);
	void WalkChildren(FAffinityTableNode* InNode);

protected:
	/**
	 * Walks in pre-order mode. Stops recursion depth if this function returns false
	 * (but will still finish traversing this level's children)
	 */
	virtual bool Visit(TWeakPtr<FAffinityTableNode>& InNode)
	{
		check(0);
		return false;
	}

	virtual bool Visit(FAffinityTableNode* InNode)
	{
		check(0);
		return false;
	}
};

/**
 * Generic, convenience lambda operator over our tree.
 */
class FLambdaWalker : public IAffinityTableNodeWalker
{
public:
	/** Node callback. Return true to continue recursion */
	using NodeCallback = std::function<bool(TWeakPtr<FAffinityTableNode>)>;

	/** Node callback for references */
	using NodeCallbackPtr = std::function<bool(FAffinityTableNode*)>;

	/**
	 * Creates a new walker
	 * @param InCallback Function to issue for each node in the tree
	 */
	FLambdaWalker(NodeCallback InCallback, NodeCallbackPtr InCallbackPtr = nullptr) :
		Callback(InCallback),
		CallbackPtr(InCallbackPtr){};

	/**
	 * Creates a walker and starts node recursion in a single step
	 * @param InCallback Function to execute for each node
	 * @param InStartNode first recursion node
	 */
	FLambdaWalker(NodeCallback InCallback, TWeakPtr<FAffinityTableNode> InStartNode) :
		Callback(InCallback),
		CallbackPtr(nullptr)
	{
		Walk(InStartNode);
	}

	FLambdaWalker(NodeCallbackPtr InCallback, FAffinityTableNode* InStartNode) :
		Callback(nullptr),
		CallbackPtr(InCallback)
	{
		Walk(InStartNode);
	}

	virtual ~FLambdaWalker()
	{
	}

protected:
	virtual bool Visit(TWeakPtr<FAffinityTableNode>& InNode) override
	{
		return Callback(InNode);
	}

	virtual bool Visit(FAffinityTableNode* InNode) override
	{
		return CallbackPtr(InNode);
	}

private:
	NodeCallback Callback;
	NodeCallbackPtr CallbackPtr;
};
