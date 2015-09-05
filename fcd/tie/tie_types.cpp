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

#include <algorithm>

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
	
	using TypeCategory = SimpleType::Category;
	TypeCategory parentCategories[] = {
		[TypeCategory::Any]					= TypeCategory::MaxSimple,
		 [TypeCategory::Integral]			= TypeCategory::Any,
		  [TypeCategory::SignedInteger]		= TypeCategory::Integral,
		  [TypeCategory::UnsignedInteger]	= TypeCategory::Integral,
		 [TypeCategory::Pointer]			= TypeCategory::Integral,
		  [TypeCategory::DataPointer]		= TypeCategory::Pointer,
		  [TypeCategory::CodePointer]		= TypeCategory::Pointer,
	};
	
	static_assert(countof(parentCategories) == SimpleType::MaxSimple, "Missing entries in parent category table");
	
	template<typename T, typename U, typename Compare>
	bool isSubsetOf(const T& a, const U& b, Compare&& compare)
	{
		return all_of(a.begin(), a.end(), [&](auto type)
		{
			return any_of(b.begin(), b.end(), [&](auto otherType)
			{
				return compare(type, otherType);
			});
		});
	}
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

#pragma mark - Type
void tie::Type::dump() const
{
	raw_os_ostream rerr(cerr);
	print(rerr);
}

#pragma mark - SimpleType
bool SimpleType::isEqualTo(const tie::Type &that) const
{
	if (getCategory() != that.getCategory())
	{
		return false;
	}
	
	const SimpleType& thatSimple = static_cast<const SimpleType&>(that);
	return getComparisonInfo().isEqualTo(thatSimple.getComparisonInfo());
}

bool SimpleType::isGeneralizationOf(const Type& that) const
{
	auto thatType = that.getCategory();
	if (thatType > MaxSimple)
	{
		// Types above MaxSimple aren't simple types, let them do the comparison
		return that.isSpecializationOf(*this);
	}
	
	const SimpleType& thatSimple = static_cast<const SimpleType&>(that);
	while (thatType != MaxSimple)
	{
		if (thatType == getCategory())
		{
			break;
		}
		thatType = parentCategories[thatType];
	}
	
	if (thatType == MaxSimple)
	{
		return false;
	}
	
	return getComparisonInfo().isGeneralizationOf(thatSimple.getComparisonInfo());
}

bool SimpleType::isSpecializationOf(const tie::Type &that) const
{
	return that.isGeneralizationOf(*this);
}

void SimpleType::print(llvm::raw_ostream &os) const
{
	getComparisonInfo().print(os, getCategory());
}

#pragma mark - CompositeType
size_t tie::CompositeType::size(const Type& that)
{
	if (auto comp = dyn_cast<const CompositeType>(&that))
	{
		return comp->types.size();
	}
	
	return 1;
}

bool tie::CompositeType::isSubsetOf(const Type &that) const
{
	if (auto comp = dyn_cast<const CompositeType>(&that))
	{
		return ::isSubsetOf(types, comp->types, [](NOT_NULL(const Type) a, NOT_NULL(const Type) b)
		{
			return a->isEqualTo(*b);
		});
	}
	
	return types.size() == 1 && that.isEqualTo(*types[0]);
}

bool tie::CompositeType::isSupersetOf(const Type &that) const
{
	if (auto comp = dyn_cast<const CompositeType>(&that))
	{
		return ::isSubsetOf(comp->types, types, [](NOT_NULL(const Type) a, NOT_NULL(const Type) b)
		{
			return a->isEqualTo(*b);
		});
	}
	
	return any_of(types.begin(), types.end(), [&](NOT_NULL(const Type) type)
	{
		return type->isEqualTo(that);
	});
}

bool tie::CompositeType::isEqualTo(const tie::Type &that) const
{
	if (auto thatUnion = dyn_cast<const CompositeType>(&that))
	{
		return types.size() == thatUnion->types.size() && isSubsetOf(*thatUnion);
	}
	
	auto thatSimple = static_cast<const SimpleType*>(&that);
	return types.size() == 1 && thatSimple->isEqualTo(*types[0]);
}

void tie::CompositeType::print(llvm::raw_ostream &os) const
{
	os << '(';
	auto iter = types.begin();
	if (iter != types.end())
	{
		(*iter)->print(os);
		for (++iter; iter != types.end(); ++iter)
		{
			os << ", ";
			(*iter)->print(os);
		}
	}
	os << ')';
}

#pragma mark - UnionType
bool UnionType::isGeneralizationOf(const tie::Type &that) const
{
	return isSupersetOf(that);
}

bool UnionType::isSpecializationOf(const tie::Type &that) const
{
	return isSubsetOf(that);
}

void UnionType::print(llvm::raw_ostream &os) const
{
	os << 'U';
	CompositeType::print(os);
}

#pragma mark - IntersectionType
bool IntersectionType::isGeneralizationOf(const tie::Type &that) const
{
	return isSubsetOf(that);
}

bool IntersectionType::isSpecializationOf(const tie::Type &that) const
{
	return isSupersetOf(that);
}

void IntersectionType::print(llvm::raw_ostream &os) const
{
	os << 'A'; // closest ASCII symbol to the intersection symbol
	CompositeType::print(os);
}
