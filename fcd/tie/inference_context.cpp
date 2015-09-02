//
// inference_context.cpp
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

#include "inference_context.h"

SILENCE_LLVM_WARNINGS_BEGIN()
#include <llvm/Support/raw_os_ostream.h>
SILENCE_LLVM_WARNINGS_END()

#include <iostream>

using namespace llvm;
using namespace std;

namespace
{
	constexpr tie::TypeVariable sentinel = numeric_limits<tie::TypeVariable>::max();
	
	tie::LateralComparisonInfo empty;
	tie::IntegralLCI booleanLCI(1);
	tie::Type anyType(tie::Type::Any, empty);
	tie::Type booleanType(tie::Type::UnsignedInteger, booleanLCI);
	
	template<typename LCI, typename... LCIArgs>
	tie::Type& makeType(DumbAllocator& pool, tie::Type::Category category, LCIArgs... args)
	{
		auto lci = pool.allocate<LCI>(args...);
		return *pool.allocate<tie::Type>(category, *lci);
	}
}

namespace tie
{
#pragma mark - InferenceContext
	InferenceContext::InferenceContext(const TargetInfo& target, MemorySSA& mssa)
	: target(target), mssa(mssa)
	{
	}
	
	TypeVariable InferenceContext::valueVariable(const llvm::Value& value)
	{
		TypeVariable next = variables.size();
		auto pair = valueVariables.insert({&value, next});
		if (pair.second)
		{
			variables.emplace_back(&value);
		}
		return pair.first->second;
	}
	
#pragma mark - InferenceContext InstVisitor implementation
	void InferenceContext::visitConstant(Constant &constant)
	{
		if (auto integralConstant = dyn_cast<ConstantInt>(&constant))
		{
			const APInt& value = integralConstant->getValue();
			// Disjunction over whether the value is signed.
			// XXX: this could be a problem if the same constant is used multiple times but with different meanings.
			DisjunctionConstraint* disj = pool.allocate<DisjunctionConstraint>(pool);
			
			auto variable = valueVariable(constant);
			disj->constrain<SpecializesConstraint>(variable, getSint(value.getMinSignedBits()));
			disj->constrain<SpecializesConstraint>(variable, getUint(value.getActiveBits()));
			constraints.push_back(disj);
			
			constrain<GeneralizesConstraint>(variable, getNum(value.getBitWidth()));
		}
		else if (auto expression = dyn_cast<ConstantExpr>(&constant))
		{
			Instruction* inst = expression->getAsInstruction();
			visit(*inst, &constant);
		}
		else
		{
			assert(isa<GlobalValue>(constant) || isa<UndefValue>(constant));
		}
	}
	
	void InferenceContext::visitICmpInst(ICmpInst &inst, Value* constraintKey)
	{
		constraintKey = constraintKey ? constraintKey : &inst;
		constrain<SpecializesConstraint>(valueVariable(*constraintKey), getBoolean());
		
		TypeVariable minSize = sentinel;
		TypeVariable maxSize = sentinel;
		switch (inst.getPredicate())
		{
			case CmpInst::ICMP_UGE:
			case CmpInst::ICMP_UGT:
			case CmpInst::ICMP_ULE:
			case CmpInst::ICMP_ULT:
				minSize = getUint(0);
				maxSize = getUint(64);
				break;
				
			case CmpInst::ICMP_SGE:
			case CmpInst::ICMP_SGT:
			case CmpInst::ICMP_SLE:
			case CmpInst::ICMP_SLT:
				minSize = getSint(0);
				maxSize = getSint(64);
				break;
				
			case CmpInst::ICMP_EQ:
			case CmpInst::ICMP_NE:
				// nothing to infer
				return;
				
			default:
				assert(false);
				return;
		}
		
		for (unsigned i = 0; i < inst.getNumOperands(); i++)
		{
			auto operandVariable = valueVariable(*inst.getOperand(i));
			constrain<SpecializesConstraint>(operandVariable, minSize);
			constrain<GeneralizesConstraint>(operandVariable, maxSize);
		}
	}
	
	void InferenceContext::visitAllocaInst(AllocaInst& inst, Value* constraintKey)
	{
		constraintKey = constraintKey ? constraintKey : &inst;
		constrain<SpecializesConstraint>(valueVariable(*constraintKey), getPointer());
	}
	
