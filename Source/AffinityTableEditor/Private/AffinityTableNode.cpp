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
#include "AffinityTableNode.h"
#include "AffinityTableStyles.h"

// FAffinityTableNode
//////////////////////////////////////////////////////////////////////////

FAffinityTableNode::FAffinityTableNode(const FGameplayTag& InTag, UAffinityTable::TagIndex InTagIndex, FAffinityTableNode* InParent /* = nullptr */, const FLinearColor& InColor /* = FLinearColor::White */)
	: Tag(InTag)
	, TagIndex(InTagIndex)
	, Parent(InParent)
	, TagColor(InColor)
	, Collapsed(InParent != nullptr)
{
}

void FAffinityTableNode::Insert(TArray<FGameplayTag>& InOutTagQueue, IndexGenerator OnIndexForTag, NewNodeCallback OnNewNode)
{
	if (InOutTagQueue.Num())
	{
		FGameplayTag NodeTag = InOutTagQueue.Pop();
		NodeSharedPtr* Child = Children.FindByPredicate([NodeTag](const NodeSharedPtr& Node) { return Node.IsValid() && Node->MatchesExact(NodeTag); });
		if (Child)
		{
			(*Child)->Insert(InOutTagQueue, OnIndexForTag, OnNewNode);
		}
		else
		{
			const FLinearColor& ChildColor = HasChildren() ? Children[0]->GetColor() : FAffinityTableStyles::PickColor();
			NodeSharedPtr NewNode = MakeShareable(new FAffinityTableNode(NodeTag, OnIndexForTag(NodeTag), this, ChildColor));
			Children.Add(NewNode);
			if (OnNewNode)
			{
				OnNewNode(NewNode);
			}
			Children.Sort([](const FAffinityTableNode::NodeSharedPtr& A, const FAffinityTableNode::NodeSharedPtr& B) { return A->GetTag() < B->GetTag(); });
			NewNode->Insert(InOutTagQueue, OnIndexForTag, OnNewNode);
		}
	}
}

void FAffinityTableNode::RemoveChild(const FAffinityTableNode* ChildNode)
{
	check(ChildNode);
	const FGameplayTag& ChildTag = ChildNode->GetTag();
	int32 ChildIndex = Children.IndexOfByPredicate([ChildTag](const NodeSharedPtr& Node) { return Node->GetTag() == ChildTag; });
	if (ChildIndex != INDEX_NONE)
	{
		Children.RemoveAt(ChildIndex);
	}
}

void FAffinityTableNode::Unlink()
{
	if (Parent)
	{
		Parent->RemoveChild(this);
	}
}

// Walkers
//////////////////////////////////////////////////////////////////////////

bool IAffinityTableNodeWalker::Walk(TWeakPtr<FAffinityTableNode> InNode)
{
	if (Visit(InNode))
	{
		WalkChildren(InNode);
		return true;
	}
	return false;
}

bool IAffinityTableNodeWalker::Walk(FAffinityTableNode* InNode)
{
	check(InNode);
	if (Visit(InNode))
	{
		WalkChildren(InNode);
		return true;
	}
	return false;
}

void IAffinityTableNodeWalker::WalkChildren(TWeakPtr<FAffinityTableNode> InNode)
{
	check(InNode.IsValid());
	for (TArray<FAffinityTableNode::NodeSharedPtr>::TIterator it = InNode.Pin()->ChildIterator(); it; ++it)
	{
		Walk(*it);
	}
}

void IAffinityTableNodeWalker::WalkChildren(FAffinityTableNode* InNode)
{
	check(InNode);
	for (TArray<FAffinityTableNode::NodeSharedPtr>::TIterator it = InNode->ChildIterator(); it; ++it)
	{
		Walk((*it).Get());
	}
}
