//===- WitnessChecker.h - Checker for the witness validity ------*- C++ -*-===//
//
//         The LLVM Compiler Infrastructure - CSFV Annotation Framework
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// TODO: rewrite
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "acsl"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/Support/raw_ostream.h"

#include "TransitionSystem.h"

#include "z3++.h"

#include <map>
#include <vector>

using namespace llvm;

namespace {

#define STATE_S "s_" // source
#define STATE_T "t_" // source next
#define STATE_U "u_" // target
#define STATE_V "v_" // target next
#define REL_A "a_"   // target next
#define REL_B "b_"   // target next
#define PI "π"       // location variable

class WitnessChecker {
public:
  WitnessChecker(Function *fun, z3::context *con)
      : F(fun), C(con), InSbu(*con), InSbv(*con), InSas(*con), InSat(*con),
        InIbu(*con), InIas(*con), Ra(*con), Rb(*con), Wus(*con), Wvt(*con),
        Wut(*con), Wvs(*con) {
    createVarsAndLabels();
    buildBlockRelation(false);
  }
  ~WitnessChecker() {}

  /*
   * \brief create a condition expression from a IcmpInst
   * \par CI source Instruction
   * \par p prefix for the variable name
   */
  z3::expr createCondExpr(ICmpInst *CI, StringRef p = "") {
    z3::expr CondExpr(*C);
    z3::expr a = createIntExpr(CI->getOperand(0), C, p);
    z3::expr b = createIntExpr(CI->getOperand(1), C, p);
    switch (CI->getPredicate()) {
    case CmpInst::ICMP_EQ: //==
      CondExpr = a == b;
      break;
    case CmpInst::ICMP_NE: //!=
      CondExpr = a != b;
      break;
    case CmpInst::ICMP_UGT:
    case CmpInst::ICMP_SGT: //>
      CondExpr = a > b;
      break;
    case CmpInst::ICMP_UGE:
    case CmpInst::ICMP_SGE: //>=
      CondExpr = a >= b;
      break;
    case CmpInst::ICMP_ULT:
    case CmpInst::ICMP_SLT: //<
      CondExpr = a < b;
      break;
    case CmpInst::ICMP_ULE:
    case CmpInst::ICMP_SLE: //<=
      CondExpr = a <= b;
      break;
    default:
      assert(false && "unreachable");
      break;
    }
    return CondExpr;
  }

  /*
   * \brief build the expression that represent the set of states of a function
   * \par c z3 context
   * \par f considered function
   * \par isTarget true if it is the target relation, false if is the source
   */
  void buildBlockRelation(bool isTarget) {
    std::vector<StringRef> Vars;
    if (isTarget) {
      createTargetVars();
      Vars = TargetVars;
    } else
      Vars = SourceVars;
    StringRef rPrefix = isTarget ? REL_A : REL_B; // prefix for the relation
    StringRef nPrefix = isTarget ? STATE_V : STATE_T;
    StringRef pPrefix = isTarget ? STATE_U : STATE_S;
    z3::expr piPrev = isTarget
                          ? C->int_const(concatStrings(STATE_U, PI).data())
                          : C->int_const(concatStrings(STATE_S, PI).data());
    z3::expr piNext = isTarget
                          ? C->int_const(concatStrings(STATE_V, PI).data())
                          : C->int_const(concatStrings(STATE_T, PI).data());
    z3::expr R = C->bool_val(false);
    //    z3::expr eqnp = createEqList(C,Vars,pPrefix,nPrefix,PI);

    for (Function::iterator blk = F->begin(), e = F->end(); blk != e; ++blk) {
      BasicBlock *Blk = &*blk;
      int L1 = StateLabels[Blk];
      z3::expr T = piPrev == L1;
      // building preserve set
      std::vector<StringRef> Preserve;
      for (std::vector<StringRef>::iterator it = Vars.begin(); it != Vars.end();
           ++it) {
        Preserve.push_back(*it);
      }
      Preserve.erase(std::remove(Preserve.begin(), Preserve.end(), PI),
                     Preserve.end());

      for (BasicBlock::iterator i = Blk->begin(), e = Blk->end(); i != e; ++i) {
        Instruction *I = &*i;
        BranchInst *BI;
        ReturnInst *RI;
        BinaryOperator *BO;
        ICmpInst *CI;
        PHINode *PHI;
        if (I == &*inst_begin(F)) { // initial state. must fill inI expressions
        }
        if ((BI = dyn_cast<BranchInst>(I))) {
          // can have more successors
          if (BI->isConditional()) {
            z3::expr CondExpr(*C);
            if ((CI = dyn_cast<ICmpInst>(BI->getCondition())))
              CondExpr = createCondExpr(CI, pPrefix);
            else
              CondExpr = createBoolExpr(BI->getCondition(), C, pPrefix);
            BasicBlock *TrueBlk = &*BI->getSuccessor(0);
            BasicBlock *FalseBlk = &*BI->getSuccessor(1);
            T = T && ((piNext == StateLabels[TrueBlk] && CondExpr) ||
                      (piNext == StateLabels[FalseBlk] && !CondExpr));
          } else { // has one successor
            BasicBlock *NextBlk = &*BI->getSuccessor(0);
            T = T && piNext == StateLabels[NextBlk];
          }
          T = T && createEqList(C, Preserve, pPrefix, nPrefix);
        } else if ((RI = dyn_cast<ReturnInst>(I))) { // final state
          T = T && piNext == -1 && createEqList(C, Preserve, pPrefix, nPrefix);
        } else { // binop, icmp or phi, just one successor.
          // \pi=l and \pi'=m and w'=e(x1,...,xN) together with the conjunction
          // of terms v'=v for all variables in scope which are different from w
          z3::expr AssignExpr(*C);
          z3::expr r = createIntExpr(I, C, nPrefix);
          std::vector<StringRef>::iterator eraseIt = Preserve.begin();
          Preserve.erase(
              std::remove(Preserve.begin(), Preserve.end(), I->getName()),
              Preserve.end());
          if ((BO = dyn_cast<BinaryOperator>(I))) {
            // TODO: duplicare next prev
            z3::expr a = createIntExpr(BO->getOperand(0), C, pPrefix);
            z3::expr b = createIntExpr(BO->getOperand(1), C, pPrefix);
            switch (BO->getOpcode()) {
            case Instruction::Add:
              AssignExpr = r == (a + b);
              break;
            case Instruction::Sub:
              AssignExpr = r == (a - b);
              break;
            case Instruction::Mul:
              AssignExpr = r == (a * b);
              break;
            case Instruction::UDiv:
            case Instruction::SDiv:
              AssignExpr = r == (a / b);
              break;
            default:
              assert(false && "unreachable");
              break;
            }
          } else if ((CI = dyn_cast<ICmpInst>(I))) {
            z3::expr CondExpr = createCondExpr(CI, pPrefix);
            AssignExpr = C->bool_val(true);
            AssignExpr = (r != 0 && CondExpr) || (r == 0 && !CondExpr);
          } else if ((PHI = dyn_cast<PHINode>(I))) {
            AssignExpr =
                r == createIntExpr(PHI->getIncomingValue(0), C, nPrefix) ||
                r == createIntExpr(PHI->getIncomingValue(1), C, nPrefix);
          }
          T = T && (AssignExpr);
        }
      } // instr
      R = R || T.simplify();
      R = R.simplify();
    } // block
    if (isTarget)
      Rb = R;
    else
      Ra = R;
  }

