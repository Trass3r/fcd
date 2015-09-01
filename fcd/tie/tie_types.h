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

// These types are meant to be allocated through a "DumbAllocator". Consequently, objects of these classes do not have
// any owning reference to any object, and cannot have a non-trivial destructor.

namespace tie
{
	class LateralComparisonInfo;
	
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
		LateralComparisonInfo& lateral;
		
	public:
		Type(Category category, LateralComparisonInfo& lateral)
		: category(category), lateral(lateral)
		{
		}
		
		Category getCategory() const { return category; }
		LateralComparisonInfo& getComparisonInfo() { return lateral; }
		const LateralComparisonInfo& getComparisonInfo() const { return lateral; }
		
		// Overloading operators < and > would be very confusing, so let's not overload ==
		// either for the sake of consistency.
		bool isEqualTo(const Type& that) const;
		bool isGeneralizationOf(const Type& that) const;
		bool isSpecializationOf(const Type& that) const;
		
		void print(llvm::raw_ostream& os) const;
		void dump() const;
	};
	
	class LateralComparisonInfo
	{
	public:
		enum Category
		{
			Any,
			Integral,
			DataPointer,
			CodePointer,
		};
		
	private:
		Category category;
		
	protected:
		LateralComparisonInfo(Category category)
		: category(category)
		{
		}
		
	public:
		LateralComparisonInfo()
		: LateralComparisonInfo(Any)
		{
		}
		
		Category getCategory() const { return category; }
		
		bool isEqualTo(const LateralComparisonInfo& info) const;
		virtual bool isGeneralizationOf(const LateralComparisonInfo& info) const;
		
		virtual void print(llvm::raw_ostream& os, Type::Category category) const;
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
		
		size_t getWidth() const { return width; }
		virtual bool isGeneralizationOf(const LateralComparisonInfo& info) const;
		virtual void print(llvm::raw_ostream& os, Type::Category category) const;
		
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
		virtual void print(llvm::raw_ostream& os, Type::Category category) const;
		
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
		virtual void print(llvm::raw_ostream& os, Type::Category category) const;
		
		static inline bool classof(const LateralComparisonInfo* that)
		{
			return that->getCategory() == CodePointer;
		}
	};
}

#endif /* tie_types_cpp */
