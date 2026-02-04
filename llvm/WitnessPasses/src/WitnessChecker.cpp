#include <WitnessChecker.h>
using namespace llvm;

// ===== Constructor =====

WitnessChecker::WitnessChecker(Function *fun, z3::context *con)
    : F(fun), C(con), IdxSort(con->int_sort()), ValSort(con->int_sort()),
      MemorySource(
          con->constant("memory_source", con->array_sort(IdxSort, ValSort))),
      MemoryTarget(
          con->constant("memory_target", con->array_sort(IdxSort, ValSort))),
      InSbu(*con), InSbv(*con), InSas(*con), InSat(*con), InIbu(*con),
      InIas(*con), SourceRelation(*con), TargetRelation(*con), WitnessST(*con),
      WitnessVU(*con), WitnessTU(*con), WitnessVS(*con) {
  createVarsAndLabels();
  buildBlockRelation(/*isTarget=*/false);
}

// ===== Public Methods =====

z3::expr WitnessChecker::buildBinaryExpr(StringRef op, z3::expr a, z3::expr b) {
  z3::expr res(*C);
  if (op == "+")
    res = a + b;
  else if (op == "-")
    res = a - b;
  else if (op == "*")
    res = a * b;
  else if (op == "/")
    res = a / b;
  else {
    assert(false && "Unsupported binary operation");
  }
  return res;
}

std::vector<StringRef> WitnessChecker::getArgs() {
  std::vector<StringRef> ArgNames;
  for (Argument &Arg : F->args()) {
    ArgNames.push_back(Arg.getName());
  }
  return ArgNames;
}

z3::expr WitnessChecker::createCondExpr(ICmpInst *CI, StringRef p) {
  z3::expr CondExpr(*C);
  z3::expr a = createIntExpr(CI->getOperand(0), C, p);
  z3::expr b = createIntExpr(CI->getOperand(1), C, p);

  switch (CI->getPredicate()) {
  case CmpInst::ICMP_EQ:
    CondExpr = a == b;
    break;
  case CmpInst::ICMP_NE:
    CondExpr = a != b;
    break;
  case CmpInst::ICMP_UGT:
  case CmpInst::ICMP_SGT:
    CondExpr = a > b;
    break;
  case CmpInst::ICMP_UGE:
  case CmpInst::ICMP_SGE:
    CondExpr = a >= b;
    break;
  case CmpInst::ICMP_ULT:
  case CmpInst::ICMP_SLT:
    CondExpr = a < b;
    break;
  case CmpInst::ICMP_ULE:
  case CmpInst::ICMP_SLE:
    CondExpr = a <= b;
    break;
  default:
    assert(false && "Unsupported comparison predicate");
    break;
  }
  return CondExpr;
}

void WitnessChecker::buildBlockRelation(bool isTarget) {
  errs() << "Building " << (isTarget ? "target" : "source")
         << " transition relation...\n";

  StateConfig config = initializeStateConfig(isTarget);

  z3::expr TransitionRelation = C->bool_val(false);

  for (Function::iterator blk = F->begin(), e = F->end(); blk != e; ++blk) {
    // errs() << isTarget << " " <<  StateLabels[&*blk];
    z3::expr blockTransition = processBasicBlock(&*blk, config);
    TransitionRelation = TransitionRelation || blockTransition.simplify();
    TransitionRelation = TransitionRelation.simplify();
  }

  if (isTarget)
    TargetRelation = TransitionRelation;
  else
    SourceRelation = TransitionRelation;
}