	void InferenceContext::visitLoadInst(LoadInst &inst, Value* constraintKey)
	{
		constraintKey = constraintKey ? constraintKey : &inst;
		auto variable = valueVariable(*constraintKey);
		assert(inst.getType()->isIntegerTy());
		unsigned bitCount = inst.getType()->getIntegerBitWidth();
		
		constrain<SpecializesConstraint>(valueVariable(*inst.getPointerOperand()), getPointer());
		constrain<GeneralizesConstraint>(variable, getNum(bitCount));
		
		if (auto access = mssa.getMemoryAccess(constraintKey))
		if (auto def = access->getDefiningAccess())
		if (auto store = dyn_cast_or_null<StoreInst>(def->getMemoryInst()))
		{
			auto valueOperand = store->getValueOperand();
			constrain<IsEqualConstraint>(variable, valueVariable(*valueOperand));
		}
	}
	
	void InferenceContext::visitStoreInst(StoreInst &inst, Value* constraintKey)
	{
		// This does not teach us anything. Memory locations can be reused for different types.
		// Instead, this creates a memory SSA defining access that we can make use of later to infer things.
	}
	
	void InferenceContext::visitGetElementPtrInst(GetElementPtrInst &inst, Value* constraintKey)
	{
		// Probably used to access a weird register location
		assert(false);
	}
	
	void InferenceContext::visitPHINode(PHINode &inst, Value* constraintKey)
	{
		constraintKey = constraintKey ? constraintKey : &inst;
		auto variable = valueVariable(*constraintKey);
		for (unsigned i = 0; i < inst.getNumIncomingValues(); i++)
		{
			Value* incoming = inst.getIncomingValue(i);
			constrain<IsEqualConstraint>(variable, valueVariable(*incoming));
		}
	}
	
	void InferenceContext::visitSelectInst(SelectInst &inst, Value* constraintKey)
	{
		constraintKey = constraintKey ? constraintKey : &inst;
		auto trueVariable = valueVariable(*inst.getTrueValue());
		constrain<SpecializesConstraint>(valueVariable(*inst.getCondition()), getBoolean());
		constrain<IsEqualConstraint>(trueVariable, valueVariable(*inst.getFalseValue()));
		constrain<GeneralizesConstraint>(valueVariable(*constraintKey), trueVariable);
	}
	
	void InferenceContext::visitCallInst(CallInst &inst, Value* constraintKey)
	{
		// do something here
		//assert(false);
	}
	
