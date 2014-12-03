//===--- DeclContext.h - Swift Language Context ASTs ------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2015 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
// This file defines the DeclContext class.  A DeclContext is the semantic
// construct that a declaration belongs to, such as the enclosing
// AbstractClosureExpr or declaration.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_DECLCONTEXT_H
#define SWIFT_DECLCONTEXT_H

#include "swift/AST/Identifier.h"
#include "swift/AST/TypeAlignments.h"
#include "swift/Basic/LLVM.h"
#include "swift/Basic/STLExtras.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/PointerUnion.h"

namespace llvm {
  class raw_ostream;
}

namespace swift {
  class AbstractFunctionDecl;
  class ASTContext;
  class ASTWalker;
  class CanType;
  class Decl;
  class DeclContext;
  class ExtensionDecl;
  class GenericParamList;
  class LazyResolver;
  class LazyMemberLoader;
  class GenericSignature;
  class GenericTypeParamType;
  class Requirement;
  class SourceFile;
  class Type;
  class Module;
  class ValueDecl;
  class Initializer;
  class ClassDecl;

enum class DeclContextKind : uint8_t {
  AbstractClosureExpr,
  Initializer,
  TopLevelCodeDecl,
  AbstractFunctionDecl,
  Last_LocalDeclContextKind = AbstractFunctionDecl,

  Module,
  FileUnit,
  NominalTypeDecl,
  ExtensionDecl,
  Last_DeclContextKind = ExtensionDecl
};

/// A DeclContext is an AST object which acts as a semantic container
/// for declarations.  As a policy matter, we currently define
/// contexts broadly: a lambda expression in a function is a new
/// DeclContext, but a new brace statement is not.  There's no
/// particular mandate for this, though.
///
/// Note that DeclContexts have stricter alignment requirements than AST nodes
/// in general, so if an AST node class multiply inherits from DeclContext
/// and another base class, it must 'using DeclContext::operator new;' in order
/// to use an allocator with the correct alignment.
class alignas(1 << DeclContextAlignInBits) DeclContext {
  
  enum {
    KindBits = DeclContextAlignInBits
  };
  static_assert(unsigned(DeclContextKind::Last_DeclContextKind) < 1U<<KindBits,
                "Not enough KindBits for DeclContextKind");
  
  llvm::PointerIntPair<DeclContext*, KindBits, DeclContextKind> ParentAndKind;

  /// Change the parent of this context.  This should only be used
  /// very carefully.
  void setParent(DeclContext *parent) { ParentAndKind.setPointer(parent); }
  friend class Initializer; // uses setParent
  friend class AutoClosureExpr; // uses setParent
  friend class AbstractClosureExpr; // uses setParent
  
public:
  DeclContext(DeclContextKind Kind, DeclContext *Parent)
    : ParentAndKind(Parent, Kind) {
    assert((Parent != 0 || isModuleContext()) &&
           "DeclContext must have a parent unless it is a module!");
  }

  /// Returns the kind of context this is.
  DeclContextKind getContextKind() const {
    return ParentAndKind.getInt();
  }
  
  /// \brief Obtain the canonical type from a type extension declaration,
  /// binding any unbound generic types if necessary.
  static CanType getExtendedType(const ExtensionDecl *ED);

  /// Determines whether this context is itself a local scope in a
  /// code block.  A context that appears in such a scope, like a
  /// local type declaration, does not itself become a local context.
  bool isLocalContext() const {
    return getContextKind() <= DeclContextKind::Last_LocalDeclContextKind;
  }
  
  /// isModuleContext - Return true if this is a subclass of Module.
  bool isModuleContext() const {
    return getContextKind() == DeclContextKind::Module;
  }

  /// \returns true if this is a context with module-wide scope, e.g. a module
  /// or a source file.
  bool isModuleScopeContext() const {
    return getContextKind() == DeclContextKind::Module ||
           getContextKind() == DeclContextKind::FileUnit;
  }

  /// \returns true if this is a type context, e.g., a struct, a class, an
  /// enum, a protocol, or an extension.
  bool isTypeContext() const {
    return getContextKind() == DeclContextKind::NominalTypeDecl ||
           getContextKind() == DeclContextKind::ExtensionDecl;
  }

  /// \brief Determine whether this is an extension context.
  bool isExtensionContext() const {
    return getContextKind() == DeclContextKind::ExtensionDecl;
  }