z3::expr *WitnessChecker::createVarArray(StringRef prefix,
                                         std::vector<StringRef> &varsVec) {
  void *rawMemory = operator new[]((int)varsVec.size() * sizeof(z3::expr));
  z3::expr *vars = (z3::expr *)rawMemory;
  for (int i = 0; i < (int)varsVec.size(); ++i) {
    new (&vars[i]) z3::expr(
        C->int_const(concatStrings(prefix, (StringRef)varsVec[i]).data()));
  }
  return vars;
}
/*
bool WitnessChecker::checkCFGwitness(){



}
*/
bool WitnessChecker::checkWitness() {

  bool isValid = false;
  z3::expr *sourceNextVars = createVarArray(STATE_U, SourceVars);

  errs() << "Created source next state variables (U)\n";
  errs() << "Number of variables: " << SourceVars.size() << "\n";
  errs() << "First variable: " << Z3_ast_to_string(*C, sourceNextVars[0])
         << "\n";

  // Build the witness validity condition:
  // W(S,T) ∧ Rt(T,V) ⇒ ∃U. W(V,U) ∧ Rs(S,U)

  z3::expr existentialPart = exists((unsigned)SourceVars.size(), sourceNextVars,
                                    WitnessVU && SourceRelation);
  z3::expr witnessCondition =
      implies(WitnessST && TargetRelation, existentialPart);
  errs() << Z3_ast_to_string(*C, !witnessCondition);
  // Check validity using Z3 solver
  z3::solver solver(*C);

  solver.add(!witnessCondition); // Check if negation is unsatisfiable

  errs() << "Checking witness validity...\n";
  switch (solver.check()) {
  case z3::unsat:
    errs() << "UNSAT: Witness is VALID!\n";
    isValid = true;
    break;

  case z3::unknown:
    errs() << "UNKNOWN: " << solver.reason_unknown() << "\n";
    break;

  case z3::sat:
    errs() << "SAT: Witness is INVALID. Counterexample:\n";
    z3::model model = solver.get_model();
    for (unsigned i = 0; i < model.size(); i++) {
      z3::func_decl v = model[i];
      assert(v.arity() == 0 && "Expected only constants in model");
      errs() << Z3_ast_to_string(*C, v) << " = "
             << Z3_ast_to_string(*C, model.get_const_interp(v)) << "\n";
    }
    break;
  }
  return isValid;
}

bool WitnessChecker::checkStutteringWitness() {

  bool isValid = false;
  z3::expr *sourceNextVars = createVarArray(STATE_U, SourceVars);

  z3::expr stepCase = exists((unsigned)SourceVars.size(), sourceNextVars,
                             WitnessVU && SourceRelation);
  errs() << "here172";

  // z3::expr existentialPart = exists((unsigned)SourceVars.size(),
  // sourceNextVars,
  //                                 WitnessVU && SourceRelation);

  z3::expr targetStutterCase = exists(
      (unsigned)SourceVars.size(), sourceNextVars, WitnessTU && SourceRelation);

  z3::expr sourceStutterCase =
      exists((unsigned)SourceVars.size(), sourceNextVars, WitnessVS);

  z3::expr stutteringCondition =
      implies(WitnessST && TargetRelation,
              stepCase || targetStutterCase || sourceStutterCase);

  errs() << Z3_ast_to_string(*C, targetStutterCase);

  z3::solver solver(*C);
  solver.add(!stutteringCondition);

  errs() << "Checking stuttering witness validity...\n";
  switch (solver.check()) {
  case z3::unsat:
    errs() << "UNSAT: Stuttering witness is VALID!\n";
    isValid = true;
    break;

  case z3::unknown:
    errs() << "UNKNOWN: " << solver.reason_unknown() << "\n";
    break;

  case z3::sat:
    errs() << "SAT: Stuttering witness is INVALID. Counterexample:\n";
    z3::model model = solver.get_model();
    for (unsigned i = 0; i < model.size(); i++) {
      z3::func_decl v = model[i];
      assert(v.arity() == 0 && "Expected only constants in model");
      errs() << v.name().str() << " = "
             << Z3_ast_to_string(*C, model.get_const_interp(v)) << "\n";
    }
    break;
  }
  return isValid;
}

