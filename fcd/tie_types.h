//
// tie_types.h
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

#ifndef tie_types_cpp
#define tie_types_cpp

#include "llvm_warnings.h"
#include "pass_targetinfo.h"

SILENCE_LLVM_WARNINGS_BEGIN()
#include <llvm/Support/raw_ostream.h>
SILENCE_LLVM_WARNINGS_END()

#include <memory>

namespace tie
{
	class Type;
	
	class LateralComparisonInfo
	{
	public:
		enum Category
		{
			Integral,
			DataPointer,
			CodePointer,
		};
		
	private:
		Category category;
		
	public:
		LateralComparisonInfo(Category category)
		: category(category)
		{
		}
		
		Category getCategory() const { return category; }
		
		virtual bool isGeneralizationOf(const LateralComparisonInfo& info) const;
	};
	
	class IntegralLCI : public LateralComparisonInfo
	{
		size_t width;
		
	protected:
		IntegralLCI(Category category, size_t width)
		: LateralComparisonInfo(category), width(width)
		{
		}
		
	public:
		IntegralLCI(size_t width)
		: IntegralLCI(Integral, width)
		{
		}
		
		virtual bool isGeneralizationOf(const LateralComparisonInfo& info) const;
		
		static inline bool classof(const LateralComparisonInfo* that)
		{
			return that->getCategory() == Integral;
		}
	};
	
	class DataPointerLCI : public IntegralLCI
	{
		Type& type;
		
	public:
		DataPointerLCI(Type& type, size_t pointerWidth)
		: IntegralLCI(DataPointer, pointerWidth), type(type)
		{
		}
		
		Type& getPointerType() { return type; }
		const Type& getPointerType() const { return type; }
		
		virtual bool isGeneralizationOf(const LateralComparisonInfo& info) const;
		
		static inline bool classof(const LateralComparisonInfo* that)
		{
			return that->getCategory() == DataPointer;
		}
	};
	
	class CodePointerLCI : public IntegralLCI
	{
	public:
		enum CodePointerType
		{
			Label,
			Function,
		};
		
	private:
		CodePointerType pointerType;
		
	public:
		CodePointerLCI(CodePointerType type, size_t width)
		: IntegralLCI(CodePointer, width), pointerType(type)
		{
		}
		
		CodePointerType getPointerType() const { return pointerType; }
		
		virtual bool isGeneralizationOf(const LateralComparisonInfo& info) const;
		
		static inline bool classof(const LateralComparisonInfo* that)
		{
			return that->getCategory() == CodePointer;
		}
	};
	
	class Type
	{
	public:
		enum Category {
			Any,
			Integral,
			SignedInteger,
			UnsignedInteger,
			Pointer,
			DataPointer,
			CodePointer,
			MaxCategory
		};
		
	private:
		Category category;
		std::unique_ptr<LateralComparisonInfo> lateral;
		
	public:
		Type(Category category, std::unique_ptr<LateralComparisonInfo> lateral)
		: category(category), lateral(std::move(lateral))
		{
		}
		
		Category getCategory() const { return category; }
		LateralComparisonInfo& getComparisonInfo() { return *lateral; }
		const LateralComparisonInfo& getComparisonInfo() const { return *lateral; }
		
		bool isGeneralizationOf(const Type& that) const;
		bool isSpecializationOf(const Type& that) const;
		
		virtual void print(llvm::raw_ostream& os) const;
		void dump() const;
	};
}

#endif /* tie_types_cpp */
