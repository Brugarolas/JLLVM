#pragma once

#include <llvm/ExecutionEngine/Orc/Core.h>
#include <llvm/ExecutionEngine/Orc/IndirectionUtils.h>
#include <llvm/ExecutionEngine/Orc/Layer.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>

#include <jllvm/object/ClassLoader.hpp>

#include "ByteCodeCompileUtils.hpp"

namespace jllvm
{
/// Class for JVM operand stack
/// This class also offers method to save and restore the current state of the stack in order to consider the control
/// flow path
class OperandStack
{
    std::vector<llvm::AllocaInst*> m_values;
    std::vector<llvm::Type*> m_types;
    llvm::IRBuilder<>& m_builder;
    std::size_t m_topOfStack{};

    class StackState
    {
        std::vector<llvm::Type*> m_types;
        std::size_t m_topOfStack{};

        StackState(std::vector<llvm::Type*> type, std::size_t topOfStack)
            : m_types{std::move(type)}, m_topOfStack{topOfStack}
        {
        }

        friend class OperandStack;
    };

public:
    using State = StackState;

    OperandStack(llvm::IRBuilder<>& builder, std::uint16_t maxStack)
        : m_builder(builder), m_values{maxStack}, m_types{maxStack}
    {
        std::generate(m_values.begin(), m_values.end(), [&] { return builder.CreateAlloca(builder.getPtrTy()); });
    }

    llvm::Value* pop_back()
    {
        llvm::AllocaInst* alloc = m_values[--m_topOfStack];
        llvm::Type* type = m_types[m_topOfStack];
        return m_builder.CreateLoad(type, alloc);
    }

    std::pair<llvm::Value*, llvm::Type*> pop_back_with_type()
    {
        llvm::AllocaInst* alloc = m_values[--m_topOfStack];
        llvm::Type* type = m_types[m_topOfStack];

        return {m_builder.CreateLoad(type, alloc), type};
    }

    void push_back(llvm::Value* value)
    {
        llvm::AllocaInst* alloc = m_values[m_topOfStack];
        m_types[m_topOfStack++] = value->getType();
        m_builder.CreateStore(value, alloc);
    }

    StackState saveState() const
    {
        return {m_types, m_topOfStack};
    }

    void restoreState(StackState state)
    {
        m_types = std::move(state.m_types);
        m_topOfStack = state.m_topOfStack;
    }

    State getHandlerState() const
    {
        return {{referenceType(m_builder.getContext())}, 1};
    }

    void setHandlerStack(llvm::Value* value)
    {
        llvm::AllocaInst* alloc = m_values.front();
        m_types.front() = value->getType();
        m_builder.CreateStore(value, alloc);
    }
};

/// Helper class to fetch properties about a class while still doing lazy class loading.
/// This works by taking callbacks which are either called immediately if a class object is already loaded, leading
/// to better code generation, or otherwise creating stubs that when called load the given class object and return
/// the value given by the callback.
class LazyClassLoaderHelper
{
    ClassLoader& m_classLoader;
    llvm::orc::JITDylib& m_mainDylib;
    llvm::orc::JITDylib& m_implDylib;
    llvm::orc::IndirectStubsManager& m_stubsManager;
    llvm::orc::JITCompileCallbackManager& m_callbackManager;
    llvm::orc::IRLayer& m_baseLayer;
    llvm::orc::MangleAndInterner& m_interner;
    llvm::DataLayout m_dataLayout;
    llvm::Triple m_triple;

    static void buildClassInitializerInitStub(llvm::IRBuilder<>& builder, const ClassObject& classObject);

    template <class F>
    llvm::Value* returnConstantForClassObject(llvm::IRBuilder<>& builder, llvm::Twine fieldDescriptor, llvm::Twine key,
                                              F&& f, bool mustInitializeClassObject);

