//
// x86_64_systemv.cpp
// Copyright (C) 2015 Félix Cloutier.
// All Rights Reserved.
//
// This file is distributed under the University of Illinois Open Source
// license. See LICENSE.md for details.
//

// About the x86_64 SystemV calling convention:
// http://x86-64.org/documentation/abi.pdf pp 20-22
// In short, for arguments:
// - Aggregates are passed in registers, unless one of the fields is a floating-point field, in which case it goes to
//		memory; or unless not enough integer registers are available, in which case it also goes to the stack.
// - Integral arguments are passed in rdi-rsi-rdx-rcx-r8-r9.
// - Floating-point arguments are passed in [xyz]mm0-[xyz]mm7
// - Anything else/left remaining goes to the stack.
// For return values:
// - Integral values go to rax-rdx.
// - Floating-point values go to xmm0-xmm1.
// - Large return values may be written to *rdi, and rax will contain rdi (in which case it's indistinguishible from
//		a function accepting the output destination as a first parameter).
// The relative parameter order of values of different classes is not preserved.

#include "cc_common.h"
#include "metadata.h"
#include "x86_64_systemv.h"

#include <llvm/IR/PatternMatch.h>
#if LLVM_VERSION_MAJOR > 4
#include <llvm/Analysis/MemorySSA.h>
#else
#include <llvm/Transforms/Utils/MemorySSA.h>
#endif

#include <unordered_map>

using namespace llvm;
using namespace llvm::PatternMatch;
using namespace std;

namespace
{
	RegisterCallingConvention<CallingConvention_x86_64_systemv> registerSysV;
	
	const char* parameterRegisters[] = { "rdi", "rsi", "rdx", "rcx", "r8", "r9" };
	const char* returnRegisters[] = {"rax", "rdx"};
	
	inline const char** registerPosition(const TargetRegisterInfo& reg, const char** begin, const char** end)
	{
		return find(begin, end, reg.name);
	}
	
	inline bool isParameterRegister(const TargetRegisterInfo& reg)
	{
		return registerPosition(reg, begin(parameterRegisters), end(parameterRegisters)) != end(parameterRegisters);
	}
	
	inline bool isReturnRegister(const TargetRegisterInfo& reg)
	{
		return registerPosition(reg, begin(returnRegisters), end(returnRegisters)) != end(returnRegisters);
	}
	
	typedef void (CallInformation::*AddParameter)(ValueInformation&&);
	
	// only handles integer types
	bool addEntriesForType(TargetInfo& targetInfo, CallInformation& info, AddParameter addParam, Type* type, const char**& regIter, const char** end, size_t* spOffset = nullptr)
	{
		unsigned pointerSize = targetInfo.getPointerSize();
		if (isa<PointerType>(type))
		{
			type = IntegerType::get(type->getContext(), pointerSize);
		}
		
		if (auto intType = dyn_cast<IntegerType>(type))
		{
			unsigned bitSize = intType->getIntegerBitWidth();
			while (regIter != end && bitSize != 0)
			{
				(info.*addParam)(ValueInformation(ValueInformation::IntegerRegister, targetInfo.registerNamed(*regIter)));
				regIter++;
				bitSize -= min<unsigned>(bitSize, 64);
			}

			if (spOffset != nullptr)
			{
				while (bitSize != 0)
				{
					(info.*addParam)(ValueInformation(ValueInformation::Stack, *spOffset));
					*spOffset += 8;
					bitSize -= min<unsigned>(bitSize, 64);
				}
			}
			return bitSize == 0;
		}
		
		return type == Type::getVoidTy(type->getContext());
	}
	