void WitnessChecker::propagateInvariants() {
  errs() << "Propagating invariants...\n";
  for (Function::iterator blk = F->begin(), e = F->end(); blk != e; ++blk) {
    BasicBlock *Blk = &*blk;
    Invariant *inv = InvariantManager::find(Blk);
    if (inv) {
      // Strengthen invariants with witness relations
      z3::expr theta = inv->getInv(0);
      inv->addInv(0, theta && WitnessST);

      theta = inv->getInv(1);
      inv->addInv(1, theta && WitnessVU);

      InvariantManager::insert(inv);
    }
  }
}

void WitnessChecker::dumpExpressions() {
  errs() << "\n=== Dumping Z3 Expressions ===\n";
  errs() << "InIas: " << Z3_ast_to_string(*C, InIas) << "\n";
  errs() << "SourceRelation (Rs): " << Z3_ast_to_string(*C, SourceRelation)
         << "\n";
  errs() << "WitnessTU" << Z3_ast_to_string(*C, WitnessTU) << "\n";
  errs() << "InSas: " << Z3_ast_to_string(*C, InSas) << "\n";
  errs() << "InSat: " << Z3_ast_to_string(*C, InSat) << "\n";
  errs() << "InIbu: " << Z3_ast_to_string(*C, InIbu) << "\n";
  errs() << "TargetRelation (Rt): " << Z3_ast_to_string(*C, TargetRelation)
         << "\n";
  errs() << "InSbu: " << Z3_ast_to_string(*C, InSbu) << "\n";
  errs() << "InSbv: " << Z3_ast_to_string(*C, InSbv) << "\n";
  errs() << "WitnessST (W(S,T)): " << Z3_ast_to_string(*C, WitnessST) << "\n";
  errs() << "WitnessVU (W(V,U)): " << Z3_ast_to_string(*C, WitnessVU) << "\n";
  errs() << "==============================\n\n";
}

// ===== Helper Methods =====

WitnessChecker::StateConfig
WitnessChecker::initializeStateConfig(bool isTarget) {
  StateConfig config(C);

  if (isTarget) {
    createTargetVars();
    config.vars = TargetVars;
    config.currPrefix = STATE_T;
    config.nextPrefix = STATE_V;
    config.piCurr = C->int_const(concatStrings(STATE_T, PC_VAR).data());
    config.piNext = C->int_const(concatStrings(STATE_V, PC_VAR).data());

  } else {
    config.vars = SourceVars;
    config.currPrefix = STATE_S;
    config.nextPrefix = STATE_U;
    config.piCurr = C->int_const(concatStrings(STATE_S, PC_VAR).data());
    config.piNext = C->int_const(concatStrings(STATE_U, PC_VAR).data());
  }
  config.isTarget = isTarget;
  config.currMemoryLevel = 0;
  errs() << "Program counter (current): " << Z3_ast_to_string(*C, config.piCurr)
         << "\n";
  errs() << "Program counter (next): " << Z3_ast_to_string(*C, config.piNext)
         << "\n";

  return config;
}

z3::expr WitnessChecker::processBasicBlock(BasicBlock *block,
                                           StateConfig &config) {
  int blockLabel = StateLabels[block];

  errs() << "Processing block with label: " << blockLabel << "\n";

  z3::expr blockTransition = config.piCurr == blockLabel;

  std::vector<StringRef> preserveVars = createPreservationSet(config.vars);

  for (BasicBlock::iterator i = block->begin(), e = block->end(); i != e; ++i) {
    Instruction *inst = &*i;
    blockTransition =
        processInstruction(inst, blockTransition, preserveVars, config);
  }

  return blockTransition;
}

std::vector<StringRef>
WitnessChecker::createPreservationSet(const std::vector<StringRef> &vars) {
  std::vector<StringRef> preserveVars(vars.begin(), vars.end());
  preserveVars.erase(
      std::remove(preserveVars.begin(), preserveVars.end(), PC_VAR),
      preserveVars.end());
  return preserveVars;
}

