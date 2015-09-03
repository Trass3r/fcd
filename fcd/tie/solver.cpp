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
	
	template<typename TCollection, typename TSort>
	TCollection sorted(const TCollection& that, TSort&& sortStrategy)
	{
		TCollection copy = that;
		sort(copy.begin(), copy.end(), sortStrategy);
		return copy;
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
	
	enum class UnificationResult
	{
		KeyNotFound,
		KeyFoundUnifyTo
	};
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
SolverState::SolverState(NOT_NULL(SolverState) parent)
: constraints(parent->constraints), parent(parent)
{
}

SolverState::SolverState(const InferenceContext::ConstraintList& constraints)
: constraints(constraints), parent(nullptr)
{
}

void SolverState::unifyReferences(UnifiedReference a, TypeVariable b)
{
	llvm_unreachable("implement me");
}

SolverState::UnifiedReference SolverState::getUnifiedReference(TypeVariable tv) const
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

void SolverState::commit()
{
	assert(parent != nullptr);
	parent->constraints = constraints;
	update(parent->unificationMap, unificationMap);
	update(parent->generalizationRelations, generalizationRelations);
	update(parent->lowerBounds, lowerBounds);
	update(parent->upperBounds, upperBounds);
}

#pragma mark - Solver
Solver::Solver(const InferenceContext& context)
: context(context), constraints(sorted(context.getConstraints(), ConstraintOrdering())), rootState(constraints)
{
	currentState = &rootState;
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
	llvm_unreachable("implement me");
}

bool Solver::process(const SpecializesConstraint& constraint)
{
	llvm_unreachable("implement me");
}

bool Solver::process(const GeneralizesConstraint& constraint)
{
	llvm_unreachable("implement me");
}

bool Solver::process(const ConjunctionConstraint& constraint)
{
	llvm_unreachable("implement me");
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
