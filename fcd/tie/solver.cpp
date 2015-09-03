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
: constraints(constraints), parent(parent)
{
}

SolverState::SolverState(const InferenceContext::ConstraintList& constraints)
: constraints(constraints), parent(nullptr)
{
}

bool SolverState::tightenLowerBound(tie::UnifiedReference target, const tie::Type *newLowerBound)
{
	llvm_unreachable("implement me");
}

bool SolverState::tightenUpperBound(tie::UnifiedReference target, const tie::Type *newUpperBound)
{
	llvm_unreachable("implement me");
}

bool SolverState::addGeneralizationRelationship(tie::UnifiedReference a, tie::UnifiedReference b)
{
	llvm_unreachable("implement me");
}

bool SolverState::unifyReferences(UnifiedReference a, TypeVariable b)
{
	if (const auto** bound = chainFind(&SolverState::lowerBounds, b))
	{
		if (!tightenLowerBound(a, *bound))
		{
			return false;
		}
	}
	
	if (const auto** bound = chainFind(&SolverState::upperBounds, b))
	{
		if (!tightenUpperBound(a, *bound))
		{
			return false;
		}
	}
	
	auto result = unificationMap.insert({b, a});
	assert(result.second);
	return result.second;
}

UnifiedReference SolverState::getUnifiedReference(TypeVariable tv) const
{
	if (const UnifiedReference* ref = chainFind(&SolverState::unificationMap, tv))
	{
		return *ref;
	}
	return UnifiedReference(tv);
}

const tie::Type* SolverState::getLowerBound(UnifiedReference ref) const
{
	auto iter = lowerBounds.find(ref);
	if (iter == lowerBounds.end())
	{
		return nullptr;
	}
	return iter->second;
}

const tie::Type* SolverState::getUpperBound(UnifiedReference ref) const
{
	auto iter = upperBounds.find(ref);
	if (iter == upperBounds.end())
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
	update(parent->unificationMap, unificationMap);
	update(parent->generalizations, generalizations);
	update(parent->lowerBounds, lowerBounds);
	update(parent->upperBounds, upperBounds);
}

#pragma mark - Solver
Solver::Solver(const InferenceContext& context)
: context(context)
, constraints(sorted<InferenceContext::ConstraintList>(context.getConstraints(), ConstraintOrdering()))
, rootState(constraints)
, currentState(&rootState)
{
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
	if (auto boundType = context.getBoundType(constraint.left))
	{
		assert(context.getBoundType(constraint.right) == nullptr);
		auto rightKey = currentState->getUnifiedReference(constraint.right);
		return currentState->tightenUpperBound(rightKey, boundType);
	}
	else if (auto boundType = context.getBoundType(constraint.right))
	{
		auto leftKey = currentState->getUnifiedReference(constraint.left);
		return currentState->tightenLowerBound(leftKey, boundType);
	}
	else
	{
		auto leftKey = currentState->getUnifiedReference(constraint.left);
		auto rightKey = currentState->getUnifiedReference(constraint.right);
		return currentState->addGeneralizationRelationship(leftKey, rightKey);
	}
}

bool Solver::process(const ConjunctionConstraint& constraint)
{
	auto constraintList = sorted<InferenceContext::ConstraintList>(constraint.constraints, ConstraintOrdering());
	SolverConstraints constraints(constraintList);
	SolverState child = currentState->createSubState(constraints);
	
	TemporarySwap<SolverState*> swap(currentState, &child);
	return solve();
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
	return make_pair(currentState->getLowerBound(unified), currentState->getUpperBound(unified));
}
