#pragma once
#include <executables/executable.h>

namespace llvm
{
	class Module;
}
class GlobalDataMgr;

class Compiler
{
protected:
	Executable& exe;
	GlobalDataMgr& datamgr;

public:
	virtual ~Compiler();

	explicit Compiler(Executable& exe, GlobalDataMgr& datamgr)
	: exe(exe),
	  datamgr(datamgr)
	{
	}

	virtual void scanForVtables(llvm::Module& module) = 0;
	virtual void transformAllocationFunctions(llvm::Module& module) = 0;
};

class GccCompiler final : Compiler
{
	using Base = Compiler;
public:
	using Compiler::Compiler;
	~GccCompiler() override;

	void scanForVtables(llvm::Module& module) override;
	void transformAllocationFunctions(llvm::Module& module) override;
};