  /// If this DeclContext is a class, or an extension on a class, return the
  /// ClassDecl, otherwise return null.
  ClassDecl *isClassOrClassExtensionContext() const;

  /// getDeclaredTypeOfContext - For a type context, retrieves the declared
  /// type of the context. Returns a null type for non-type contexts.
  Type getDeclaredTypeOfContext() const;

  /// getDeclaredTypeInContext - For a type context, retrieves the declared
  /// type of the context as visible from within the context. Returns a null
  /// type for non-type contexts.
  Type getDeclaredTypeInContext() const;
  
  /// getDeclaredInterfaceType - For a type context, retrieves the interface
  /// type of the context as seen from outside the context. Returns a null
  /// type for non-type contexts.
  Type getDeclaredInterfaceType() const;

  /// \brief Retrieve the innermost generic parameters introduced by this
  /// context or one of its parent contexts, or null if this context is not
  /// directly dependent on any generic parameters.
  GenericParamList *getGenericParamsOfContext() const;

  /// \brief Retrieve the interface generic type parameters and requirements
  /// exposed by this context.
  GenericSignature *getGenericSignatureOfContext() const;
  
  /// Returns this or the first local parent context, or nullptr if it is not
  /// contained in one.
  DeclContext *getLocalContext();
  const DeclContext *getLocalContext() const {
    return const_cast<DeclContext*>(this)->getLocalContext();
  }

  /// Returns the innermost method context.
  ///
  /// This routine looks through closure, initializer, and local function
  /// contexts to find the innermost method (including constructors and
  /// destructors).
  ///
  /// \returns the innermost method, or null if there is no such method.
  AbstractFunctionDecl *getInnermostMethodContext();
  const AbstractFunctionDecl *getInnermostMethodContext() const {
    return const_cast<DeclContext*>(this)->getInnermostMethodContext();
  }

  /// Returns the innermost type context.
  ///
  /// This routine looks through closure, initializer, and local function
  /// contexts to find the innermost type context -- nominal type or
  /// extension.
  DeclContext *getInnermostTypeContext();
  const DeclContext *getInnermostTypeContext() const {
    return const_cast<DeclContext *>(this)->getInnermostTypeContext();
  }

  /// Returns the semantic parent of this context.  A context has a
  /// parent if and only if it is not a module context.
  DeclContext *getParent() const {
    return ParentAndKind.getPointer();
  }

  /// Return true if this is a child of the specified other decl context.
  bool isChildContextOf(const DeclContext *Other) const {
    if (this == Other) return false;

    for (const DeclContext *CurContext = this; CurContext;
         CurContext = CurContext->getParent())
      if (CurContext == Other)
        return true;
    return false;
  }

  /// Returns the module context that contains this context.
  Module *getParentModule() const;

  /// Returns the module scope context that contains this context.
  ///
  /// This is either a \c Module or a \c FileUnit.
  DeclContext *getModuleScopeContext() const;

  /// Returns the source file that contains this context, or null if this
  /// is not within a source file.
  SourceFile *getParentSourceFile() const;

  /// Determine whether this declaration context is generic, meaning that it or
  /// any of its parents have generic parameters.
  bool isGenericContext() const;

  /// Determine whether the innermost context is generic.
  bool isInnermostContextGeneric() const;

  /// Returns true if lookups within this context can't affect downstream files.
  ///
  /// \param functionsArePrivate If true, functions are considered private
  /// contexts. If false, functions are considered private only if implicitly
  /// or explicitly marked private. When concerned only with a function's body,
  /// pass true.
  bool isPrivateContextForLookup(bool functionsArePrivate) const;

  /// Look for the set of declarations with the given name within a type,
  /// its extensions and, optionally, its supertypes.
  ///
  /// This routine performs name lookup within a given type, its extensions
  /// and, optionally, its supertypes and their extensions, from the perspective
  /// of the current DeclContext. It can eliminate non-visible, hidden, and
  /// overridden declarations from the result set. It does not, however, perform
  /// any filtering based on the semantic usefulness of the results.
  ///
  /// \param type The type to look into.
  ///
  /// \param member The member to search for.
  ///
  /// \param options Options that control name lookup, based on the
  /// \c NL_* constants in \c NameLookupOptions.
  ///
  /// \param typeResolver Used to resolve types, usually for overload purposes.
  /// May be null.
  ///
  /// \param[out] decls Will be populated with the declarations found by name
  /// lookup.
  ///
  /// \returns true if anything was found.
  bool lookupQualified(Type type, DeclName member, unsigned options,
                       LazyResolver *typeResolver,
                       SmallVectorImpl<ValueDecl *> &decls) const;

