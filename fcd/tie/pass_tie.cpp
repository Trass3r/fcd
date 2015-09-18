//
// pass_tie.cpp
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
#include "pass_targetinfo.h"
#include "pass_tie.h"
#include "solver.h"

#include <cassert>
#include <iostream>
#include <limits>

using namespace std;
using namespace llvm;

#pragma mark - Pass Implementation
char TypeInference::ID = 0;

TypeInference::TypeInference() : CallGraphSCCPass(ID)
{
}

const char* TypeInference::getPassName() const
{
	return "Type Inference";
}

void TypeInference::getAnalysisUsage(AnalysisUsage &au) const
{
	au.addRequired<TargetInfo>();
	CallGraphSCCPass::getAnalysisUsage(au);
}

bool TypeInference::runOnSCC(CallGraphSCC &scc)
{
	assert(scc.isSingular());
	const auto& info = getAnalysis<TargetInfo>();
	
	for (auto& node : scc)
	{
		if (auto func = node->getFunction())
		if (!func->empty())
		{
			MemorySSA mssa(*func);
			tie::InferenceContext ctx(info, mssa);
			ctx.visit(*func);
			
			tie::Solver solver(pool, ctx);
			solver.solve();
		}
	}
	return false;
}

TypeInference* createTypeInferencePass()
{
	return new TypeInference;
}

INITIALIZE_PASS_BEGIN(TypeInference, "tie", "High-Level Type Inference", false, true)
INITIALIZE_PASS_DEPENDENCY(TargetInfo)
INITIALIZE_PASS_END(TypeInference, "tie", "High-Level Type Inference", false, true)
