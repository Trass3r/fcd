//
// solver.cpp
// Copyright (C) 2015 Félix Cloutier.
// All Rights Reserved.
//
// This file is part of fcd.
// 
// fcd is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// fcd is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with fcd.  If not, see <http://www.gnu.org/licenses/>.
//

#include "solver.h"

SILENCE_LLVM_WARNINGS_BEGIN()
#include <llvm/Support/raw_os_ostream.h>
SILENCE_LLVM_WARNINGS_END()

#include <iostream>

using namespace llvm;
using namespace std;
using namespace tie;

namespace
{
	struct ConstraintOrdering
	{
		bool operator()(Constraint* a, Constraint* b)
		{
			return a->type < b->type;
		}
	};
	
	template<typename T>
	struct TemporarySwap
	{
		T& target;
		T storage;
		
		TemporarySwap(T& target, T value)
		: target(target), storage(value)
		{
			swap(target, storage);
		}
		
		~TemporarySwap()
		{
			swap(target, storage);
		}
	};
	
	template<typename TOutputCollection, typename TInputCollection, typename TSort>
	TOutputCollection sorted(const TInputCollection& that, TSort&& sortStrategy)
	{
		TOutputCollection result;
		copy(that.begin(), that.end(), back_inserter(result));
		sort(result.begin(), result.end(), sortStrategy);
		return result;
	}
	
	template<typename TMap>
	void update(TMap& into, const TMap& reference)
	{
		for (const auto& pair : reference)
		{
			auto insertResult = into.insert(pair);
			if (!insertResult.second)
			{
				insertResult.first->second = pair.second;
			}
		}
	}
	
	void printGroup(raw_ostream& os, const VariableReferenceGroup& ref)
	{
		os << '<';
		auto iter = ref.begin();
		auto end = ref.end();
		if (iter != end)
		{
			os << *iter;
			++iter;
			while (iter != end)
			{
				os << ", " << *iter;
				++iter;
			}
		}
		os << '>';
	}
}

#pragma mark -
SolverConstraints::SolverConstraints(const InferenceContext::ConstraintList& constraints)
: current(constraints.begin()), end(constraints.end())
{
}

Constraint* SolverConstraints::pop()
{
	if (current == end)
	{
		return nullptr;
	}
	
	auto result = *current;
	current++;
	return result;
}

#pragma mark - Solver State
SolverState::SolverState(const SolverConstraints& constraints, NOT_NULL(SolverState) parent)
: constraints(constraints), referenceGroups(parent->referenceGroups), specializations(parent->specializations), parent(parent)
{
}

SolverState::SolverState(const InferenceContext::ConstraintList& constraints)
: constraints(constraints), parent(nullptr)
{
}

bool SolverState::tightenOneBound(tie::UnifiedReference target, const tie::Type &newBound, TypeOrdering ordering, BoundMapSelector boundSelector, BoundMapSelector oppositeSelector)
{
	if (NOT_NULL(const tie::Type)* oppositeBound = chainFind(oppositeSelector, target))
	{
		// Make sure that new suggested bound is correctly related to the opposite bound.
		if (!(newBound.*ordering)(**oppositeBound))
		{
			return false;
		}
	}
	
	bool updateBound = true;
	if (NOT_NULL(const tie::Type)* currentBound = chainFind(boundSelector, target))
	{
		// Update bound only if new type is more restrictive.
		updateBound = ((*currentBound)->*ordering)(newBound);
	}
	
	if (updateBound)
	{
		// Insert or update value.
		auto result = (this->*boundSelector).insert({target, &newBound});
		if (!result.second)
		{
			result.first->second = &newBound;
		}
	}
	return true;
}

bool SolverState::tightenOneGeneralBound(tie::UnifiedReference target, const tie::Type& newLowerBound)
{
	return tightenOneBound(target, newLowerBound, &Type::isGeneralizationOf, &SolverState::mostGeneralBounds, &SolverState::mostSpecificBounds);
}

bool SolverState::tightenOneSpecificBound(tie::UnifiedReference target, const tie::Type& newUpperBound)
{
	return tightenOneBound(target, newUpperBound, &Type::isSpecializationOf, &SolverState::mostSpecificBounds, &SolverState::mostGeneralBounds);
}