	/*
	int publicfoo(this, uint64_t a, uint64_t b, uint64_t c)
		{
			int d = foo(b, a);
			return d + 5;
		}
	private:
		virtual int foo(this, uint64_t a, uint64_t b) { return 0; }
		int a;
	};

	int main(int argc, const char* argv[])
	{
		auto b = std::make_unique<Base>();
		return 3 + b->publicfoo(1,2,3);
	}

	00000000004003c2 <_ZN4Base9publicfooEmmm>:
	  4003c2:	50                   	push   rax
	  4003c3:	48 89 f0             	mov    rax,rsi
	  4003c6:	48 8b 0f             	mov    rcx,QWORD PTR [rdi]
	  4003c9:	48 89 d6             	mov    rsi,rdx
	  4003cc:	48 89 c2             	mov    rdx,rax
	  4003cf:	ff 51 10             	call   QWORD PTR [rcx+0x10]
	  4003d2:	83 c0 05             	add    eax,0x5
	  4003d5:	59                   	pop    rcx
	  4003d6:	c3                   	ret

	  rdi-rsi-rdx-rcx-r8-r9

	0000000000400360 <main>:
	  400360:	55                   	push   rbp
	  400361:	53                   	push   rbx
	  400362:	50                   	push   rax
	  400363:	bf 10 00 00 00       	mov    edi,0x10
	  400368:	e8 e3 ff ff ff       	call   400350 <_Znwm@plt>
	  40036d:	48 89 c3             	mov    rbx,rax
	  400370:	31 c0                	xor    eax,eax
	  400372:	48 89 43 08          	mov    QWORD PTR [rbx+0x8],rax
	  400376:	48 89 03             	mov    QWORD PTR [rbx],rax
	  400379:	48 c7 03 f8 03 40 00 	mov    QWORD PTR [rbx],0x4003f8
	  400380:	be 01 00 00 00       	mov    esi,0x1
	  400385:	ba 02 00 00 00       	mov    edx,0x2
	  40038a:	b9 03 00 00 00       	mov    ecx,0x3
	  40038f:	48 89 df             	mov    rdi,rbx
	  400392:	e8 2b 00 00 00       	call   4003c2 <_ZN4Base9publicfooEmmm>
	  400397:	89 c5                	mov    ebp,eax
	  400399:	83 c5 03             	add    ebp,0x3
	  40039c:	48 8b 03             	mov    rax,QWORD PTR [rbx]
	  40039f:	48 89 df             	mov    rdi,rbx
	  4003a2:	ff 50 08             	call   QWORD PTR [rax+0x8]
	  4003a5:	89 e8                	mov    eax,ebp
	  4003a7:	48 83 c4 08          	add    rsp,0x8
	  4003ab:	5b                   	pop    rbx
	  4003ac:	5d                   	pop    rbp
	  4003ad:	c3                   	ret

	 * define i64 @_ZN4Base9publicfooEmmm(i64, i64, i64, i64) !fcd.vaddr !39 !fcd.recoverable !3 {
	  %5 = inttoptr i64 %1 to i64*, !dbg !40          ; [#uses=1 type=i64*] [debug line = x86_read_reg:55:15@x86_get_effective_address:188:21@x86_read_mem:196:17@x86_read_source_operand:221:11@x86_move_zero_extend:375:24@x86_mov:1137:2]
	  %6 = load i64, i64* %5, align 4                 ; [#uses=2 type=i64]
	  %7 = add i64 %6, 16, !dbg !53                   ; [#uses=1 type=i64] [debug line = x86_get_effective_address:188:18@x86_read_mem:196:17@x86_read_source_operand:221:11@x86_call:592:20]
	  %8 = inttoptr i64 %7 to i64 (i64, i64, i64)**   ; [#uses=1 type=i64 (i64, i64, i64)**]
	  %9 = load i64 (i64, i64, i64)*, i64 (i64, i64, i64)** %8, align 4 ; [#uses=1 type=i64 (i64, i64, i64)*]
	  %10 = call i64 %9(i64 %3, i64 %2, i64 %6)       ; [#uses=1 type=i64]
	  %11 = add i64 %10, 5, !dbg !62                  ; [#uses=1 type=i64] [debug line = x86_add_flags<int>:274:15@x86_add:288:20@x86_add:568:20]
	  %12 = and i64 %11, 4294967295, !dbg !69         ; [#uses=1 type=i64] [debug line = x86_add:288:20@x86_add:568:20]
	  ret i64 %12
	}

	define i32 @main(i32, i8**) !fcd.funver !70 {
	  %3 = call i8* @_Znwm(i64 16)                    ; [#uses=2 type=i8*]
	  %4 = ptrtoint i8* %3 to i64                     ; [#uses=3 type=i64]
	  %5 = add i64 %4, 8, !dbg !71                    ; [#uses=1 type=i64] [debug line = x86_get_effective_address:188:18@x86_write_mem:203:17@x86_write_destination_operand:257:4@x86_move_zero_extend:376:2@x86_mov:1137:2]
	  %6 = inttoptr i64 %5 to i64*                    ; [#uses=1 type=i64*]
	  store i64 0, i64* %6, align 4, !fcd.prgmem !3
	  %7 = bitcast i8* %3 to i64*, !dbg !82           ; [#uses=2 type=i64*] [debug line = x86_read_reg:55:15@x86_get_effective_address:188:21@x86_write_mem:203:17@x86_write_destination_operand:257:4@x86_move_zero_extend:376:2@x86_mov:1137:2]
	  store i64 ptrtoint (i8* getelementptr inbounds ([40 x i8], [40 x i8]* @rodata, i64 0, i64 16) to i64), i64* %7, align 4, !fcd.prgmem !3
	  %8 = call i64 @_ZN4Base9publicfooEmmm(i64 4195223, i64 %4, i64 1, i64 2) ; [#uses=1 type=i64]
	  %9 = trunc i64 %8 to i32, !dbg !95              ; [#uses=1 type=i32] [debug line = x86_read_reg:61:15@x86_read_reg:82:9@x86_read_source_operand:217:11@x86_move_zero_extend:375:24@x86_mov:1137:2]
	  %10 = add i32 %9, 3, !dbg !106                  ; [#uses=1 type=i32] [debug line = x86_add_flags<int>:274:15@x86_add:288:20@x86_add:568:20]
	  %11 = load i64, i64* %7, align 4                ; [#uses=1 type=i64]
	  %12 = add i64 %11, 8, !dbg !113                 ; [#uses=1 type=i64] [debug line = x86_get_effective_address:188:18@x86_read_mem:196:17@x86_read_source_operand:221:11@x86_call:592:20]
	  %13 = inttoptr i64 %12 to void (i64)**          ; [#uses=1 type=void (i64)**]
	  %14 = load void (i64)*, void (i64)** %13, align 4 ; [#uses=1 type=void (i64)*]
	  call void %14(i64 %4)
	  ret i32 %10
	}

	uint64_t _ZN4Base9publicfooEmmm(uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3)
	{
		return (*(uint64_t(**)(uint64_t, uint64_t, uint64_t))(*(uint64_t*)arg1 + 16))(arg3, arg2, *(uint64_t*)arg1) + 5 & 0xffffffff;
	}
	call i64 @_ZN4Base9publicfooEmmm(i64 4195223, i64 %4, i64 1, i64 2)

	*/
	void identifyParameterCandidates(TargetInfo& target, MemorySSA& mssa, MemoryAccess* access, CallInformation& fillOut)
	{

		// Look for values that are written but not used by the caller (parameters).
		// MemorySSA chains memory uses and memory defs. Walk back from the call until the previous call, or to liveOnEntry.
		// Registers in the parameter set that are written to before the function call are parameters for sure.
		// Stack values that are written before a function must also be analyzed post-call to make sure that they're not
		// read again before we can determine with certainty that they're parameters.
		while (!mssa.isLiveOnEntryDef(access))
		{
			access ->print(llvm::outs());
			if (isa<MemoryPhi>(access))
			{
				// too hard, give up
				break;
			}

			auto useOrDef = cast<MemoryUseOrDef>(access);
			Instruction* memoryInst = useOrDef->getMemoryInst();
			if (isa<CallInst>(memoryInst))
			{
				break;
			}
			
			auto def = cast<MemoryDef>(useOrDef);
			// TODO: this check is only *almost* good. The right thing to do would be to make sure that the only
			// accesses reaching from this def are other defs (with a call ending the chain). However, just checking
			// that there is a single use is much faster, and probably good enough.
			if (def->hasOneUse())
			{
				if (auto store = dyn_cast<StoreInst>(memoryInst))
				{
					auto& pointer = *store->getPointerOperand();
					if (const TargetRegisterInfo* info = target.registerInfo(pointer))
					{
						// this could be a parameter register
						if (isParameterRegister(*info))
						{
							auto range = fillOut.parameters();
							const char** regName = std::find(begin(parameterRegisters), end(parameterRegisters), info->name);
							auto paramIdx = regName - begin(parameterRegisters);

							const TargetRegisterInfo* info2 = target.registerInfo(*regName);

							auto position = lower_bound(range.begin(), range.end(), info, [](const ValueInformation& that, const TargetRegisterInfo* i)
							{
								if (that.type == ValueInformation::IntegerRegister)
								{
									auto thatName = registerPosition(*that.registerInfo, begin(parameterRegisters), end(parameterRegisters));
									auto iName = registerPosition(*i, begin(parameterRegisters), end(parameterRegisters));
									return thatName < iName;
								}
								return false;
							});
							
							// TODO: add registers in sequence up to this register
							// (for instance, if we see a use for `rdi` and `rdx`, add `rsi`)
							
							if (position == range.end() || position->registerInfo != info)
							{
								fillOut.insertParameter(position, ValueInformation::IntegerRegister, info);
							}
						}
					}
					else if (md::isProgramMemory(*store))
					{
						// this could be a stack register
						Value* origin = nullptr;
						ConstantInt* offset = nullptr;
						if (match(&pointer, m_BitCast(m_Add(m_Value(origin), m_ConstantInt(offset)))))
						if (const TargetRegisterInfo* rsp = target.registerInfo(*origin))
						if (rsp->name == "rsp")
						{
							// stack parameter
							auto range = fillOut.parameters();
							auto position = lower_bound(range.begin(), range.end(), offset->getLimitedValue(), [](const ValueInformation& that, uint64_t offset)
							{
								return that.type < ValueInformation::Stack || that.frameBaseOffset < offset;
							});
							
							// TODO: add/extend values up to this stack offset.
							// If we see a parameter at +0 and a parameter at +16, then we have missing values.
							
							if (position == range.end() || position->registerInfo != info)
							{
								fillOut.insertParameter(position, ValueInformation::IntegerRegister, info);
							}
						}
					}
				}
				else
				{
					// if it's not a call and it's not a store, then what is it?
					assert(false);
				}
			}
			
			access = useOrDef->getDefiningAccess();
		}
	}
	
