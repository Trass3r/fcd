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

#include "constraints.h"
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
#include <unordered_map>
#include <unordered_set>

namespace tie
{
	class InferenceContext : public llvm::InstVisitor<InferenceContext>
	{
		union TypeOrValue
		{
			// The discriminant is whether an entry for `value` exists in `valueVariables`.
			const llvm::Value* value;
			const tie::Type* type;
			
			TypeOrValue(const llvm::Value* value)
			: value(value)
			{
			}
			
			TypeOrValue(const tie::Type* type)
			: type(type)
			{
			}
		};
		
		const TargetInfo& target;
		llvm::MemorySSA& mssa;
		DumbAllocator pool;
		std::unordered_set<llvm::Value*> visited;
		std::deque<Constraint*> constraints;
		
		std::deque<TypeOrValue> variables;
		std::unordered_map<const llvm::Value*, TypeVariable> valueVariables;
		
		size_t valueVariable(const llvm::Value& v);
		
		TypeVariable getAny();
		TypeVariable getBoolean();
		TypeVariable getNum(unsigned width = 0);
		TypeVariable getSint(unsigned width = 0);
		TypeVariable getUint(unsigned width = 0);
		TypeVariable getFunctionPointer();
		TypeVariable getBasicBlockPointer();
		TypeVariable getPointer();
		TypeVariable getPointerTo(const tie::Type& pointee);
		
		template<typename T, typename... Args>
		void constrain(Args... args)
		{
			auto constraint = pool.allocate<T>(args...);
			constraints.push_back(constraint);
		}
		
	public:
		InferenceContext(const TargetInfo& target, llvm::MemorySSA& ssa);
		
		void print(llvm::raw_ostream& os) const;
		void dump() const;
		
		const std::deque<Constraint*> getConstraints() const { return constraints; }
		const tie::Type* getBoundType(TypeVariable tv) const;
		
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
