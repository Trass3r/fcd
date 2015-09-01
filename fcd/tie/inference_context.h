//
// inference_context.h
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

#ifndef inference_context_cpp
#define inference_context_cpp

#include "dumb_allocator.h"
#include "llvm_warnings.h"
#include "pass_targetinfo.h"
#include "tie_types.h"

SILENCE_LLVM_WARNINGS_BEGIN()
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/InstVisitor.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils/MemorySSA.h>
SILENCE_LLVM_WARNINGS_END()

#include <deque>
#include <functional>
#include <type_traits>
#include <unordered_set>

namespace tie
{
	struct TypeOrValue
	{
		llvm::Value* value;
		const tie::Type* type;
		
		TypeOrValue(llvm::Value* value) : value(value), type(nullptr)
		{
		}
		
		TypeOrValue(const tie::Type* type) : value(nullptr), type(type)
		{
		}
		
		void print(llvm::raw_ostream& os) const;
		void dump() const;
	};
	
	struct Constraint
	{
		enum Type : char
		{
			Specializes = ':', // adds information ("inherits from", larger bit count)
			Generalizes = '!', // takes away information (smaller bit count)
			IsEqual = '=',
			
			Conjunction = '&',
			Disjunction = '|',
		};
		
		Type type;
		
		Constraint(Type type)
		: type(type)
		{
		}
		
		virtual void print(llvm::raw_ostream& os) const = 0;
		void dump();
	};
	
	template<Constraint::Type ConstraintType>
	struct CombinatorConstraint : public Constraint
	{
		static bool classof(const Constraint* that)
		{
			return that->type == ConstraintType;
		}
		
		DumbAllocator& pool;
		PooledDeque<Constraint*> constraints;
		
		CombinatorConstraint(DumbAllocator& pool)
		: Constraint(ConstraintType), pool(pool), constraints(pool)
		{
		}
		
		template<typename Constraint, typename... TArgs>
		Constraint* constrain(TArgs&&... args)
		{
			auto constraint = pool.allocate<Constraint>(args...);
			constraints.push_back(constraint);
			return constraint;
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
					os << ") " << (char)ConstraintType << " (";
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
		
		llvm::Value* left;
		TypeOrValue right;
		
		BinaryConstraint(llvm::Value* left, TypeOrValue right)
		: Constraint(ConstraintType), left(left), right(right)
		{
		}
		
		virtual void print(llvm::raw_ostream& os) const override
		{
			os << "value<";
			left->printAsOperand(os);
			os << "> " << (char)ConstraintType << ' ';
			right.print(os);
		}
	};
	
	using SpecializesConstraint = BinaryConstraint<Constraint::Specializes>;
	using GeneralizesConstraint = BinaryConstraint<Constraint::Generalizes>;
	using IsEqualConstraint = BinaryConstraint<Constraint::IsEqual>;
	
	class InferenceContext : public llvm::InstVisitor<InferenceContext>
	{
		const TargetInfo& target;
		llvm::MemorySSA& mssa;
		DumbAllocator pool;
		std::unordered_set<llvm::Value*> visited;
		std::deque<Constraint*> constraints;
		
		template<typename T, typename... Args>
		void constrain(Args... args)
		{
			auto constraint = pool.allocate<T>(args...);
			constraints.push_back(constraint);
		}
		
		void print(llvm::raw_ostream& os) const;
		
	public:
		InferenceContext(const TargetInfo& target, llvm::MemorySSA& ssa);
		
		void dump() const;
		
		const std::deque<Constraint*> getConstraints() const & { return constraints; }
		std::deque<Constraint*> getConstraints() && { return std::move(constraints); }
		
		static const tie::Type& getAny();
		static const tie::Type& getBoolean();
		const tie::Type& getNum(unsigned width = 0);
		const tie::Type& getSint(unsigned width = 0);
		const tie::Type& getUint(unsigned width = 0);
		const tie::Type& getFunctionPointer();
		const tie::Type& getBasicBlockPointer();
		const tie::Type& getPointer();
		const tie::Type& getPointerTo(const tie::Type& pointee);
		
		void visitICmpInst(llvm::ICmpInst& inst, llvm::Value* constraintKey = nullptr);
		void visitAllocaInst(llvm::AllocaInst& inst, llvm::Value* constraintKey = nullptr);
		void visitLoadInst(llvm::LoadInst& inst, llvm::Value* constraintKey = nullptr);
		void visitStoreInst(llvm::StoreInst& inst, llvm::Value* constraintKey = nullptr);
		void visitGetElementPtrInst(llvm::GetElementPtrInst& inst, llvm::Value* constraintKey = nullptr);
		void visitPHINode(llvm::PHINode& inst, llvm::Value* constraintKey = nullptr);
		void visitSelectInst(llvm::SelectInst& inst, llvm::Value* constraintKey = nullptr);
		void visitCallInst(llvm::CallInst& inst, llvm::Value* constraintKey = nullptr);
		
		void visitBinaryOperator(llvm::BinaryOperator& inst, llvm::Value* constraintKey = nullptr);
		void visitCastInst(llvm::CastInst& inst, llvm::Value* constraintKey = nullptr);
		void visitTerminatorInst(llvm::TerminatorInst& inst, llvm::Value* constraintKey = nullptr);
		void visitInstruction(llvm::Instruction& inst, llvm::Value* constraintKey = nullptr);
		
		void visitConstant(llvm::Constant& constant);
		void visit(llvm::Instruction& inst, llvm::Value* constraintKey);
		void visit(llvm::Instruction& inst);
		using llvm::InstVisitor<InferenceContext>::visit;
	};
}

#endif /* inference_context_cpp */