	void identifyReturnCandidates(TargetInfo& target, MemorySSA& mssa, MemoryAccess* access, CallInformation& fillOut)
	{
		for (User* user : access->users())
		{
			if (auto memPhi = dyn_cast<MemoryPhi>(user))
			{
				identifyReturnCandidates(target, mssa, memPhi, fillOut);
			}
			else if (auto memUse = dyn_cast<MemoryUse>(user))
			{
				if (auto load = dyn_cast<LoadInst>(memUse->getMemoryInst()))
				if (const TargetRegisterInfo* info = target.registerInfo(*load->getPointerOperand()))
				if (isReturnRegister(*info))
				{
					auto range = fillOut.returns();
					auto position = lower_bound(range.begin(), range.end(), info, [](const ValueInformation& that, const TargetRegisterInfo* i)
					{
						if (that.type == ValueInformation::IntegerRegister)
						{
							auto thatName = registerPosition(*that.registerInfo, begin(parameterRegisters), end(parameterRegisters));
							auto iName = registerPosition(*i, begin(parameterRegisters), end(parameterRegisters));
							return thatName < iName;
						}
						return false;
					});
					
					// TODO: add registers in sequence up to this register
					// (for instance, if we see a use for `rdx`, there should be an `rax` somewhere)
					if (position == range.end() || position->registerInfo != info)
					{
						fillOut.insertReturn(position, ValueInformation::IntegerRegister, info);
					}
				}
			}
		}
	}
}

