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
#include "not_null.h"
#include "tie_types.h"

#include <algorithm>
#include <deque>
#include <type_traits>
#include <unordered_map>

namespace tie
{
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
	public:
		struct UnifiedReference
		{
			TypeVariable tv;
			
			explicit UnifiedReference(TypeVariable tv)
			: tv(tv)
			{
			}
			
			operator TypeVariable() const { return tv; }
		};
		
	private:
		SolverConstraints constraints;
		std::unordered_map<TypeVariable, UnifiedReference> unificationMap;
		std::unordered_map<TypeVariable, TypeVariable> generalizationRelations;
		std::unordered_map<TypeVariable, const tie::Type*> lowerBounds;
		std::unordered_map<TypeVariable, const tie::Type*> upperBounds;
		SolverState* parent;
		
		SolverState(NOT_NULL(SolverState) parent);
		
		template<typename MapLocator>
		auto chainFind(MapLocator locator, TypeVariable key)
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
			}
			return nullptr;
		}
		
		template<typename MapLocator>
		auto chainFind(MapLocator locator, TypeVariable key) const
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
			}
			return nullptr;
		}
		
	public:
		SolverState(const InferenceContext::ConstraintList& constraints);
		
		bool raiseLowerBound(UnifiedReference target, const tie::Type* newLowerBound);
		bool lowerUpperBound(UnifiedReference target, const tie::Type* newUpperBound);
		bool bindType(UnifiedReference target, const tie::Type* type);
		void unifyReferences(UnifiedReference a, TypeVariable b);
		
		UnifiedReference getUnifiedReference(TypeVariable variable) const;
		const tie::Type* getLowerBound(UnifiedReference ref) const;
		const tie::Type* getUpperBound(UnifiedReference ref) const;
		
		Constraint* getNextConstraint();
		
		void commit();
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
		
		std::pair<const tie::Type*, const tie::Type*> getInferredType(const llvm::Value& value) const;
	};
}

#endif /* solver_cpp */