  /// Return the ASTContext for a specified DeclContext by
  /// walking up to the enclosing module and returning its ASTContext.
  ASTContext &getASTContext() const;

  /// \returns true if traversal was aborted, false otherwise.
  bool walkContext(ASTWalker &Walker);

  void dumpContext() const;
  unsigned printContext(llvm::raw_ostream &OS) const;
  
  // Only allow allocation of DeclContext using the allocator in ASTContext.
  void *operator new(size_t Bytes, ASTContext &C,
                     unsigned Alignment = alignof(DeclContext));
};

/// An iterator that walks through a list of declarations stored
/// within some iterable declaration context.
class DeclIterator {
  Decl *Current;

public: 
  typedef std::forward_iterator_tag iterator_category;
  typedef Decl *value_type;
  typedef Decl *reference;
  typedef Decl *pointer; // Non-standard but convenient
  typedef std::ptrdiff_t difference_type;

  DeclIterator(Decl *current = nullptr) : Current(current) { }

  reference operator*() const { return Current; }
  pointer operator->() const { return Current; }

  DeclIterator &operator++();

  DeclIterator operator++(int) {
    DeclIterator old = *this;
    ++*this;
    return old;
  }

  friend bool operator==(DeclIterator lhs, DeclIterator rhs) {
    return lhs.Current == rhs.Current; 
  }

  friend bool operator!=(DeclIterator lhs, DeclIterator rhs) {
    return !(lhs == rhs);
  }
};

/// The range of declarations stored within an iterable declaration
/// context.
typedef IteratorRange<DeclIterator> DeclRange;

/// The kind of an \c IterableDeclContext.
enum class IterableDeclContextKind : uint8_t {  
  NominalTypeDecl,
  ExtensionDecl,
};

/// A declaration context that tracks the declarations it (directly)
/// owns and permits iteration over them.
///
/// Note that an iterable declaration context must inherit from both
/// \c IterableDeclContext and \c DeclContext.
class IterableDeclContext {
  /// The first declaration in this context.
  mutable Decl *FirstDecl = nullptr;

  /// The last declaration in this context, used for efficient insertion,
  /// along with the kind of iterable declaration context.
  mutable llvm::PointerIntPair<Decl *, 2, IterableDeclContextKind> 
    LastDeclAndKind;

  /// Lazy member loader, if any.
  ///
  /// FIXME: Can we collapse this storage into something?
  mutable LazyMemberLoader *LazyLoader = nullptr;

  /// Lazy member loader context data.
  uint64_t LazyLoaderContextData = 0;

public:
  IterableDeclContext(IterableDeclContextKind kind)
    : LastDeclAndKind(nullptr, kind) { }

  /// Determine the kind of iterable context we have.
  IterableDeclContextKind getIterableContextKind() const {
    return LastDeclAndKind.getInt();
  }

  /// Retrieve the set of members in this context.
  DeclRange getMembers() const;

  /// Add a member to this context. If the hint decl is specified, the new decl
  /// is inserted immediately after the hint.
  void addMember(Decl *member, Decl *hint = nullptr);

  /// Retrieve the lazy member loader.
  LazyMemberLoader *getLoader() const {
    assert(isLazy());
    return LazyLoader;
  }

  /// Retrieve the context data for the lazy member loader.
  uint64_t getLoaderContextData() const {
    assert(isLazy());
    return LazyLoaderContextData;
  }

  /// Check whether there are lazily-loaded members.
  bool isLazy() const {
    return LazyLoader != nullptr;
  }  

  /// Set the loader for lazily-loaded members.
  void setLoader(LazyMemberLoader *loader, uint64_t contextData);

  /// Load all of the members of this context.
  void loadAllMembers() const;

private:
  /// Add a member to the list for iteration purposes, but do not notify the
  /// subclass that we have done so.
  ///
  /// This is used internally when loading members, because loading a
  /// member is an invisible addition.
  void addMemberSilently(Decl *member, Decl *hint = nullptr) const;
};
  
} // end namespace swift

#endif
