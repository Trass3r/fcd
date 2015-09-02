//
// pass_tie.h
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

#ifndef pass_tie_cpp
#define pass_tie_cpp

#include "llvm_warnings.h"

SILENCE_LLVM_WARNINGS_BEGIN()
#include <llvm/Analysis/CallGraphSCCPass.h>
#include <llvm/Pass.h>
SILENCE_LLVM_WARNINGS_END()

class TypeInference : public llvm::CallGraphSCCPass
{
public:
	static char ID;
	
	TypeInference();
	
	virtual const char* getPassName() const override;
	virtual void getAnalysisUsage(llvm::AnalysisUsage& au) const override;
	virtual bool runOnSCC(llvm::CallGraphSCC& scc) override;
};

namespace llvm
{
	void initializeTypeInferencePass(PassRegistry& pr);
}

#endif /* pass_tie_cpp */