bool SolverState::tightenGeneralBound(tie::UnifiedReference target, const tie::Type &newLowerBound)
{
	bool result = tightenOneGeneralBound(target, newLowerBound);
	if (result)
	{
		for (const auto& pair : specializations)
		{
			if (pair.second == target && !tightenOneGeneralBound(pair.first, newLowerBound))
			{
				return false;
			}
		}
	}
	return result;
}

bool SolverState::tightenSpecificBound(tie::UnifiedReference target, const tie::Type &newUpperBound)
{
	bool result = tightenOneSpecificBound(target, newUpperBound);
	if (result)
	{
		for (const auto& pair : specializations)
		{
			if (pair.first == target && !tightenOneSpecificBound(pair.second, newUpperBound))
			{
				return false;
			}
		}
	}
	return result;
}

bool SolverState::addSpecializationRelationship(UnifiedReference subtype, UnifiedReference inheritsFrom)
{
	auto inserted = make_pair(subtype, inheritsFrom);
	auto result = specializations.insert(inserted);
	if (result.second)
	{
		if (NOT_NULL(const Type)* boundType = chainFind(&SolverState::boundTypes, subtype))
		{
			tightenSpecificBound(inheritsFrom, **boundType);
		}
		else if (NOT_NULL(const Type)* boundType = chainFind(&SolverState::boundTypes, inheritsFrom))
		{
			tightenGeneralBound(subtype, **boundType);
		}
		
		for (const auto& existing : specializations)
		{
			if (inserted.second == existing.first && !addSpecializationRelationship(inserted.first, existing.second))
			{
				return false;
			}
		}
	}
	
	return true;
}

bool SolverState::unifyReferences(UnifiedReference unifyTo, TypeVariable newElement)
{
	auto& unifyGroup = referenceGroups.at(unifyTo);
	auto iter = unificationMap.find(newElement);
	if (iter == unificationMap.end())
	{
		unifyGroup.push_back(newElement);
		unificationMap.insert({newElement, unifyTo});
	}
	else
	{
		if (iter->second != unifyTo)
		{
			if (auto boundType = chainFind(&SolverState::boundTypes, iter->second))
			if (!bindType(unifyTo, **boundType))
			{
				return false;
			}
			
			if (auto general = chainFind(&SolverState::mostGeneralBounds, iter->second))
			if (!tightenGeneralBound(unifyTo, **general))
			{
				return false;
			}
			
			if (auto specific = chainFind(&SolverState::mostSpecificBounds, iter->second))
			if (!tightenSpecificBound(unifyTo, **specific))
			{
				return false;
			}
			
			// Specializations should have been adjusted now that the bounds have been tightened.
			
			auto& moveFrom = referenceGroups.at(iter->second);
			for (TypeVariable tv : moveFrom)
			{
				unificationMap[tv] = unifyTo;
			}
			unifyGroup.insert(unifyGroup.end(), moveFrom.begin(), moveFrom.end());
			moveFrom.clear();
		}
	}
	return true;
}

bool SolverState::bindType(tie::UnifiedReference type, const tie::Type &bound)
{
	auto result = boundTypes.insert({type, &bound});
	return result.second || result.first->second == &bound;
}

UnifiedReference SolverState::getUnifiedReference(TypeVariable tv)
{
	if (const UnifiedReference* ref = chainFind(&SolverState::unificationMap, tv))
	{
		return *ref;
	}
	
	size_t index = referenceGroups.size();
	referenceGroups.emplace_back();
	referenceGroups.back().push_back(tv);
	unificationMap.insert({tv, index});
	return index;
}

const tie::Type* SolverState::getGeneralBound(UnifiedReference ref) const
{
	auto iter = mostGeneralBounds.find(ref);
	if (iter == mostGeneralBounds.end())
	{
		return nullptr;
	}
	return iter->second;
}

const tie::Type* SolverState::getSpecificBound(UnifiedReference ref) const
{
	auto iter = mostSpecificBounds.find(ref);
	if (iter == mostSpecificBounds.end())
	{
		return nullptr;
	}
	return iter->second;
}

Constraint* SolverState::getNextConstraint()
{
	return constraints.pop();
}