z3::expr
WitnessChecker::processInstruction(Instruction *inst, z3::expr blockTransition,
                                   std::vector<StringRef> &preserveVars,
                                   StateConfig &config) {
  if (BranchInst *branchInst = dyn_cast<BranchInst>(inst)) {
    return processBranchInstruction(branchInst, blockTransition, preserveVars,
                                    config);
  } else if (ReturnInst *returnInst = dyn_cast<ReturnInst>(inst)) {
    return processReturnInstruction(returnInst, blockTransition, preserveVars,
                                    config);
  } else if (AllocaInst *allocaInst = dyn_cast<AllocaInst>(inst)) {
    return processAllocaInstruction(allocaInst, blockTransition, preserveVars,
                                    config);
    return blockTransition;
  } else if (StoreInst *storeInst = dyn_cast<StoreInst>(inst)) {
    return processStoreInstruction(storeInst, blockTransition, preserveVars,
                                   config);
    return blockTransition;
  } else if (LoadInst *loadInst = dyn_cast<LoadInst>(inst)) {
    return processLoadInstruction(loadInst, blockTransition, config);
  } else {
    return processAssignmentInstruction(inst, blockTransition, preserveVars,
                                        config);
  }
}
z3::expr WitnessChecker::processLoadInstruction(LoadInst *loadInst,
                                                z3::expr blockTransition,
                                                StateConfig &config) {
  Value *ptr = loadInst->getPointerOperand();
  StringRef ptrName = ptr->getName();
  int memLevel = config.ptrMap[ptrName];

  z3::expr val = C->int_const("dummy");

  if (config.isTarget) {
    val = z3::select(MemoryTarget, C->int_val(memLevel));
    errs() << "stackAfterStore" << Z3_ast_to_string(*C, MemoryTarget) << "\n";

  } else {
    val = z3::select(MemorySource, C->int_val(memLevel));
    errs() << "stackAfterStore" << Z3_ast_to_string(*C, MemorySource) << "\n";
  }
  z3::expr resultVar = createIntExpr(loadInst, C, config.nextPrefix);

  errs() << "Result expression Load inst: " << Z3_ast_to_string(*C, resultVar)
         << "\n";
  return (resultVar == val) && blockTransition;
}
z3::expr WitnessChecker::processStoreInstruction(
    StoreInst *storeInst, z3::expr blockTransition,
    std::vector<StringRef> &preserveVars, StateConfig &config) {

  Value *ptr = storeInst->getPointerOperand();
  Value *val = storeInst->getValueOperand();
  StringRef ptrName = ptr->getName();
  StringRef valName = val->getName();

  errs() << "Processing store instruction: " << *ptr << " = " << *val << "\n";
  int memLevel = config.ptrMap[ptrName];
  if (config.isTarget) {
    MemoryTarget = z3::store(MemoryTarget, C->int_val(memLevel),
                             createIntExpr(val, C, config.currPrefix));
  } else {
    MemorySource = z3::store(MemoryTarget, C->int_val(memLevel),
                             createIntExpr(val, C, config.currPrefix));
  }

  return blockTransition;
}

z3::expr WitnessChecker::processAllocaInstruction(
    AllocaInst *allocaInst, z3::expr blockTransition,
    const std::vector<StringRef> &preserveVars, StateConfig &config) {

  config.ptrMap[allocaInst->getName()] =
      config.currMemoryLevel; // maps pointer name to index this is ok

  config.currMemoryLevel++;
  return blockTransition;
}
z3::expr WitnessChecker::processBranchInstruction(
    BranchInst *branchInst, z3::expr blockTransition,
    const std::vector<StringRef> &preserveVars, const StateConfig &config) {
  if (branchInst->isConditional()) {
    z3::expr condExpr = createBranchCondition(branchInst, config.currPrefix);

    BasicBlock *trueBlock = branchInst->getSuccessor(0);
    BasicBlock *falseBlock = branchInst->getSuccessor(1);

    blockTransition = blockTransition &&
                      ((config.piNext == StateLabels[trueBlock] && condExpr) ||
                       (config.piNext == StateLabels[falseBlock] && !condExpr));
  } else {
    BasicBlock *nextBlock = branchInst->getSuccessor(0);
    blockTransition =
        blockTransition && (config.piNext == StateLabels[nextBlock]);
  }

  return blockTransition &&
         createEqList(C, preserveVars, config.currPrefix, config.nextPrefix);
}