const char* CallingConvention_x86_64_systemv::name = "x86_64/sysv";

const char* CallingConvention_x86_64_systemv::getHelp() const
{
	return "x86_64 SystemV ABI system calling convention";
}

bool CallingConvention_x86_64_systemv::matches(TargetInfo &target, Executable &executable) const
{
	string triple = executable.getTargetTriple();
	string::size_type firstDash = triple.find('-');
	string::size_type secondDash = triple.find('-', firstDash + 1);
	string::size_type nextDash = triple.find('-', secondDash + 1);
	string arch = triple.substr(0, firstDash);
	string os = triple.substr(secondDash + 1, nextDash);
	if (arch.compare(0, 3, "x86") == 0)
	{
		return os.compare(0, 6, "macosx") == 0 || executable.getExecutableType().compare(0, 3, "ELF") == 0;
	}
	return false;
}

const char* CallingConvention_x86_64_systemv::getName() const
{
	return name;
}

// this one is only called from hackhack function
bool CallingConvention_x86_64_systemv::analyzeFunction(ParameterRegistry &registry, CallInformation &callInfo, Function &function)
{
	// TODO: Look at called functions to find hidden parameters/return values
	
	if (md::isPrototype(function))
	{
		return false;
	}
	
	TargetInfo& targetInfo = registry.getTargetInfo();
	
	// We always need rip and rsp.
	callInfo.addParameter(ValueInformation::IntegerRegister, targetInfo.registerNamed("rip"));
	callInfo.addParameter(ValueInformation::IntegerRegister, targetInfo.registerNamed("rsp"));
	
	// Identify register GEPs.
	// (assume x86 regs as first parameter)
	assert(function.arg_size() == 1);
	auto regs = function.arg_begin();
	auto pointerType = dyn_cast<PointerType>(regs->getType());
	assert(pointerType != nullptr && pointerType->getElementType()->getStructName() == "struct.x86_regs");
	(void) pointerType;
	
	unordered_multimap<const TargetRegisterInfo*, GetElementPtrInst*> geps;
	for (auto& use : regs->uses())
	{
		if (GetElementPtrInst* gep = dyn_cast<GetElementPtrInst>(use.getUser()))
		if (const TargetRegisterInfo* regName = targetInfo.registerInfo(*gep))
		{
			geps.insert({regName, gep});
		}
	}
	
	// Look at temporary registers that are read before they are written
	MemorySSA& mssa = *registry.getMemorySSA(function);
	for (const char* name : parameterRegisters)
	{
		const TargetRegisterInfo* smallReg = targetInfo.registerNamed(name);
		const TargetRegisterInfo& regInfo = targetInfo.largestOverlappingRegister(*smallReg);
		auto range = geps.equal_range(&regInfo);
		
		vector<Instruction*> addresses;
		for (auto iter = range.first; iter != range.second; ++iter)
		{
			addresses.push_back(iter->second);
		}
		
		for (size_t i = 0; i < addresses.size(); ++i)
		{
			Instruction* addressInst = addresses[i];
			for (auto& use : addressInst->uses())
			{
				if (auto load = dyn_cast<LoadInst>(use.getUser()))
				{
					MemoryAccess* parent = cast<MemoryUse>(mssa.getMemoryAccess(load))->getDefiningAccess();
					if (mssa.isLiveOnEntryDef(parent))
					{
						// register argument!
						callInfo.addParameter(ValueInformation::IntegerRegister, &regInfo);
					}
				}
				else if (auto cast = dyn_cast<CastInst>(use.getUser()))
				{
					if (cast->getType()->isPointerTy())
					{
						addresses.push_back(cast);
					}
				}
			}
		}
	}
	
	// Does the function refer to values at an offset above the initial rsp value?
	// Assume that rsp is known to be preserved.
	auto spRange = geps.equal_range(targetInfo.getStackPointer());
	for (auto iter = spRange.first; iter != spRange.second; ++iter)
	{
		auto* gep = iter->second;
		// Find all uses of reference to sp register
		for (auto& use : gep->uses())
		{
			if (auto load = dyn_cast<LoadInst>(use.getUser()))
			{
				// Find uses above +8 (since +0 is the return address)
				for (auto& use : load->uses())
				{
					ConstantInt* offset = nullptr;
					if (match(use.get(), m_Add(m_Value(), m_ConstantInt(offset))))
					{
						auto intOffset = static_cast<int64_t>(offset->getLimitedValue());
						if (intOffset > 8)
						{
							// memory argument!
							callInfo.addParameter(ValueInformation::Stack, intOffset);
						}
					}
				}
			}
		}
	}
	
	// Are we using return registers?
	vector<const TargetRegisterInfo*> usedReturns;
	usedReturns.reserve(2);
	
	for (const char* name : returnRegisters)
	{
		const TargetRegisterInfo* regInfo = targetInfo.registerNamed(name);
		auto range = geps.equal_range(regInfo);
		for (auto iter = range.first; iter != range.second; ++iter)
		{
			bool hasStore = any_of(iter->second->use_begin(), iter->second->use_end(), [](Use& use)
			{
				return isa<StoreInst>(use.getUser());
			});
			
			if (hasStore)
			{
				usedReturns.push_back(regInfo);
				break;
			}
		}
	}
	
	for (const TargetRegisterInfo* reg : ipaFindUsedReturns(registry, function, usedReturns))
	{
		// return value!
		callInfo.addReturn(ValueInformation::IntegerRegister, reg);
	}
	
	return true;
}

