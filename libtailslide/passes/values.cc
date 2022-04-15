#include "lslmini.hh"
#include "values.hh"

namespace Tailslide {

bool ConstantDeterminingVisitor::beforeDescend(LSLASTNode *node) {
  // invalidate any old constant value we had, it might not be valid anymore
  if (!node->isStatic() && node->getNodeType() != NODE_CONSTANT) {
    node->setConstantValue(nullptr);
    node->setConstantPrecluded(false);
  }

  if (node->getType() == TYPE(LST_ERROR)) {
    // absolutely no chance of us figuring out a constant value
    // for this node, don't descend.
    node->setConstantPrecluded(true);
    return false;
  }

  // need special iteration order for script nodes
  // don't automatically descend!
  if (node->getNodeType() == NODE_SCRIPT)
    return false;
  return true;
}

bool ConstantDeterminingVisitor::visit(LSLScript *node) {
  // need to iterate over global vars FIRST since expressions in
  // global functions may make use of them.
  LSLASTNode *child = node->getChild(0)->getChildren();
  while (child != nullptr) {
    if (LSLASTNode *gs_child = child->getChild(0)) {
      if (gs_child->getNodeType() == NODE_GLOBAL_VARIABLE)
        gs_child->visit(this);
    }
    child = child->getNext();
  }
  // safe to descend into functions and event handlers now
  visitChildren(node);
  return false;
}

bool ConstantDeterminingVisitor::visit(LSLDeclaration *node) {
  auto *id = (LSLIdentifier *) node->getChild(0);
  LSLASTNode *rvalue = node->getChild(1);
  LSLConstant *cv = nullptr;
  bool cv_precluded = false;
  if (rvalue && rvalue->getNodeType() != NODE_NULL) {
    cv = rvalue->getConstantValue();
    cv_precluded = rvalue->getConstantPrecluded();
  }
  DEBUG(LOG_DEBUG_SPAM, nullptr,
      "set %s const to %p\n",
        id->getName(),
        rvalue ? rvalue->getConstantValue() : nullptr
  );
  auto *sym = id->getSymbol();
  sym->setConstantValue(cv);
  sym->setConstantPrecluded(cv_precluded);
  return false;
}

bool ConstantDeterminingVisitor::visit(LSLExpression *node) {
  int operation = node->getOperation();
  LSLConstant *constant_value = node->getConstantValue();
  DEBUG(
      LOG_DEBUG_SPAM,
      nullptr,
      "expression.determine_value() op=%d cv=%s st=%d\n",
      operation,
      constant_value ? constant_value->getNodeName() : nullptr,
      node->getNodeSubType()
  );

  LSLASTNode *left = node->getChild(0);
  LSLASTNode *right = node->getChild(1);

  if (left->getType() == TYPE(LST_ERROR) || (right && right->getType() == TYPE(LST_ERROR))) {
    node->setConstantPrecluded(true);
    return true;
  }

  // only check normal expressions
  switch (node->getNodeSubType()) {
    case NODE_NO_SUB_TYPE:
    case NODE_CONSTANT_EXPRESSION:
    case NODE_PARENTHESIS_EXPRESSION:
    case NODE_BINARY_EXPRESSION:
    case NODE_UNARY_EXPRESSION:
      break;
    default:
      return true;
  }

  if (operation == 0 || operation == '(')
    constant_value = left->getConstantValue();
  else if (operation == '=') {
    assert(right);
    constant_value = right->getConstantValue();
  } else {
    LSLConstant *c_left = left->getConstantValue();
    LSLConstant *c_right = right ? right->getConstantValue() : nullptr;

    // we need a constant value from the c_left, and if we have a c_right side, it MUST have a constant value too
    if (c_left && (right == nullptr || c_right != nullptr))
      constant_value = _mOperationBehavior->operation(operation, c_left, c_right, node->getLoc());
    else
      constant_value = nullptr;
  }
  node->setConstantValue(constant_value);
  return true;
}

bool ConstantDeterminingVisitor::visit(LSLGlobalVariable *node) {
  // if it's initialized, set its constant value
  auto *identifier = (LSLIdentifier *) node->getChild(0);
  auto *sym = identifier->getSymbol();
  LSLASTNode *rvalue = node->getChild(1);
  if (rvalue) {
    sym->setConstantValue(rvalue->getConstantValue());
    sym->setConstantPrecluded(rvalue->getConstantPrecluded());
  }
  return true;
}

bool ConstantDeterminingVisitor::visit(LSLLValueExpression *node) {
  auto *id = (LSLIdentifier*) node->getChild(0);
  LSLSymbol *symbol = id->getSymbol();

  // can't determine value if we don't have a symbol
  if (symbol == nullptr) {
    node->setConstantValue(nullptr);
    node->setConstantPrecluded(true);
    return true;
  }

  auto *member_id = (LSLIdentifier*) node->getChild(1);
  const char *member = nullptr;
  if (member_id && member_id->getNodeType() == NODE_IDENTIFIER)
    member = ((LSLIdentifier *) member_id)->getName();

  LSLConstant *constant_value = nullptr;
  DEBUG(LOG_DEBUG_SPAM, nullptr, "id %s assigned %d times\n", id->getName(), symbol->getAssignments());
  if (symbol->getAssignments() == 0) {
    constant_value = symbol->getConstantValue();
    if (constant_value != nullptr && member != nullptr) { // getting a member
      LSLIType c_type = constant_value->getIType();
      switch (c_type) {
        case LST_VECTOR: {
          auto *c = (LSLVectorConstant *) constant_value;
          auto *v = (Vector3 *) c->getValue();
          assert(v);
          switch (member[0]) {
            case 'x':
              constant_value = _mAllocator->newTracked<LSLFloatConstant>(v->x);
              break;
            case 'y':
              constant_value = _mAllocator->newTracked<LSLFloatConstant>(v->y);
              break;
            case 'z':
              constant_value = _mAllocator->newTracked<LSLFloatConstant>(v->z);
              break;
            default:
              constant_value = nullptr;
          }
          break;
        }
        case LST_QUATERNION: {
          auto *c = (LSLQuaternionConstant *) constant_value;
          auto *v = (Quaternion *) c->getValue();
          assert(v);
          switch (member[0]) {
            case 'x':
              constant_value = _mAllocator->newTracked<LSLFloatConstant>(v->x);
              break;
            case 'y':
              constant_value = _mAllocator->newTracked<LSLFloatConstant>(v->y);
              break;
            case 'z':
              constant_value = _mAllocator->newTracked<LSLFloatConstant>(v->z);
              break;
            case 's':
              constant_value = _mAllocator->newTracked<LSLFloatConstant>(v->s);
              break;
            default:
              constant_value = nullptr;
          }
          break;
        }
        default:
          constant_value = nullptr;
          break;
      }
    }
  }
  node->setConstantValue(constant_value);
  return true;
}

bool ConstantDeterminingVisitor::visit(LSLListExpression *node) {
  LSLASTNode *child = node->getChildren();
  LSLConstant *new_child = nullptr;

  // if we have children
  if (child->getNodeType() != NODE_NULL) {
    // make sure they are all constant
    for (child = node->getChildren(); child; child = child->getNext()) {
      if (!child->isConstant()) {
        node->setConstantPrecluded(child->getConstantPrecluded());
        return true;
      }
    }

    // create assignables for them
    for (child = node->getChildren(); child; child = child->getNext()) {
      if (new_child == nullptr) {
        new_child = child->getConstantValue()->copy(_mAllocator);
      } else {
        new_child->addNextSibling(child->getConstantValue()->copy(_mAllocator));
      }
    }
  }

  // create constant value
  node->setConstantValue(_mAllocator->newTracked<LSLListConstant>(new_child));
  return true;
}

bool ConstantDeterminingVisitor::visit(LSLVectorExpression *node) {
  float v[3];
  int cv = 0;

  for (LSLASTNode *child = node->getChildren(); child; child = child->getNext()) {
    // if we have too many children, make sure we don't overflow cv
    if (cv >= 3)
      return true;

    // all children must be constant
    if (!child->isConstant()) {
      node->setConstantPrecluded(child->getConstantPrecluded());
      return true;
    }

    // all children must be float/int constants - get their val or bail if they're wrong
    switch (child->getConstantValue()->getIType()) {
      case LST_FLOATINGPOINT:
        v[cv++] = ((LSLFloatConstant *) child->getConstantValue())->getValue();
        break;
      case LST_INTEGER:
        v[cv++] = (F32) ((LSLIntegerConstant *) child->getConstantValue())->getValue();
        break;
      default:
        node->setConstantPrecluded(true);
        return true;
    }
  }

  // make sure we had enough children
  if (cv < 3)
    return true;

  // create constant value
  node->setConstantValue(_mAllocator->newTracked<LSLVectorConstant>(v[0], v[1], v[2]));
  return true;
}


bool ConstantDeterminingVisitor::visit(LSLQuaternionExpression *node) {
  float v[4];
  int cv = 0;

  for (LSLASTNode *child = node->getChildren(); child; child = child->getNext()) {
    // if we have too many children, make sure we don't overflow cv
    if (cv >= 4)
      return true;

    // all children must be constant
    if (!child->isConstant()) {
      node->setConstantPrecluded(child->getConstantPrecluded());
      return true;
    }

    // all children must be float/int constants - get their val or bail if they're wrong
    switch (child->getConstantValue()->getIType()) {
      case LST_FLOATINGPOINT:
        v[cv++] = ((LSLFloatConstant *) child->getConstantValue())->getValue();
        break;
      case LST_INTEGER:
        v[cv++] = (F32) ((LSLIntegerConstant *) child->getConstantValue())->getValue();
        break;
      default:
        node->setConstantPrecluded(true);
        return true;
    }
  }

  // make sure we had enough children
  if (cv < 4)
    return true;

  // create constant value
  node->setConstantValue(_mAllocator->newTracked<LSLQuaternionConstant>(v[0], v[1], v[2], v[3]));
  return true;
}

bool ConstantDeterminingVisitor::visit(LSLTypecastExpression *node) {
  // what are we casting to
  auto *child = node->getChild(0);
  auto *val = child->getConstantValue();
  node->setConstantValue(nullptr);
  if (!val) {
    node->setConstantPrecluded(child->getConstantPrecluded());
    return true;
  }
  auto to_type = node->getType();
  node->setConstantValue(_mOperationBehavior->cast(to_type, val, val->getLoc()));
  return true;
}


}
