
#pragma once

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/ErrorHandling.h>

#include <memory>
#include <variant>
#include <vector>

namespace jllvm
{

/// <BaseType> ::= 'B' | 'C' | 'D' | 'F' | 'I' | 'J' | 'S' | 'Z'
/// Note: We add 'V' for void here as well for convenience.
class BaseType
{
public:
    enum Values
    {
        Boolean, /// 'Z'
        Byte,    /// 'B'
        Char,    /// 'C'
        Short,   /// 'S'
        Int,     /// 'I'
        Float,   /// 'F'
        Double,  /// 'D'
        Long,    /// 'J'
        Void     /// 'V'
    };

private:
    Values m_value;

public:
    /*implicit*/ BaseType(Values value) : m_value(value) {}

    /// Returns the enum value for this base type.
    Values getValue() const
    {
        return m_value;
    }

    /// Returns true if this base type is an integer type.
    bool isIntegerType() const
    {
        switch (m_value)
        {
            case Boolean:
            case Byte:
            case Char:
            case Short:
            case Int:
            case Long: return true;
            default: return false;
        }
    }

    /// Returns true if this type is unsigned. All other types are signed.
    bool isUnsigned() const
    {
        return m_value == Char || m_value == Boolean;
    }

    bool operator==(const BaseType& rhs) const
    {
        return m_value == rhs.m_value;
    }

    bool operator!=(const BaseType& rhs) const
    {
        return !(rhs == *this);
    }
};

/// <ObjectType> ::= 'L' <ClassName> ';'
struct ObjectType
{
    llvm::StringRef className;

    bool operator==(const ObjectType& rhs) const
    {
        return className == rhs.className;
    }

    bool operator!=(const ObjectType& rhs) const
    {
        return !(rhs == *this);
    }
};

struct ArrayType;

using FieldType = std::variant<BaseType, ObjectType, ArrayType>;

/// <ArrayType> ::= '[' <FieldType>
struct ArrayType
{
    std::unique_ptr<FieldType> componentType;

    bool operator==(const ArrayType& rhs) const
    {
        return *componentType == *rhs.componentType;
    }

    bool operator!=(const ArrayType& rhs) const
    {
        return !(rhs == *this);
    }
};

/// Parses a field descriptor string to a more convenient object representation.
/// The lifetime of any strings (basically any contained 'ObjectType's) is equal to the lifetime of the string passed
/// in.
/// Note: This function does not allow error handling at this point in time and either exhibits UB or asserts
/// on invalid strings.
FieldType parseFieldType(llvm::StringRef string);

inline bool isReferenceDescriptor(llvm::StringRef string)
{
    return string.front() == 'L' || string.front() == '[';
}

/// <MethodType> ::= '(' { <FieldType> } ')' <FieldType>
struct MethodType
{
    std::vector<FieldType> parameters;
    FieldType returnType;
};

/// Parses a method descriptor string to a more convenient object representation.
/// Same notes about lifetimes and error handling apply as in 'parseFieldType'.
MethodType parseMethodType(llvm::StringRef string);

} // namespace jllvm
