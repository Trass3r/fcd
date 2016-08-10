
#include "gcc.h"
#include "globaldatamgr.h"

SILENCE_LLVM_WARNINGS_BEGIN()
#include <llvm/ADT/ArrayRef.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Constants.h>
SILENCE_LLVM_WARNINGS_END()

using namespace llvm;

Compiler::~Compiler()
{
}

GccCompiler::~GccCompiler()
{
}

/*
template <typename Range, typename Pred> //, std::enable_if_t<std::is_function<>>>
auto partition(Range&& range, Pred pred)
{
	struct Ret
	{
		using elPtr = decltype(&*range.begin());
		elPtr begin;
		elPtr end;
	} ret;
	bool predState = false;
	for(auto&& el : range)
	{
		bool newState = pred(el);
		if (!)
	}
}*/

void analyzeVT(uint64_t* start, uint64_t len)
{
	//if ()
}

void GccCompiler::scanForVtables(Module& module)
{
	auto* rosection   = exe.getSectionInfo(".rodata");
	auto* codesection = exe.getSectionInfo(".text");
	assert(rosection);
	assert(codesection);

	uint64_t codeStart = codesection->vaddr;
	uint64_t codeEnd   = codeStart + codesection->data.size();
	// TODO: use real pointer size
	auto pointers = ArrayRef<uint64_t>(reinterpret_cast<const uint64_t*>(rosection->data.data()), rosection->data.size()/sizeof(uint64_t));

	auto vbegin = pointers.begin();
	bool inRange = false;
	for (auto it = pointers.begin(), end = pointers.end(); it != end; ++it)
	//for (uint64_t p : pointers)
	{
		uint64_t p = *it;
		if (p >= codeStart && p < codeEnd)
		{
			if (!inRange)
			{
				// start new range
				vbegin = it;
				inRange = true;
			}
			continue;
		}

		if (!inRange)
			continue;

		// try to handle holes inside
		// TODO possible? cxx_pure_virtual
		// padding afterwards
		if (p == 0)
			continue;

		// here we stop
		inRange = false;

		// do it
		//analyzeVT(it, it - vbegin);

		// base class can have zeros
		for (; *vbegin == 0; --vbegin) {}

		// typeinfo
		assert(*(vbegin-1) == 0 || (*(vbegin-1) > rosection->vaddr && *(vbegin-1) < rosection->data.size()));
		vbegin -= 2;

		uint64_t vaddr = codeStart + (vbegin - pointers.begin());
		auto var = datamgr.create(ArrayRef<uint64_t>(vbegin, it), vaddr, 0, true);
		var->setLinkage(GlobalValue::LinkOnceODRLinkage);

		/*
		auto vtabledata = llvm::ConstantDataArray::get(module.getContext(), ArrayRef<uint64_t>(vbegin, it));
		// TODO: struct type?
		// ArrayType::get(Type::getInt8Ty(module.getContext())->getPointerTo(),
		auto vtable = new GlobalVariable(module, vtabledata->getType(), true, GlobalVariable::LinkOnceODRLinkage, vtabledata, ".vtable");
		*/
	}

	//HACK
	if (inRange)
	{
		// base class can have zeros
		for (; *vbegin == 0; --vbegin) {}

		// typeinfo
		assert(*(vbegin - 1) == 0 || (*(vbegin - 1) > rosection->vaddr && *(vbegin - 1) < rosection->data.size()));
		vbegin -= 2;

		uint64_t vaddr = codeStart + (vbegin - pointers.begin());
		auto var = datamgr.create(ArrayRef<uint64_t>(vbegin, pointers.end()), vaddr, 0, true);
		var->setLinkage(GlobalValue::LinkOnceODRLinkage);
	}
}


void GccCompiler::transformAllocationFunctions(Module &module)
{

}