z3::expr WitnessChecker::createBranchCondition(BranchInst *branchInst,
                                               StringRef prefix) {
  if (ICmpInst *cmpInst = dyn_cast<ICmpInst>(branchInst->getCondition())) {
    return createCondExpr(cmpInst, prefix);
  } else {
    return createBoolExpr(branchInst->getCondition(), C, prefix);
  }
}

z3::expr WitnessChecker::processReturnInstruction(
    ReturnInst *returnInst, z3::expr blockTransition,
    const std::vector<StringRef> &preserveVars, const StateConfig &config) {
  // Return instruction: final state (π_next == -1)
  return blockTransition && (config.piNext == -1) &&
         createEqList(C, preserveVars, config.currPrefix, config.nextPrefix);
}

z3::expr WitnessChecker::processAssignmentInstruction(
    Instruction *inst, z3::expr blockTransition,
    std::vector<StringRef> &preserveVars, const StateConfig &config) {
  z3::expr resultExpr = createIntExpr(inst, C, config.nextPrefix);

  errs() << "Processing instruction: " << *inst << "\n";
  errs() << "Result expression: " << Z3_ast_to_string(*C, resultExpr) << "\n";

  // Remove assigned variable from preservation set
  preserveVars.erase(
      std::remove(preserveVars.begin(), preserveVars.end(), inst->getName()),
      preserveVars.end());

  // Build assignment expression based on instruction type
  z3::expr assignExpr = createAssignmentExpr(inst, resultExpr, config);

  return blockTransition && assignExpr;
}

z3::expr WitnessChecker::createAssignmentExpr(Instruction *inst,
                                              z3::expr resultExpr,
                                              const StateConfig &config) {
  if (BinaryOperator *binOp = dyn_cast<BinaryOperator>(inst)) {
    return createBinaryOpExpr(binOp, resultExpr, config.currPrefix);
  } else if (ICmpInst *cmpInst = dyn_cast<ICmpInst>(inst)) {
    return createComparisonExpr(cmpInst, resultExpr, config.currPrefix);
  } else if (PHINode *phiNode = dyn_cast<PHINode>(inst)) {
    return createPhiExpr(phiNode, resultExpr, config.nextPrefix);
  } else if (SelectInst *selInst = dyn_cast<SelectInst>(inst)) {
    // Select: conditional assignment based on boolean condition
    return createSelectExpr(selInst, resultExpr, config.currPrefix);
  } else {

    errs() << "Warning: Unsupported instruction in witness generation: "
           << *inst << "\n";
    errs() << "Opcode: " << Instruction::getOpcodeName(inst->getOpcode())
           << "\n";
    return C->bool_val(true);
  }
}

z3::expr WitnessChecker::createBinaryOpExpr(BinaryOperator *binOp,
                                            z3::expr resultExpr,
                                            StringRef prefix) {
  z3::expr operand1 = createIntExpr(binOp->getOperand(0), C, prefix);
  z3::expr operand2 = createIntExpr(binOp->getOperand(1), C, prefix);

  errs() << "Operand1: " << Z3_ast_to_string(*C, operand1) << "\n";
  errs() << "Operand2: " << Z3_ast_to_string(*C, operand2) << "\n";

  switch (binOp->getOpcode()) {
  case Instruction::Add:
    return resultExpr == (operand1 + operand2);
  case Instruction::Sub:
    return resultExpr == (operand1 - operand2);
  case Instruction::Mul:
    return resultExpr == (operand1 * operand2);
  case Instruction::UDiv:
  case Instruction::SDiv:
    return resultExpr == (operand1 / operand2);
  default:
    assert(false && "Unsupported binary operation");
    return C->bool_val(false);
  }
}

