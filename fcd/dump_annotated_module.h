#pragma once

#include <llvm/Support/FormattedStream.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/AssemblyAnnotationWriter.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Module.h>

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

struct CommentWriter final : llvm::AssemblyAnnotationWriter
{
	void emitFunctionAnnot(const llvm::Function* f, llvm::formatted_raw_ostream& s) override
	{
		s << "; [#uses=" << f->getNumUses() << ']';
		s << '\n';
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
			if (auto ddi = llvm::dyn_cast<llvm::DbgDeclareInst>(inst))
			{
				if (!padding)
				{
					s.PadToColumn(50);
					s << ';';
				}
				s << " [debug variable = " << ddi->getVariable()->getName() << ']';
			}
			else if (auto dvi = llvm::dyn_cast<llvm::DbgValueInst>(inst))
			{
				if (!padding)
				{
					s.PadToColumn(50);
					s << ';';
				}
				s << " [debug variable = " << dvi->getVariable()->getName() << ']';
			}
		}
	}
};

static void dumpAnnotatedModule(llvm::Module& module)
{
	CommentWriter cw = {};
	module.print(llvm::outs(), &cw);
}