bool CallingConvention_x86_64_systemv::analyzeFunctionType(ParameterRegistry& registry, CallInformation& fillOut, FunctionType& type)
{
	TargetInfo& targetInfo = registry.getTargetInfo();
	auto iter = begin(returnRegisters);
	auto addReturn = &CallInformation::addReturn<ValueInformation>;
	if (!addEntriesForType(targetInfo, fillOut, addReturn, type.getReturnType(), iter, end(returnRegisters)))
	{
		return false;
	}
	
	size_t spOffset = 0;
	iter = begin(parameterRegisters);
	auto addParam = &CallInformation::addParameter<ValueInformation>;
	for (Type* t : type.params())
	{
		if (!addEntriesForType(targetInfo, fillOut, addParam, t, iter, end(parameterRegisters), &spOffset))
		{
			return false;
		}
	}
	
	return true;
}


#include <llvm/IR/AssemblyAnnotationWriter.h>
#include <llvm/Support/FormattedStream.h>

#include <llvm/IR/DebugInfoMetadata.h>
static void printDebugLoc(const llvm::DebugLoc& loc, llvm::formatted_raw_ostream& s)
{
	// prepend function name
	auto* scope = llvm::cast<llvm::DIScope>(loc.getScope());
	s << scope->getName();
	s << ':' << loc.getLine() << ':' << loc.getCol();
	if (llvm::DILocation* inlAt = loc.getInlinedAt())
	{
		s << '@';
		printDebugLoc(inlAt, s);
	}
}