z3::expr WitnessChecker::createComparisonExpr(ICmpInst *cmpInst,
                                              z3::expr resultExpr,
                                              StringRef prefix) {
  z3::expr compExpr = createCondExpr(cmpInst, prefix);
  // Encode result as boolean (0 or non-zero)
  return (resultExpr != 0 && compExpr) || (resultExpr == 0 && !compExpr);
}

z3::expr WitnessChecker::createPhiExpr(PHINode *phiNode, z3::expr resultExpr,
                                       StringRef prefix) {
  // PHI node: result is one of the incoming values
  return resultExpr == createIntExpr(phiNode->getIncomingValue(0), C, prefix) ||
         resultExpr == createIntExpr(phiNode->getIncomingValue(1), C, prefix);
}

z3::expr WitnessChecker::createSelectExpr(SelectInst *selInst,
                                          z3::expr resultExpr,
                                          StringRef prefix) {
  // %y = select i1 %c0, i32 %y1, i32 %y2
  // Encode as: (cond ? result == y1 : result == y2)
  Value *Cond = selInst->getCondition();
  Value *TrueV = selInst->getTrueValue();
  Value *FalseV = selInst->getFalseValue();

  z3::expr condBool(*C);
  if (auto *CI = dyn_cast<ICmpInst>(Cond)) {
    // Condition is a compare: build boolean directly
    condBool = createCondExpr(CI, prefix);
  } else {
    // Generic i1 or integer-encoded boolean: treat non-zero as true
    z3::expr condInt = createIntExpr(Cond, C, prefix);
    condBool = condInt != 0;
  }

  z3::expr trueExpr = createIntExpr(TrueV, C, prefix);
  z3::expr falseExpr = createIntExpr(FalseV, C, prefix);

  return (condBool && (resultExpr == trueExpr)) ||
         (!condBool && (resultExpr == falseExpr));
}

void WitnessChecker::createVarsAndLabels() {
  int variableCount = 0;
  int labelCount = 1;

  // Assign numeric labels to basic blocks
  for (Function::iterator i = F->begin(), e = F->end(); i != e; ++i) {
    BasicBlock *block = &*i;
    Labels.push_back(block);
    StateLabels[block] = labelCount++;
  }

  // Collect function arguments as variables
  for (Argument &arg : F->args()) {
    StringRef argName = arg.getName();
    if (!argName.empty()) {
      std::string *argNameCopy = new std::string(argName.data());
      SourceVars.push_back(*argNameCopy);
      ++variableCount;
    }
  }

  // Collect named instructions as variables
  for (inst_iterator i = inst_begin(*F), e = inst_end(*F); i != e; ++i) {
    Instruction *inst = &*i;
    SourceInsts.push_back(inst);
    StringRef varName = inst->getName();

    if (!varName.empty()) {
      std::string *varNameCopy = new std::string(varName.data());
      SourceVars.push_back(*varNameCopy);
      ++variableCount;
    }
  }

  // Add program counter variable
  SourceVars.push_back("π");
}

void WitnessChecker::createTargetVars() {
  std::vector<StringRef> tempVars;
  int variableCount = 0;

  // Collect function arguments as variables
  for (Argument &arg : F->args()) {
    StringRef argName = arg.getName();
    if (!argName.empty()) {
      tempVars.push_back(argName);
      ++variableCount;
    }
  }

  for (inst_iterator i = inst_begin(*F), e = inst_end(*F); i != e; ++i) {
    Instruction *inst = &*i;
    TargetInsts.push_back(inst);
    StringRef varName = inst->getName();

    if (!varName.empty()) {
      tempVars.push_back(varName);
      ++variableCount;
    }
  }

  // Add program counter variable
  tempVars.push_back("π");
  TargetVars = tempVars;
}