    template <class F>
    llvm::Value* doCallForClassObject(llvm::IRBuilder<>& builder, llvm::StringRef className, llvm::StringRef methodName,
                                      llvm::StringRef methodType, bool isStatic, llvm::Twine key,
                                      llvm::ArrayRef<llvm::Value*> args, F&& f);

    struct VTableOffset
    {
        std::size_t slot;
    };

    struct ITableOffset
    {
        std::size_t interfaceId;
        std::size_t slot;
    };

    using ResolutionResult = swl::variant<VTableOffset, ITableOffset, std::string>;

    static ResolutionResult vTableResult(const ClassObject* classObject, const Method* method);

    static ResolutionResult iTableResult(const ClassObject* interface, const Method* method);

    static ResolutionResult virtualMethodResolution(const ClassObject* classObject, llvm::StringRef methodName,
                                                    llvm::StringRef methodType);

    static ResolutionResult interfaceMethodResolution(const ClassObject* classObject, llvm::StringRef methodName,
                                                      llvm::StringRef methodType, ClassLoader& classLoader);

public:
    LazyClassLoaderHelper(ClassLoader& classLoader, llvm::orc::JITDylib& mainDylib, llvm::orc::JITDylib& implDylib,
                          llvm::orc::IndirectStubsManager& stubsManager,
                          llvm::orc::JITCompileCallbackManager& callbackManager, llvm::orc::IRLayer& baseLayer,
                          llvm::orc::MangleAndInterner& interner, const llvm::DataLayout& dataLayout)
        : m_mainDylib(mainDylib),
          m_implDylib(implDylib),
          m_stubsManager(stubsManager),
          m_callbackManager(callbackManager),
          m_baseLayer(baseLayer),
          m_dataLayout(dataLayout),
          m_classLoader(classLoader),
          m_interner(interner)
    {
        m_mainDylib.withLinkOrderDo([&](const llvm::orc::JITDylibSearchOrder& dylibSearchOrder)
                                    { m_implDylib.setLinkOrder(dylibSearchOrder); });
    }

    /// Creates a non-virtual call to the possibly static function 'methodName' of the type 'methodType' within
    /// 'className' using 'args'. This is used to implement `invokestatic` and `invokespecial`.
    llvm::Value* doNonVirtualCall(llvm::IRBuilder<>& builder, bool isStatic, llvm::StringRef className,
                                  llvm::StringRef methodName, llvm::StringRef methodType,
                                  llvm::ArrayRef<llvm::Value*> args);

    enum MethodResolution
    {
        /// 5.4.3.3. Method Resolution from the JVM Spec.
        Virtual,
        /// 5.4.3.4. Interface Method Resolution from the JVM Spec.
        Interface
    };

    /// Creates a virtual call to the function 'methodName' of the type 'methodType' within 'className' using 'args'.
    /// 'resolution' determines how the actual method to be called is resolved using the previously mentioned strings.
    llvm::Value* doIndirectCall(llvm::IRBuilder<>& builder, llvm::StringRef className, llvm::StringRef methodName,
                                llvm::StringRef methodType, llvm::ArrayRef<llvm::Value*> args,
                                MethodResolution resolution);

    /// Returns an LLVM integer constant which contains the offset of the 'fieldName' with the type 'fieldType'
    /// within the class 'className'.
    llvm::Value* getInstanceFieldOffset(llvm::IRBuilder<>& builder, llvm::StringRef className,
                                        llvm::StringRef fieldName, llvm::StringRef fieldType);

    /// Returns an LLVM Pointer which points to the static field 'fieldName' with the type 'fieldType'
    /// within the class 'className'.
    llvm::Value* getStaticFieldAddress(llvm::IRBuilder<>& builder, llvm::StringRef className, llvm::StringRef fieldName,
                                       llvm::StringRef fieldType);

    /// Returns an LLVM Pointer which points to the class object of the type with the given field descriptor.
    llvm::Value* getClassObject(llvm::IRBuilder<>& builder, llvm::Twine fieldDescriptor,
                                bool mustInitializeClassObject = false);
};
} // namespace jllvm