	void InferenceContext::visitBinaryOperator(BinaryOperator &inst, Value* constraintKey)
	{
		constraintKey = constraintKey ? constraintKey : &inst;
		auto variable = valueVariable(*constraintKey);
		auto left = valueVariable(*inst.getOperand(0));
		auto right = valueVariable(*inst.getOperand(1));
		
		auto opcode = inst.getOpcode();
		// Division and modulus operations produce a result smaller or equal to the input.
		if (opcode == BinaryOperator::SDiv || opcode == BinaryOperator::SRem || opcode == BinaryOperator::LShr)
		{
			constrain<SpecializesConstraint>(variable, getUint());
			constrain<GeneralizesConstraint>(variable, left);
			constrain<GeneralizesConstraint>(variable, right);
		}
		else if (opcode == BinaryOperator::UDiv || opcode == BinaryOperator::URem || opcode == BinaryOperator::AShr)
		{
			constrain<SpecializesConstraint>(variable, getSint());
			constrain<GeneralizesConstraint>(variable, left);
			constrain<GeneralizesConstraint>(variable, right);
		}
		else if (opcode == BinaryOperator::And)
		{
			// A logical AND is sometimes used to truncate integers, even signed ones and sometimes even pointers, so
			// don't infer signedness.
			constrain<GeneralizesConstraint>(variable, left);
			constrain<GeneralizesConstraint>(variable, right);
		}
		else if (opcode == BinaryOperator::Add)
		{
			DisjunctionConstraint* disj = pool.allocate<DisjunctionConstraint>(pool);
			
			auto numeric = getNum();
			auto pointer = getPointer();
			// Case 1: both sides are integers
			ConjunctionConstraint* case1 = pool.allocate<ConjunctionConstraint>(pool);
			case1->constrain<SpecializesConstraint>(left, numeric);
			case1->constrain<SpecializesConstraint>(right, numeric);
			case1->constrain<SpecializesConstraint>(variable, left);
			case1->constrain<SpecializesConstraint>(variable, right);
			disj->constraints.push_back(case1);
			
			// Case 2: left side is a pointer, right side is an integer
			ConjunctionConstraint* case2 = pool.allocate<ConjunctionConstraint>(pool);
			case2->constrain<SpecializesConstraint>(left, pointer);
			case2->constrain<SpecializesConstraint>(right, numeric);
			case2->constrain<SpecializesConstraint>(variable, pointer);
			disj->constraints.push_back(case2);
			
			// Case 3: left side is an integer, right side is a pointer
			ConjunctionConstraint* case3 = pool.allocate<ConjunctionConstraint>(pool);
			case3->constrain<SpecializesConstraint>(left, numeric);
			case3->constrain<SpecializesConstraint>(right, pointer);
			case3->constrain<SpecializesConstraint>(variable, pointer);
			disj->constraints.push_back(case3);
			
			constraints.push_back(disj);
		}
		// Subtracting pointers results in an integer.
		else if (opcode == BinaryOperator::Sub)
		{
			// Special case for two's complement
			auto constant = dyn_cast<ConstantInt>(inst.getOperand(0));
			if (constant != nullptr && constant->getLimitedValue() == 0)
			{
				constrain<SpecializesConstraint>(right, getSint());
				constrain<IsEqualConstraint>(variable, right);
			}
			else
			{
				auto numeric = getNum();
				auto pointer = getPointer();
				DisjunctionConstraint* disj = pool.allocate<DisjunctionConstraint>(pool);
				
				// Case 1: both sides are integers
				ConjunctionConstraint* case1 = pool.allocate<ConjunctionConstraint>(pool);
				case1->constrain<SpecializesConstraint>(left, numeric);
				case1->constrain<SpecializesConstraint>(right, numeric);
				case1->constrain<SpecializesConstraint>(variable, left);
				case1->constrain<SpecializesConstraint>(variable, right);
				disj->constraints.push_back(case1);
				
				// Case 2: left side is a pointer, right side is an integer
				ConjunctionConstraint* case2 = pool.allocate<ConjunctionConstraint>(pool);
				case2->constrain<SpecializesConstraint>(left, pointer);
				case2->constrain<SpecializesConstraint>(right, numeric);
				case2->constrain<SpecializesConstraint>(variable, pointer);
				disj->constraints.push_back(case2);
				
				// Case 3: both sides are pointers
				ConjunctionConstraint* case3 = pool.allocate<ConjunctionConstraint>(pool);
				case3->constrain<SpecializesConstraint>(left, pointer);
				case3->constrain<SpecializesConstraint>(right, pointer);
				case3->constrain<SpecializesConstraint>(variable, numeric);
				disj->constraints.push_back(case3);
				
				constraints.push_back(disj);
			}
		}
		// Special case for negation
		else if (opcode == BinaryOperator::Xor)
		{
			auto op1 = inst.getOperand(1);
			auto constant = dyn_cast<ConstantInt>(op1);
			if (constant == nullptr)
			{
				constant = dyn_cast<ConstantInt>(inst.getOperand(0));
			}
			
			if (constant != nullptr && constant->getValue() == constant->getType()->getMask())
			{
				auto nonConstant = constant == op1 ? right : left;
				constrain<SpecializesConstraint>(nonConstant, getUint());
				constrain<IsEqualConstraint>(variable, nonConstant);
			}
			else
			{
				constrain<SpecializesConstraint>(variable, left);
				constrain<SpecializesConstraint>(variable, right);
			}
		}
		// Everything else should produce an output at least as large as the input.
		else
		{
			constrain<SpecializesConstraint>(variable, left);
			constrain<SpecializesConstraint>(variable, right);
		}
	}
	
	void InferenceContext::visitCastInst(CastInst &inst, Value* constraintKey)
	{
		constraintKey = constraintKey ? constraintKey : &inst;
		auto variable = valueVariable(*constraintKey);
		// Try to imply that the value had had this type all along. If it doesn't work,
		// fall back to an actual cast.
		auto disj = pool.allocate<DisjunctionConstraint>(pool);
		
		auto type = inst.getType();
		auto casted = valueVariable(*inst.getOperand(0));
		if (auto intType = dyn_cast<IntegerType>(type))
		{
			auto num = getNum(intType->getIntegerBitWidth());
			auto conj = pool.allocate<ConjunctionConstraint>(pool);
			conj->constrain<SpecializesConstraint>(casted, num);
			conj->constrain<IsEqualConstraint>(variable, casted);
			disj->constraints.push_back(conj);
			
			// fall back
			disj->constrain<SpecializesConstraint>(variable, num);
		}
		else if (isa<PointerType>(type))
		{
			auto pointer = getPointer();
			auto conj = pool.allocate<ConjunctionConstraint>(pool);
			conj->constrain<SpecializesConstraint>(casted, pointer);
			conj->constrain<IsEqualConstraint>(variable, casted);
			disj->constraints.push_back(conj);
			
			// fall back
			disj->constrain<SpecializesConstraint>(variable, pointer);
		}
		else
		{
			assert(!"Implement me");
		}
		
		disj->constrain<IsEqualConstraint>(variable, casted);
		
		constraints.push_back(disj);
		constraints.push_back(disj);
	}
	
