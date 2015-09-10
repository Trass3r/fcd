//
// constraints.h
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

#ifndef constraints_cpp
#define constraints_cpp

#include "dumb_allocator.h"
#include "llvm_warnings.h"
#include "not_null.h"
#include "tie_types.h"

SILENCE_LLVM_WARNINGS_BEGIN()
#include <llvm/IR/Instructions.h>
#include <llvm/Support/raw_ostream.h>
SILENCE_LLVM_WARNINGS_END()

namespace tie
{
	typedef size_t TypeVariable;
	
	struct Constraint
	{
		enum Type : char
		{
			IsEqual,
			Specializes, // adds information ("inherits from", larger bit count)
			Generalizes, // takes away information (smaller bit count)
			Conjunction,
			Disjunction,
		};
		
		Type type;
		
		Constraint(Type type)
		: type(type)
		{
		}
		
		virtual void print(llvm::raw_ostream& os) const = 0;
		void dump() const;
	};
	
	template<Constraint::Type ConstraintType>
	struct CombinatorConstraint : public Constraint
	{
		static bool classof(const Constraint* that)
		{
			return that->type == ConstraintType;
		}
		
		DumbAllocator& pool;
		PooledDeque<NOT_NULL(Constraint)> constraints;
		
		CombinatorConstraint(DumbAllocator& pool)
		: Constraint(ConstraintType), pool(pool), constraints(pool)
		{
		}
		
		template<typename Constraint, typename... TArgs>
		Constraint& constrain(TArgs&&... args)
		{
			auto constraint = pool.allocate<Constraint>(args...);
			constraints.push_back(constraint);
			return *constraint;
		}
		
		virtual void print(llvm::raw_ostream& os) const override
		{
			os << '(';
			auto iter = constraints.begin();
			if (iter != constraints.end())
			{
				os << '(';
				(*iter)->print(os);
				for (++iter; iter != constraints.end(); ++iter)
				{
					os << ") ";// << (char)ConstraintType << " (";
					switch (ConstraintType)
					{
						case Conjunction: os << '&'; break;
						case Disjunction: os << '|'; break;
						default: assert(false);
					}
					(*iter)->print(os);
				}
				os << ')';
			}
			os << ')';
		}
	};
	
	using ConjunctionConstraint = CombinatorConstraint<Constraint::Conjunction>;
	using DisjunctionConstraint = CombinatorConstraint<Constraint::Disjunction>;
	
	template<Constraint::Type ConstraintType>
	struct BinaryConstraint : public Constraint
	{
		static bool classof(const Constraint* that)
		{
			return that->type == ConstraintType;
		}
		
		TypeVariable left, right;
		
		BinaryConstraint(TypeVariable left, TypeVariable right)
		: Constraint(ConstraintType), left(left), right(right)
		{
		}
		
		virtual void print(llvm::raw_ostream& os) const override
		{
			os << '<' << left << "> ";
			switch (type)
			{
				case IsEqual: os << '='; break;
				case Specializes: os << ':'; break;
				case Generalizes: os << '!'; break;
				default: assert(false);
			}
			os << " <" << right << '>';
		}
	};
	
	using SpecializesConstraint = BinaryConstraint<Constraint::Specializes>;
	using GeneralizesConstraint = BinaryConstraint<Constraint::Generalizes>;
	using IsEqualConstraint = BinaryConstraint<Constraint::IsEqual>;
}

#endif /* constraints_cpp */
