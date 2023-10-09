// Copyright (C) 2023 The JLLVM Contributors.
//
// This file is part of JLLVM.
//
// JLLVM is free software; you can redistribute it and/or modify it under  the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 3, or (at your option) any later version.
//
// JLLVM is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
// of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with JLLVM; see the file LICENSE.txt.  If not
// see <http://www.gnu.org/licenses/>.

#pragma once

#include <llvm/ADT/StringRef.h>

#include <jllvm/class/ClassFile.hpp>
#include <jllvm/class/Descriptors.hpp>
#include <jllvm/object/ClassObject.hpp>

#include <string>

#include <swl/variant.hpp>

/// This file contains mangling and demangling functions for all Java related functions of the compiler that have
/// special meanings. The compiler generates calls to functions with these names to perform functions such as method
/// lookups or more.
/// These methods are special as they require the loading of a class object when called. Having the compiler generate
/// calls to these functions and requiring the JIT framework to generate the definitions decouples the JVM to LLVM
/// compilation from the VM systems completely.
namespace jllvm
{
/// Mangling for calling a given Java method directly. This does not perform any lookups or class loading but rather
/// assumes that the given method with the given type MUST exist in the already loaded class.
/// The function signature of the call must match the method descriptor with the 'this' object as first argument.
///
/// Syntax:
/// <direct-call> ::= <class-name> '.' <method-name> ':' <descriptor>
std::string mangleDirectMethodCall(llvm::StringRef className, llvm::StringRef methodName, MethodType descriptor);

std::string mangleDirectMethodCall(const Method* method);

std::string mangleDirectMethodCall(const MethodInfo& methodInfo, const ClassFile& classFile);

/// Mangling for calling a function returning either the address of a static field or the offset of an instance field.
/// The caller must know whether the field is a static or an instance field and use the corresponding function
/// signature:
/// Instance field: () -> int(sizeof(std::size_t) * 8)
/// Static field: () -> ptr
///
/// Syntax:
/// <field-access> ::= <class-name> '.' <field-name> ':' <descriptor>
std::string mangleFieldAccess(llvm::StringRef className, llvm::StringRef fieldName, FieldType descriptor);

enum class MethodResolution
{
    /// 5.4.3.3. Method Resolution from the JVM Spec.
    Virtual,
    /// 5.4.3.4. Interface Method Resolution from the JVM Spec.
    Interface,
    /// 6.5 'invokespecial': Method resolution from the JVM Spec.
    Special,
};

/// Mangling for calling a function performing method resolution and then calling the resolved method.
/// The function signature of the call must match the method descriptor with the 'this' object as first argument.
///
/// Syntax:
/// <method-resolution-call> ::= <method-resolution> <direct-call>
/// <method-resolution> ::= 'Virtual Call to ' | 'Interface Call to ' | 'Special Call to '
std::string mangleMethodResolutionCall(MethodResolution resolution, llvm::StringRef className,
                                       llvm::StringRef methodName, MethodType descriptor);

/// Mangling for calling a function performing static method resolution and then calling the resolved method.
/// The function signature of the call must match the method descriptor exactly.
///
/// Syntax:
/// <static-call> ::= 'Static Call to ' <direct-call>
std::string mangleStaticCall(llvm::StringRef className, llvm::StringRef methodName, MethodType descriptor);

/// Mangling for calling a function returning a loaded class object.
/// The function signature of the call must be: () -> reference.
///
/// Syntax:
/// <class-object-access> ::= 'Load ' <descriptor>
std::string mangleClassObjectAccess(FieldType descriptor);

/// A call produced via 'mangleFieldAccess'.
struct DemangledFieldAccess
{
    llvm::StringRef className;
    llvm::StringRef fieldName;
    FieldType descriptor;
};

/// A call produced via 'mangleMethodResolutionCall'.
struct DemangledMethodResolutionCall
{
    MethodResolution resolution{};
    llvm::StringRef className;
    llvm::StringRef methodName;
    MethodType descriptor;
};

/// A call produced via 'mangleStaticCall'.
struct DemangledStaticCall
{
    llvm::StringRef className;
    llvm::StringRef methodName;
    MethodType descriptor;
};

using DemangledVariant =
    swl::variant<std::monostate, DemangledFieldAccess, DemangledMethodResolutionCall, DemangledStaticCall, FieldType>;

/// Attempts to demangle a symbol produced by any of the 'mangle*' functions above with the exception of
/// 'mangleDirectMethodCall'.
/// Returns 'std::monostate' if the symbol name is not the output of any of these functions.
DemangledVariant demangleStubSymbolName(llvm::StringRef symbolName);

} // namespace jllvm