	void InferenceContext::visitTerminatorInst(TerminatorInst &inst, Value* constraintKey)
	{
		// do nothing
	}
	
	void InferenceContext::visitInstruction(Instruction &inst, Value* constraintKey)
	{
		assert(false);
	}
	
	void InferenceContext::visit(Instruction& inst, Value* constraintKey)
	{
		constraintKey = constraintKey ? constraintKey : &inst;
		for (unsigned i = 0; i < inst.getNumOperands(); i++)
		{
			auto op = inst.getOperand(i);
			if (auto constant = dyn_cast<Constant>(op))
			{
				visitConstant(*constant);
			}
		}
		
		InstVisitor<InferenceContext>::visit(inst);
	}
	
	void InferenceContext::visit(Instruction& inst)
	{
		visit(inst, &inst);
	}
	
#pragma mark - InferenceContext misc.
	void InferenceContext::print(raw_ostream& os) const
	{
		for (size_t i = 0; i < variables.size(); i++)
		{
			os << i << ":";
			const auto& var = variables[i];
			auto iter = valueVariables.find(var.value);
			if (iter == valueVariables.end())
			{
				os << "  <";
				var.type->print(os);
				os << '>';
			}
			else
			{
				if (!isa<Instruction>(var.value))
				{
					os << "  ";
				}
				var.value->print(os);
			}
			os << '\n';
		}
		os << '\n';
		
		for (const auto* constraint : constraints)
		{
			constraint->print(os);
			os << '\n';
		}
	}
	
	void InferenceContext::dump() const
	{
		raw_os_ostream rerr(cerr);
		print(rerr);
	}
	
	const Type* InferenceContext::getBoundType(TypeVariable tv) const
	{
		if (tv < variables.size())
		{
			const auto& typeOrValue = variables[tv];
			if (valueVariables.count(typeOrValue.value) == 0)
			{
				return typeOrValue.type;
			}
		}
		
		return nullptr;
	}
	
#pragma mark - InferenceContext getters
	// XXX: cache these to avoid unnecessary allocations?
	TypeVariable InferenceContext::getAny()
	{
		TypeVariable result = variables.size();
		variables.emplace_back(&anyType);
		return result;
	}
	
	TypeVariable InferenceContext::getBoolean()
	{
		TypeVariable result = variables.size();
		variables.emplace_back(&booleanType);
		return result;
	}
	
	TypeVariable InferenceContext::getNum(unsigned width)
	{
		TypeVariable result = variables.size();
		auto& type = makeType<IntegralLCI>(pool, tie::Type::Integral, width);
		variables.emplace_back(&type);
		return result;
	}
	
	TypeVariable InferenceContext::getSint(unsigned width)
	{
		TypeVariable result = variables.size();
		auto& type = makeType<IntegralLCI>(pool, tie::Type::SignedInteger, width);
		variables.emplace_back(&type);
		return result;
	}
	
	TypeVariable InferenceContext::getUint(unsigned width)
	{
		TypeVariable result = variables.size();
		auto& type = makeType<IntegralLCI>(pool, tie::Type::UnsignedInteger, width);
		variables.emplace_back(&type);
		return result;
	}
	
	TypeVariable InferenceContext::getFunctionPointer()
	{
		TypeVariable result = variables.size();
		auto& type = makeType<CodePointerLCI>(pool, tie::Type::CodePointer, tie::CodePointerLCI::Function, target.getPointerWidth());
		variables.emplace_back(&type);
		return result;
	}
	
	TypeVariable InferenceContext::getBasicBlockPointer()
	{
		TypeVariable result = variables.size();
		auto& type = makeType<CodePointerLCI>(pool, tie::Type::CodePointer, tie::CodePointerLCI::Label, target.getPointerWidth());
		variables.emplace_back(&type);
		return result;
	}
	
	TypeVariable InferenceContext::getPointer()
	{
		TypeVariable result = variables.size();
		auto& type = makeType<IntegralLCI>(pool, tie::Type::Pointer, target.getPointerWidth());
		variables.emplace_back(&type);
		return result;
	}
	
	TypeVariable InferenceContext::getPointerTo(const tie::Type& pointee)
	{
		TypeVariable result = variables.size();
		auto& type = makeType<DataPointerLCI>(pool, tie::Type::DataPointer, pointee, target.getPointerWidth());
		variables.emplace_back(&type);
		return result;
	}
}
