#include "symbol_resolution.hh"
#include "../lslmini.hh"

namespace Tailslide {


bool BaseSymbolResolutionVisitor::visit(LSLLValueExpression *node) {
  ((LSLIdentifier *) node->getChild(0))->resolveSymbol(SYM_VARIABLE);
  return false;
}

bool BaseSymbolResolutionVisitor::visit(LSLFunctionExpression *node) {
  auto *id = (LSLIdentifier *) node->getChild(0);
  id->resolveSymbol(SYM_FUNCTION);
  return true;
}

/// replace the node's old symbol table, registering the new one.
void BaseSymbolResolutionVisitor::replaceSymbolTable(LSLASTNode *node) {
  // TODO: unregister old table? need to figure out node copy semantics.
  auto *symtab = _mAllocator->newTracked<LSLSymbolTable>();
  node->setSymbolTable(symtab);
  node->mContext->table_manager->registerTable(symtab);
}


// all global var definitions are implicitly hoisted above function definitions
// all functions and states have their declarations implicitly hoisted as well.
class GlobalSymbolResolutionVisitor: public BaseSymbolResolutionVisitor {
  public:
    explicit GlobalSymbolResolutionVisitor(ScriptAllocator *allocator) : BaseSymbolResolutionVisitor(allocator) {};
    virtual bool visit(LSLGlobalVariable *node) {
      // descend first so we can resolve any symbol references present in the rvalue
      // before we've defined the identifier from the lvalue.
      // Necessary so things like `string foo = foo;` will error correctly.
      visitChildren(node);

      auto *identifier = (LSLIdentifier *) node->getChild(0);
      identifier->setSymbol(_mAllocator->newTracked<LSLSymbol>(
          identifier->getName(), identifier->getType(), SYM_VARIABLE, SYM_GLOBAL, node->getLoc(), nullptr, node
      ));
      node->defineSymbol(identifier->getSymbol());
      return false;
    };

    virtual bool visit(LSLGlobalFunction *node) {
      replaceSymbolTable(node);
      auto *identifier = (LSLIdentifier *) node->getChild(0);

      // define function in parent scope since functions have their own scope
      identifier->setSymbol(_mAllocator->newTracked<LSLSymbol>(
          identifier->getName(),
          identifier->getType(),
          SYM_FUNCTION,
          SYM_GLOBAL,
          node->getLoc(),
          (LSLFunctionDec *) node->getChild(1)
      ));
      node->getParent()->defineSymbol(identifier->getSymbol());
      // don't descend, we only want the declaration.
      return false;
    };