SolverState SolverState::createSubState(const SolverConstraints& constraints)
{
	return SolverState(constraints, this);
}

void SolverState::commit()
{
	assert(parent != nullptr);
	parent->constraints = constraints;
	parent->referenceGroups = referenceGroups;
	update(parent->unificationMap, unificationMap);
	update(parent->mostGeneralBounds, mostGeneralBounds);
	update(parent->mostSpecificBounds, mostSpecificBounds);
	parent->specializations.swap(specializations);
}

void SolverState::dump() const
{
	raw_os_ostream rerr(cerr);
	rerr << "Non-recursive dump\n"; // does not take into account parent SolverState
	
	rerr << "\nBounds:\n";
	for (UnifiedReference i = 0; i < referenceGroups.size(); ++i)
	{
		auto general = mostGeneralBounds.find(i);
		auto specific = mostSpecificBounds.find(i);
		if (general != mostGeneralBounds.end() || specific != mostSpecificBounds.end())
		{
			rerr << "  ";
			if (specific != mostSpecificBounds.end())
			{
				specific->second->print(rerr);
				rerr << " : ";
			}
			
			printGroup(rerr, referenceGroups[i]);
			
			if (general != mostGeneralBounds.end())
			{
				rerr << " : ";
				general->second->print(rerr);
			}
			rerr << '\n';
		}
	}
}

#pragma mark - Solver
Solver::Solver(const InferenceContext& context)
: context(context)
, constraints(sorted<InferenceContext::ConstraintList>(context.getConstraints(), ConstraintOrdering()))
, rootState(constraints)
, currentState(&rootState)
{
	for (TypeVariable v = 0; v < context.getVariableCount(); v++)
	{
		if (const Type* type = context.getBoundType(v))
		{
			auto ref = currentState->getUnifiedReference(v);
			currentState->bindType(ref, *type);
		}
	}
}

bool Solver::process(const tie::Constraint &constraint)
{
	switch (constraint.type)
	{
		case Constraint::IsEqual: return process(cast<IsEqualConstraint>(constraint));
		case Constraint::Specializes: return process(cast<SpecializesConstraint>(constraint));
		case Constraint::Generalizes: return process(cast<GeneralizesConstraint>(constraint));
		case Constraint::Conjunction: return process(cast<ConjunctionConstraint>(constraint));
		case Constraint::Disjunction: return process(cast<DisjunctionConstraint>(constraint));
			
		default:
			assert(false);
			return false;
	}
}

bool Solver::process(const IsEqualConstraint& constraint)
{
	auto key = currentState->getUnifiedReference(constraint.left);
	return currentState->unifyReferences(key, constraint.right);
}

bool Solver::process(const SpecializesConstraint& constraint)
{
	return process(GeneralizesConstraint(constraint.right, constraint.left));
}

bool Solver::process(const GeneralizesConstraint& constraint)
{
	auto leftKey = currentState->getUnifiedReference(constraint.left);
	auto rightKey = currentState->getUnifiedReference(constraint.right);
	return currentState->addSpecializationRelationship(leftKey, rightKey);
}

bool Solver::process(const ConjunctionConstraint& constraint)
{
	auto constraintList = sorted<InferenceContext::ConstraintList>(constraint.constraints, ConstraintOrdering());
	SolverConstraints constraints(constraintList);
	SolverState child = currentState->createSubState(constraints);

	TemporarySwap<SolverState*> swap(currentState, &child);
	if (solve())
	{
		currentState->commit();
		return true;
	}
	else
	{
		return false;
	}
}

bool Solver::process(const DisjunctionConstraint& constraint)
{
	llvm_unreachable("implement me");
}

bool Solver::solve()
{
	while (Constraint* constraint = currentState->getNextConstraint())
	{
		if (!process(*constraint))
		{
			return false;
		}
	}
	
	return true;
}

pair<const tie::Type*, const tie::Type*> Solver::getInferredType(const llvm::Value &value) const
{
	TypeVariable variable = context.getVariableForValue(value);
	auto unified = currentState->getUnifiedReference(variable);
	return make_pair(currentState->getGeneralBound(unified), currentState->getSpecificBound(unified));
}
