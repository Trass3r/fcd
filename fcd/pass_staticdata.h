#pragma once

#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/Passes.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Constants.h>

#include <unordered_map>

class StaticDataPass final : public llvm::ModulePass
{
	using Base = llvm::ModulePass;
	llvm::SmallVector<llvm::GlobalVariable*, 4> sectionsData;
	llvm::SmallVector<uint64_t, 4> sectionsAddresses;

public:
	static char ID;

	StaticDataPass()
	: Base(ID)
	{
	}

	bool runOnModule(llvm::Module& module) override;
	bool runOnFunction(llvm::Function& function);
	bool runOnBB(llvm::BasicBlock& block);
	bool runOnInst(llvm::Instruction& inst);

private:
	//! check if staticAddress references static data and replace it with a proper global array access
	bool fixReference(llvm::Value* valueToReplace, uint64_t staticAddress, llvm::Value* dynamicOffset = nullptr);
};