  /*
   * \brief Create an array of Z3::expr objects containing all the variables
   */
  
  z3::expr *createVarArray(StringRef p, std::vector<StringRef> &varsVec) {
    void *rawMemory = operator new[]((int)varsVec.size() * sizeof(z3::expr));
    z3::expr *vars = (z3::expr *)rawMemory;
    for (int i = 0; i < (int)varsVec.size(); ++i) {
      new (&vars[i]) z3::expr(
          C->int_const(concatStrings(p, (StringRef)varsVec[i]).data()));
    }
    return vars;
    // TODO: don't know how to free this weird-allocated memory. Stupid C++
  }

  bool checkWitness() {
    bool checked = false;
    z3::expr *tVars = createVarArray(STATE_T, SourceVars);
    z3::expr e = exists((unsigned)SourceVars.size(), tVars, Wvt && Ra);
    e = implies(Wus && Rb, e);

    DEBUG(errs() << Z3_ast_to_string(*C, e) << "\n");
    z3::params par(*C);
    z3::solver s(*C); // = tact.mk_solver();
    s.add(!e);
    switch (s.check()) {
    case z3::unsat:
      DEBUG(errs() << "unsat: e is valid!\n");
      checked = true;
      break;
    case z3::unknown:
      DEBUG(errs() << "unknown\n");
      DEBUG(errs() << s.reason_unknown() << "\n");
      break;
    case z3::sat:
      errs() << "sat: e is not valid. counterExample\n";
      z3::model m = s.get_model();
      // traversing the model
      for (unsigned i = 0; i < m.size(); i++) {
        z3::func_decl v = m[i];
        // this problem contains only constants
        assert(v.arity() == 0);
        DEBUG(errs() << v.name().str() << " = "
                     << Z3_ast_to_string(*C, m.get_const_interp(v)) << "\n");
      }
      break;
    }
    return checked;
  }

  // TODO: implement the ranking function!
  bool checkStutteringWitness() {
    bool checked = false;
    z3::expr *tVars = createVarArray(STATE_T, SourceVars);
    // step simulation case:
    z3::expr e1 = exists((unsigned)SourceVars.size(), tVars, Wvt && Ra);
    // stuttering on B case:
    z3::expr e2 = exists((unsigned)SourceVars.size(), tVars, Wut && Ra);
    // stuttering on A case:
    z3::expr e3 = exists((unsigned)SourceVars.size(), tVars, Wvs);
    z3::expr e = implies(Wus && Rb, e1 || e2 || e3);

    DEBUG(errs() << Z3_ast_to_string(*C, e) << "\n");
    z3::params par(*C);
    z3::solver s(*C); // = tact.mk_solver();
    s.add(!e);
    switch (s.check()) {
    case z3::unsat:
      DEBUG(errs() << "unsat: e is valid!\n");
      checked = true;
      break;
    case z3::unknown:
      DEBUG(errs() << "unknown\n");
      DEBUG(errs() << s.reason_unknown() << "\n");
      break;
    case z3::sat:
      errs() << "sat: e is not valid. counterExample\n";
      z3::model m = s.get_model();
      // traversing the model
      for (unsigned i = 0; i < m.size(); i++) {
        z3::func_decl v = m[i];
        // this problem contains only constants
        assert(v.arity() == 0);
        DEBUG(errs() << v.name().str() << " = "
                     << Z3_ast_to_string(*C, m.get_const_interp(v)) << "\n");
      }
      break;
    }
    return checked;

    return checked;
  }