    virtual bool visit(LSLState *node) {
      replaceSymbolTable(node);
      auto *identifier = (LSLIdentifier *)node->getChild(0);
      identifier->setSymbol(_mAllocator->newTracked<LSLSymbol>(
          identifier->getName(), identifier->getType(), SYM_STATE, SYM_GLOBAL, identifier->getLoc()
      ));
      node->getParent()->defineSymbol(identifier->getSymbol());
      // don't descend, we only want the declaration
      return false;
    };
};

bool SymbolResolutionVisitor::visit(LSLScript *node) {
  replaceSymbolTable(node);
  // Walk over just the globals before we descend into function
  // bodies and do general symbol resolution.
  GlobalSymbolResolutionVisitor visitor(_mAllocator);
  node->visit(&visitor);
  return true;
}

bool SymbolResolutionVisitor::visit(LSLDeclaration *node) {
  // visit the rvalue first so we correctly handle things like
  // `string foo = foo;`
  LSLASTNode *rvalue = node->getChild(1);
  if (rvalue)
    rvalue->visit(this);

  auto *identifier = (LSLIdentifier *) node->getChild(0);
  identifier->setSymbol(_mAllocator->newTracked<LSLSymbol>(
      identifier->getName(), identifier->getType(), SYM_VARIABLE, SYM_LOCAL, node->getLoc(), nullptr, node
  ));
  node->defineSymbol(identifier->getSymbol());

  // if (1) string foo; isn't valid!
  if (!node->getDeclarationAllowed()) {
    NODE_ERROR(node, E_DECLARATION_INVALID_HERE, identifier->getSymbol()->getName());
  }
  return false;
}

static void register_func_param_symbols(LSLASTNode *proto, bool is_event) {
  LSLASTNode *child = proto->getChildren();
  while (child) {
    auto *identifier = (LSLIdentifier *) child;
    identifier->setSymbol(proto->mContext->allocator->newTracked<LSLSymbol>(
        identifier->getName(),
        identifier->getType(),
        SYM_VARIABLE,
        is_event ? SYM_EVENT_PARAMETER : SYM_FUNCTION_PARAMETER,
        child->getLoc()
    ));
    proto->defineSymbol(identifier->getSymbol());
    child = child->getNext();
  }
}

bool SymbolResolutionVisitor::visit(LSLGlobalFunction *node) {
  assert(_mPendingJumps.empty());
  visitChildren(node);
  resolvePendingJumps();
  return false;
}

bool SymbolResolutionVisitor::visit(LSLFunctionDec *node) {
  register_func_param_symbols(node, false);
  return true;
}

bool SymbolResolutionVisitor::visit(LSLGlobalVariable *node) {
  // Symbol resolution for everything inside is already done, don't descend.
  return false;
}

bool SymbolResolutionVisitor::visit(LSLEventHandler *node) {
  replaceSymbolTable(node);

  auto *id = (LSLIdentifier *) node->getChild(0);
  // look for a prototype for this event in the builtin namespace
  auto *sym = node->getRoot()->lookupSymbol(id->getName(), SYM_EVENT);
  if (sym) {
    id->setSymbol(_mAllocator->newTracked<LSLSymbol>(
        id->getName(), id->getType(), SYM_EVENT, SYM_BUILTIN, node->getLoc(), (LSLParamList *) node->getChild(1)
    ));
    node->getParent()->defineSymbol(id->getSymbol());
  } else {
    NODE_ERROR(node, E_INVALID_EVENT, id->getName());
  }

  assert(_mPendingJumps.empty());
  visitChildren(node);
  resolvePendingJumps();
  return false;
}

bool SymbolResolutionVisitor::visit(LSLEventDec *node) {
  register_func_param_symbols(node, true);
  return true;
}

bool SymbolResolutionVisitor::visit(LSLLabel *node) {
  auto *identifier = (LSLIdentifier *) node->getChild(0);
  identifier->setSymbol(_mAllocator->newTracked<LSLSymbol>(
      identifier->getName(), identifier->getType(), SYM_LABEL, SYM_LOCAL, node->getLoc()
  ));
  node->defineSymbol(identifier->getSymbol());
  _mCollectedLabels.emplace_back(identifier);
  return true;
}

bool SymbolResolutionVisitor::visit(LSLJumpStatement *node) {
  // Jumps are special. Since they can jump forwards in the control flow, we
  // can only resolve the labels they refer to after we leave the enclosing
  // function or event handler, having passed the last place the label it
  // refers to could have been defined.
  _mPendingJumps.emplace_back((LSLIdentifier*) node->getChild(0));
  return true;
}

bool SymbolResolutionVisitor::visit(LSLStateStatement *node) {
  ((LSLIdentifier *) node->getChild(0))->resolveSymbol(SYM_STATE);
  return true;
}

bool SymbolResolutionVisitor::visit(LSLCompoundStatement *node) {
  replaceSymbolTable(node);
  return true;
}

void SymbolResolutionVisitor::resolvePendingJumps() {
  for (auto *id : _mPendingJumps) {
    // First do the lookup by lexical scope, triggering an error if it fails.
    id->resolveSymbol(SYM_LABEL);

    // That's all we have to do unless we want to match SL exactly.
    if (!_mLindenJumpSemantics)
      continue;

    // Labels in SL are weird in that they pretend they're lexically scoped but they
    // really aren't in either LSO or Mono. The label you're jumping to _must_
    // be in the lexical scope of your `jump`, but it will actually jump to the
    // last occurrence of a label with a given name within the function body,
    // crossing lexical scope boundaries. This was likely a mistake, but we have
    // to deal with it if we want the same `jump` semantics as LSO. Gnarly.

    // Note that Mono will actually fail to compile correctly on duplicated labels.
    // because they're included in the CIL verbatim.

    if (auto *orig_sym = id->getSymbol()) {
      LSLSymbol *new_sym = nullptr;
      // Now get the label this will jump to in SL, iterate in reverse so the last
      // instance of a label in a function will come first
      for (auto i = _mCollectedLabels.rbegin(); i != _mCollectedLabels.rend(); ++i) {
        auto *cand_sym = (*i)->getSymbol();
        if (!cand_sym || cand_sym->getSymbolType() != SYM_LABEL)
          continue;
        // name matches
        if (!strcmp(cand_sym->getName(), orig_sym->getName())) {
          new_sym = cand_sym;
          break;
        }
      }
      assert(new_sym);
      // This jump specifically will jump to a label other than the one you might expect,
      // so warn on that along with the general warning for duplicate label names.
      if (new_sym != orig_sym) {
        NODE_ERROR(id, W_JUMP_TO_WRONG_LABEL, orig_sym->getName());
      }
      id->setSymbol(new_sym);
    }
  }

  if (_mLindenJumpSemantics) {
    // Walk the list of collected labels and warn on any duplicated names
    std::set<std::string> label_names;
    for (auto &label_id: _mCollectedLabels) {
      if (label_names.find(label_id->getName()) != label_names.end()) {
        NODE_ERROR(label_id, W_DUPLICATE_LABEL_NAME, label_id->getName());
      } else {
        label_names.insert(label_id->getName());
      }
    }
  }

  _mPendingJumps.clear();
  _mCollectedLabels.clear();
}

}
