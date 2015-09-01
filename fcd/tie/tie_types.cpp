//
// tie_types.cpp
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

#include "tie_types.h"

SILENCE_LLVM_WARNINGS_BEGIN()
#include <llvm/Support/raw_os_ostream.h>
SILENCE_LLVM_WARNINGS_END()

#include <iostream>

using namespace llvm;
using namespace std;
using namespace tie;

namespace
{
	template<typename T, size_t N>
	constexpr size_t countof(T (&)[N])
	{
		return N;
	}
	
	using TypeCategory = tie::Type::Category;
	TypeCategory parentCategories[] = {
		[TypeCategory::Any]					= TypeCategory::MaxCategory,
		 [TypeCategory::Integral]			= TypeCategory::Any,
		  [TypeCategory::SignedInteger]		= TypeCategory::Integral,
		  [TypeCategory::UnsignedInteger]	= TypeCategory::Integral,
		 [TypeCategory::Pointer]			= TypeCategory::Integral,
		  [TypeCategory::DataPointer]		= TypeCategory::Pointer,
		  [TypeCategory::CodePointer]		= TypeCategory::Pointer,
	};
	
	static_assert(countof(parentCategories) == tie::Type::MaxCategory, "Missing entries in parent category table");
}

bool LateralComparisonInfo::isGeneralizationOf(const LateralComparisonInfo &info) const
{
	return true;
}

bool LateralComparisonInfo::isEqualTo(const tie::LateralComparisonInfo &info) const
{
	return isGeneralizationOf(info) && info.isGeneralizationOf(*this);
}

void LateralComparisonInfo::print(llvm::raw_ostream &os, Type::Category category) const
{
	os << "<any>";
}

bool IntegralLCI::isGeneralizationOf(const LateralComparisonInfo &info) const
{
	if (auto that = dyn_cast<IntegralLCI>(&info))
	{
		return width <= that->width;
	}
	return false;
}

void IntegralLCI::print(llvm::raw_ostream &os, Type::Category category) const
{
	switch (category)
	{
		case Type::Integral: os << '_'; break;
		case Type::SignedInteger: os << 's'; break;
		case Type::UnsignedInteger: os << 'u'; break;
		case Type::Pointer: os << 'p'; break;
		default: assert(false); break;
	}
	
	os << "int" << getWidth();
}

bool DataPointerLCI::isGeneralizationOf(const LateralComparisonInfo &info) const
{
	if (auto that = dyn_cast<DataPointerLCI>(&info))
	{
		return getPointerType().isGeneralizationOf(that->getPointerType());
	}
	return false;
}

void DataPointerLCI::print(llvm::raw_ostream &os, Type::Category category) const
{
	type.print(os);
	os << '*';
}

bool CodePointerLCI::isGeneralizationOf(const LateralComparisonInfo &info) const
{
	if (auto that = dyn_cast<CodePointerLCI>(&info))
	{
		return getPointerType() < that->getPointerType();
	}
	return false;
}

void CodePointerLCI::print(llvm::raw_ostream &os, Type::Category category) const
{
	if (getPointerType() == Label)
	{
		os << "label";
	}
	else
	{
		os << "func";
	}
	os << "ptr";
}

#pragma mark - tie::Type
bool tie::Type::isEqualTo(const tie::Type &that) const
{
	if (getCategory() != that.getCategory())
	{
		return false;
	}
	
	return getComparisonInfo().isEqualTo(that.getComparisonInfo());
}

bool tie::Type::isGeneralizationOf(const Type& that) const
{
	auto thatType = that.getCategory();
	while (thatType != MaxCategory)
	{
		if (thatType == getCategory())
		{
			break;
		}
		thatType = parentCategories[thatType];
	}
	
	if (thatType == MaxCategory)
	{
		return false;
	}
	
	return getComparisonInfo().isGeneralizationOf(that.getComparisonInfo());
}

bool tie::Type::isSpecializationOf(const tie::Type &that) const
{
	return that.isGeneralizationOf(*this);
}

void tie::Type::print(llvm::raw_ostream &os) const
{
	getComparisonInfo().print(os, getCategory());
}

void tie::Type::dump() const
{
	raw_os_ostream rerr(cerr);
	print(rerr);
}
