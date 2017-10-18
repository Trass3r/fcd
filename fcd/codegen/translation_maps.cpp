//
// translation_maps.cpp
// Copyright (C) 2015 Félix Cloutier.
// All Rights Reserved.
//
// This file is distributed under the University of Illinois Open Source
// license. See LICENSE.md for details.
//

#include "metadata.h"
#include "translation_maps.h"

using namespace llvm;
using namespace std;

Function* AddressToFunction::insertFunction(uint64_t address)
{
	char defaultName[] = "func_0000000000000000";
	snprintf(defaultName, sizeof defaultName, "func_%" PRIx64, address);
	
	// TODO: can't use internal currently as everything gets removed
	Function* fn = Function::Create(&fnType, GlobalValue::ExternalLinkage, defaultName, &module);
	// TODO: sret? pointer parameter specifies the address of a structure that is the return value of the function in the source program. This pointer must be guaranteed by the caller to be valid: loads and stores to the structure may be assumed by the callee not to trap and to be properly aligned
	// TODO: returned? 
	fn->addAttributes(1, AttributeSet::get(module.getContext(), 1, { Attribute::NoAlias, Attribute::NoCapture, Attribute::NonNull }));
	// FIXME: pointerelementtype will disappear
	fn->addDereferenceableAttr(1, module.getDataLayout().getTypeAllocSize((*fnType.param_begin())->getPointerElementType()));

	// FIXME: try to prevent it from being deleted too early, remove later?
	/*
	PointerType* int8ptr = PointerType::get(IntegerType::get(module.getContext(), 8), 0);
	ArrayType* usedArray = ArrayType::get(int8ptr, 1);
	GlobalVariable* gvar_array_llvm_used = new GlobalVariable(module, usedArray, false, GlobalValue::AppendingLinkage,
	        ConstantArray::get(usedArray, { ConstantExpr::getBitCast(fn, int8ptr) }),
	"llvm.compiler.used");
	gvar_array_llvm_used->setSection("llvm.metadata");
	*/

	// TODO: not sure that even holds
	//fn->addFnAttr(Attribute::ArgMemOnly);
	md::setVirtualAddress(*fn, address);
	md::setArgumentsRecoverable(*fn);
	return fn;
}

size_t AddressToFunction::getDiscoveredEntryPoints(unordered_set<uint64_t> &entryPoints) const
{
	size_t total = 0;
	for (const auto& pair : functions)
	{
		if (md::isPrototype(*pair.second))
		{
			entryPoints.insert(pair.first);
			++total;
		}
	}
	return total;
}

Function* AddressToFunction::getCallTarget(uint64_t address)
{
	Function*& result = functions[address];
	
	if (result == nullptr)
	{
		result = insertFunction(address);
	}
	return result;
}

Function* AddressToFunction::createFunction(uint64_t address)
{
	Function*& result = functions[address];
	if (result == nullptr)
	{
		result = insertFunction(address);
	}
	else if (!md::isPrototype(*result))
	{
		// the function needs to be fresh and new
		return nullptr;
	}
	
	// reset prototype status (and everything else, really)
	result->dropAllReferences();
	BasicBlock::Create(result->getContext(), "entry", result);
	md::setVirtualAddress(*result, address);
	md::setArgumentsRecoverable(*result);
	return result;
}

bool AddressToBlock::getOneStub(uint64_t& address)
{
	auto iter = stubs.begin();
	while (iter != stubs.end())
	{
		if (iter->second->getNumUses() != 0)
		{
			address = iter->first;
			return true;
		}
		iter->second->eraseFromParent();
		iter = stubs.erase(iter);
	}
	return false;
}

llvm::BasicBlock* AddressToBlock::blockToInstruction(uint64_t address)
{
	auto iter = blocks.find(address);
	if (iter != blocks.end())
	{
		return iter->second;
	}
	
	BasicBlock*& stub = stubs[address];
	if (stub == nullptr)
	{
		stub = BasicBlock::Create(insertInto.getContext(), "", &insertInto);
		ReturnInst::Create(insertInto.getContext(), stub);
	}
	return stub;
}

llvm::BasicBlock* AddressToBlock::implementInstruction(uint64_t address)
{
	BasicBlock*& bodyBlock = blocks[address];
	if (bodyBlock != nullptr)
	{
		return nullptr;
	}
	
	bodyBlock = BasicBlock::Create(insertInto.getContext(), "", &insertInto);
	
	auto leadingZeroes = static_cast<unsigned>(__builtin_clzll(address));
	unsigned pointerSize = ((sizeof address * CHAR_BIT) - leadingZeroes + CHAR_BIT - 1) / CHAR_BIT * 2;
	
	// set block name (aesthetic reasons)
	char blockName[] = "0000000000000000";
	snprintf(blockName, sizeof blockName, "%0.*" PRIx64, pointerSize, address);
	bodyBlock->setName(blockName);
	
	auto iter = stubs.find(address);
	if (iter != stubs.end())
	{
		iter->second->replaceAllUsesWith(bodyBlock);
		iter->second->eraseFromParent();
		stubs.erase(iter);
	}
	return bodyBlock;
}
