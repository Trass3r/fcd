#include "pass_staticdata.h"

#include <llvm/Analysis/AliasAnalysis.h>
#include <llvm/Analysis/Passes.h>
#include <llvm/Analysis/CallGraphSCCPass.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/PatternMatch.h>

using namespace llvm;
using namespace llvm::PatternMatch;

char StaticDataPass::ID = 0;

bool StaticDataPass::runOnModule(llvm::Module& module)
{
	for (const char* name : { "rodata", "data", "bss" })
	{
		GlobalVariable* var = module.getGlobalVariable(name, true);
		if (!var)
			continue;

		GlobalVariable* varaddr = module.getGlobalVariable(name + std::string(".vaddr"), true);
		assert(varaddr);

		uint64_t address = cast<ConstantInt>(varaddr->getInitializer())->getLimitedValue();

		sectionsData.push_back(var);
		sectionsAddresses.push_back(address);
	}

	bool changed = false;
	for (Function& func : module)
	{
		changed |= runOnFunction(func);
	}

	return changed;
}

bool StaticDataPass::runOnFunction(Function& function)
{
	bool changed = false;
	for (BasicBlock& block : function)
	{
		changed |= runOnBB(block);
	}
	if (changed)
		function.removeFnAttr(Attribute::ArgMemOnly);
	return changed;
}

bool StaticDataPass::runOnBB(BasicBlock& block)
{
	bool changed = false;

	std::vector<Instruction*> v;
	v.reserve(block.getInstList().size());
	for (Instruction& inst : block)
		v.push_back(&inst);

	for (Instruction* inst : v)
	{
		changed |= runOnInst(*inst);
	}
	return changed;
}

bool StaticDataPass::fixReference(Value* valueToReplace, uint64_t staticAddress, Value* dynamicOffset /*= nullptr*/)
{
	size_t i = size_t(-1);
	for (uint64_t staticDataVirtualAddr : sectionsAddresses)
	{
		++i;
		if (staticAddress < staticDataVirtualAddr)
			continue;

		GlobalVariable* staticDataArray = sectionsData[i];
		uint64_t staticDataSize = staticDataArray->getInitializer()->getType()->getArrayNumElements();

		if (staticAddress >= staticDataVirtualAddr + staticDataSize)
			continue;

		LLVMContext& ctx = valueToReplace->getContext();
		IntegerType* i64 = Type::getInt64Ty(ctx);
		ConstantInt* zero = ConstantInt::get(i64, 0);

		ConstantInt* baseOffset = ConstantInt::get(i64, staticAddress - staticDataVirtualAddr);
		Constant* arr[2] = { zero, baseOffset };
		Constant* arrayBaseRef = ConstantExpr::getInBoundsGetElementPtr(staticDataArray->getType()->getElementType(), staticDataArray, ArrayRef<Constant*>(arr, 2));

		Value* arrayRef = ConstantExpr::getPointerCast(arrayBaseRef, valueToReplace->getType());

		if (dynamicOffset)
		{
			auto inst = cast<Instruction>(valueToReplace);
			ConstantInt* elementSizeBytes = ConstantInt::get(i64, valueToReplace-> getType()->getPointerElementType()->getScalarSizeInBits() / 8);
			dynamicOffset = BinaryOperator::CreateExactUDiv(dynamicOffset, elementSizeBytes, "", inst);
			arrayRef = GetElementPtrInst::CreateInBounds(arrayRef, dynamicOffset, "", inst);
		}

		valueToReplace->replaceAllUsesWith(arrayRef);

		assert(valueToReplace->use_empty());
		if (auto inst = dyn_cast<Instruction>(valueToReplace))
			inst->eraseFromParent();

		return true;
	}

	return false;
}

static bool matchesAdd(Value* a, Value*& b, ConstantInt*& c)
{
	return match(a, m_Add(m_Value(b), m_ConstantInt(c))) ||
	       match(a, m_Add(m_ConstantInt(c), m_Value(b)));
}

bool StaticDataPass::runOnInst(Instruction& inst)
{
	if (auto loadInst = dyn_cast<LoadInst>(&inst))
	{
		ConstantInt* globalAddress = nullptr;
		Value* loadFrom = loadInst->getPointerOperand();

		// load from global variable
		// %0 = load i32, i32* inttoptr (i64 address to i32*)
		if (auto inttoptr = dyn_cast<ConstantExpr>(loadFrom))
		{
			globalAddress = dyn_cast<ConstantInt>(inttoptr->getOperand(0));
			if (!globalAddress)
				return false;
		}

		// array access
		// DWORD PTR [rdx*4+0x601000] =>
		//
		// %2 = shl i64 %rdx, 2
		// %3 = add i64 %2, 0x601000
		// %4 = inttoptr i64 %3 to i32*
		// %5 = load i32, i32* %4
		if (auto inttoptr = dyn_cast<IntToPtrInst>(loadFrom))
		{
			auto operand = inttoptr->getOperand(0);
			Value* dynamicOffset;
			if (matchesAdd(operand, dynamicOffset, globalAddress))
				return fixReference(loadFrom, globalAddress->getLimitedValue(), dynamicOffset);
		}

		if (!globalAddress)
			return false;

		return fixReference(loadFrom, globalAddress->getLimitedValue());
	}
	else if (auto store = dyn_cast<StoreInst>(&inst))
	{
		// write to global variable or array entry
		// store i32 42, i32* inttoptr (i64 address to i32*)
		Value* storeDest = store->getPointerOperand();
		if (auto inttoptr = dyn_cast<ConstantExpr>(storeDest))
		{
			ConstantInt* globalAddress = dyn_cast<ConstantInt>(inttoptr->getOperand(0));
			if (!globalAddress)
				return false;
			return fixReference(storeDest, globalAddress->getLimitedValue());
		}

		// store vtable address
		// %1 = getelementptr %struct.x86_regs* %0, i64 0, i32 9, i32 0
		// store i64 address, i64* %1
		Value* valueOperand = store->getValueOperand();
		ConstantInt* constant = dyn_cast<ConstantInt>(valueOperand);
		if (constant)
			return fixReference(valueOperand, constant->getLimitedValue());
	}

	return false;
}

ModulePass* createStaticDataPass()
{
	return new StaticDataPass();
}

static RegisterPass<StaticDataPass> X("staticdata", "fix static data references", true, false);
