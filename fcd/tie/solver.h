//
// solver.h
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

#ifndef solver_cpp
#define solver_cpp

#include "inference_context.h"
#include "llvm_warnings.h"
#include "not_null.h"
#include "tie_types.h"

SILENCE_LLVM_WARNINGS_BEGIN()
#include <llvm/ADT/SmallVector.h>
SILENCE_LLVM_WARNINGS_END()

#include <algorithm>
#include <deque>
#include <set>
#include <type_traits>
#include <unordered_map>

namespace tie
{
#ifdef DEBUG
	typedef std::vector<TypeVariable> VariableReferenceGroup;
#else
	typedef llvm::SmallVector<TypeVariable, 2> VariableReferenceGroup;
#endif
	
	typedef VariableReferenceGroup* UnifiedReference;
	
	class SolverConstraints
	{
		InferenceContext::ConstraintList::const_iterator current;
		std::deque<Constraint*>::const_iterator end;
		
	public:
		explicit SolverConstraints(const InferenceContext::ConstraintList& constraints);
		SolverConstraints(const SolverConstraints&) = default;
		SolverConstraints(SolverConstraints&&) = default;
		
		SolverConstraints& operator=(const SolverConstraints&) = default;
		SolverConstraints& operator=(SolverConstraints&&) = default;
		
		Constraint* pop();
	};
	
	class SolverState
	{
	private:
		SolverConstraints constraints;
		std::deque<VariableReferenceGroup> referenceGroups;
		std::unordered_map<TypeVariable, UnifiedReference> unificationMap;
		std::unordered_map<UnifiedReference, NOT_NULL(const Type)> boundTypes;
		std::unordered_map<UnifiedReference, NOT_NULL(const Type)> mostGeneralBounds;
		std::unordered_map<UnifiedReference, NOT_NULL(const Type)> mostSpecificBounds;
		std::set<std::pair<UnifiedReference, UnifiedReference>> specializations; // pair.first extends pair.second
		SolverState* parent;
		
		SolverState(const SolverConstraints& constraints, NOT_NULL(SolverState) parent);
		
		template<typename MapLocator, typename KeyType>
		auto chainFind(MapLocator locator, KeyType key)
			-> typename std::remove_reference<decltype(this->*locator)>::type::mapped_type*;
		
		template<typename MapLocator, typename KeyType>
		auto chainFind(MapLocator locator, KeyType key) const
			-> const typename std::remove_reference<decltype(this->*locator)>::type::mapped_type*;
		
		typedef std::unordered_map<UnifiedReference, NOT_NULL(const Type)> SolverState::*BoundMapSelector;
		typedef bool (Type::*TypeOrdering)(const Type&) const;
		
		bool tightenOneBound(UnifiedReference target, const Type& newBound, TypeOrdering ordering, BoundMapSelector bound, BoundMapSelector opposite);
		bool tightenOneGeneralBound(UnifiedReference target, const Type& newLowerBound);
		bool tightenOneSpecificBound(UnifiedReference target, const Type& newUpperBound);
		
	public:
		SolverState(const InferenceContext::ConstraintList& constraints);
		SolverState(SolverState&&) = default;
		
		bool tightenGeneralBound(UnifiedReference target, const Type& newLowerBound);
		bool tightenSpecificBound(UnifiedReference target, const Type& newUpperBound);
		bool addSpecializationRelationship(UnifiedReference subtype, UnifiedReference inheritsFrom);
		bool unifyReferences(UnifiedReference a, TypeVariable b);
		bool bindType(UnifiedReference type, const Type& bound);
		UnifiedReference getUnifiedReference(TypeVariable variable);
		
		const Type* getGeneralBound(UnifiedReference ref) const;
		const Type* getSpecificBound(UnifiedReference ref) const;
		
		Constraint* getNextConstraint();
		
		SolverState createSubState(const SolverConstraints& constraints);
		void commit();
		
		void dump() const;
	};
	
	class Solver
	{
		const InferenceContext& context;
		InferenceContext::ConstraintList constraints;
		SolverState rootState;
		SolverState* currentState;
		
		bool process(const Constraint& constraint);
		bool process(const IsEqualConstraint& equal);
		bool process(const SpecializesConstraint& specializes);
		bool process(const GeneralizesConstraint& generalizes);
		bool process(const ConjunctionConstraint& conj);
		bool process(const DisjunctionConstraint& disj);
		
	public:
		Solver(const InferenceContext& context);
		
		bool solve();
		
		std::pair<const Type*, const Type*> getInferredType(const llvm::Value& value) const;
	};
}

#pragma mark - Templates Implementation
template<typename MapLocator, typename KeyType>
auto tie::SolverState::chainFind(MapLocator locator, KeyType key)
	-> typename std::remove_reference<decltype(this->*locator)>::type::mapped_type*
{
	auto current = this;
	while (current != nullptr)
	{
		auto& map = current->*locator;
		auto iter = map.find(key);
		if (iter != map.end())
		{
			return &iter->second;
		}
		current = current->parent;
	}
	return nullptr;
}

template<typename MapLocator, typename KeyType>
auto tie::SolverState::chainFind(MapLocator locator, KeyType key) const
	-> const typename std::remove_reference<decltype(this->*locator)>::type::mapped_type*
{
	auto current = this;
	while (current != nullptr)
	{
		auto& map = current->*locator;
		auto iter = map.find(key);
		if (iter != map.end())
		{
			return &iter->second;
		}
		current = current->parent;
	}
	return nullptr;
}

#endif /* solver_cpp */