class MemorySSAAnnotatedWriter final : public llvm::AssemblyAnnotationWriter
{
   friend class llvm::MemorySSA;
   const MemorySSA *MSSA;

 public:
   MemorySSAAnnotatedWriter(const MemorySSA *M) : MSSA(M) {}

    void emitBasicBlockStartAnnot(const BasicBlock *BB, formatted_raw_ostream &OS) override
	{
	 if (MemoryAccess *MA = MSSA->getMemoryAccess(BB))
		OS << "; " << *MA << "\n";
	}

	void emitInstructionAnnot(const Instruction *I, formatted_raw_ostream &OS) override
	{
	 if (MemoryAccess *MA = MSSA->getMemoryAccess(I))
		OS << "; " << *MA << '*' << MA->getNumUses() << '\n';
	}

   void printInfoComment(const llvm::Value& v, llvm::formatted_raw_ostream& s) override
   {
	   bool padding = false;
	   if (!v.getType()->isVoidTy())
	   {
		   s.PadToColumn(50);
		   padding = true;
		   s << "; [#uses=" << v.getNumUses() << " type=" << *v.getType() << ']';
	   }
	   if (auto inst = llvm::dyn_cast<llvm::Instruction>(&v))
	   {
		   if (const llvm::DebugLoc& loc = inst->getDebugLoc())
		   {
			   if (!padding)
			   {
				   s.PadToColumn(50);
				   padding = true;
				   s << ';';
			   }
			   s << " [debug line = ";
			   printDebugLoc(loc, s);
			   s << ']';
		   }
	   }
   }
 };

bool CallingConvention_x86_64_systemv::analyzeCallSite(ParameterRegistry &registry, CallInformation &fillOut, CallSite cs)
{
	fillOut.clear();
	TargetInfo& targetInfo = registry.getTargetInfo();
	
	Instruction& inst = *cs.getInstruction();
	Function& caller = *inst.getParent()->getParent();
	MemorySSA& mssa = *registry.getMemorySSA(caller);
	MemoryDef* thisDef = cast<MemoryDef>(mssa.getMemoryAccess(&inst));

	MemorySSAAnnotatedWriter annotator(&mssa);
	caller.print(llvm::outs(), &annotator);

	identifyParameterCandidates(targetInfo, mssa, thisDef->getDefiningAccess(), fillOut);
	identifyReturnCandidates(targetInfo, mssa, thisDef, fillOut);
	return true;
}