  void propagateInvariants() {
    DEBUG(errs() << "propagating invariants");
    for (Function::iterator blk = F->begin(), e = F->end(); blk != e; ++blk) {
      BasicBlock *Blk = &*blk;
      Invariant *inv = InvariantManager::find(Blk);
      if (inv) {
        z3::expr theta = inv->getInv(0);
        inv->addInv(0, theta && Wus);
        theta = inv->getInv(1);
        inv->addInv(1, theta && Wvt);
        InvariantManager::insert(inv);
      }
    }
  }

  /*
   * \brief write all the expressions in the err output. debug method
   */
  void dumpExpressions() {
    //    errs() << "InIas: " << Z3_ast_to_string(*C,InIas) << "\n";
    errs() << "Ra: " << Z3_ast_to_string(*C, Ra) << "\n";
    //    errs() << "InSas: " << Z3_ast_to_string(*C,InSas) << "\n";
    //    errs() << "InSat: " << Z3_ast_to_string(*C,InSat) << "\n";
    //    errs() << "InIbu: " << Z3_ast_to_string(*C,InIbu) << "\n";
    errs() << "Rb: " << Z3_ast_to_string(*C, Rb) << "\n";
    //    errs() << "InSbu: " << Z3_ast_to_string(*C,InSbu) << "\n";
    //    errs() << "InSbv: " << Z3_ast_to_string(*C,InSbv) << "\n";
    errs() << "Wus: " << Z3_ast_to_string(*C, Wus) << "\n";
    //    errs() << "Wvt: " << Z3_ast_to_string(*C,Wvt) << "\n";
  }

  // getter + setters
  z3::expr wus() { return Wus; }
  z3::expr wvt() { return Wvt; }

  void wus(z3::expr w) { Wus = w; }
  void wvt(z3::expr w) { Wvt = w; }

  z3::expr wut() { return Wut; }
  z3::expr wvs() { return Wvs; }

  void wut(z3::expr w) { Wut = w; }
  void wvs(z3::expr w) { Wvs = w; }

  int getLabel(BasicBlock *I) { return StateLabels[I]; }

  std::vector<StringRef> getSourceVars() { return SourceVars; }

  std::vector<StringRef> getTargetVars() { return TargetVars; }

private:
  Function *F;
  z3::context *C;
  std::vector<StringRef> SourceVars;
  std::vector<StringRef> TargetVars;
  std::vector<BasicBlock *> Labels;
  DenseMap<BasicBlock *, int> StateLabels;
  std::unorderedMap < Basic std::vector<Instruction *> SourceInsts;
  std::vector<Instruction *> TargetInsts;

  // Some of the expressions are duplicated because of the renaming of the
  // variables. this is due to the fact that z3 doesn't provide function or
  // relation definition

  // Sets of states
  z3::expr InSbu;
  z3::expr InSbv;
  z3::expr InSas;
  z3::expr InSat;

  // Subset of initial states
  z3::expr InIbu;
  z3::expr InIas;

  // Transition relations
  z3::expr Ra;
  z3::expr Rb;

  // witness relations
  z3::expr Wus;
  z3::expr Wvt;
  z3::expr Wut;
  z3::expr Wvs;

  void createVarsAndLabels() {
    int c = 0; // counts the variables
    int l = 0; // counts the labels (instruction)
    for (Function::iterator i = F->begin(), e = F->end(); i != e; ++i) {
      BasicBlock *I = &*i;
      errs() << I Labels.push_back(I);
      StateLabels[I] = l;
      ++l;
    }
    for (inst_iterator i = inst_begin(*F), e = inst_end(*F); i != e; ++i) {
      Instruction *I = &*i;
      SourceInsts.push_back(I);
      StringRef s = I->getName();
      if (s != "") {
        std::string *f = new std::string(s.data());
        SourceVars.push_back(*f);
        ++c;
      }
    }
    SourceVars.push_back("π");
  }

  void createTargetVars() {
    std::vector<StringRef> TempVars;
    int c = 0; // counts the variables
    for (inst_iterator i = inst_begin(*F), e = inst_end(*F); i != e; ++i) {
      Instruction *I = &*i;
      TargetInsts.push_back(I);
      StringRef s = I->getName();
      if (s != "") {
        TempVars.push_back(s);
        ++c;
      }
    }
    TempVars.push_back("π");
    TargetVars = TempVars;
  }
};
} // end of anonymous namespace
