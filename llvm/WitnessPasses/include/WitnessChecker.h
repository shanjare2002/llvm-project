//===- WitnessChecker.h - Checker for the witness validity ------*- C++ -*-===//
//
//         The LLVM Compiler Infrastructure - CSFV Annotation Framework
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the WitnessChecker class for verifying refinement witnesses
// between source and target programs using Z3-based symbolic verification.
//
//===----------------------------------------------------------------------===//

#ifndef WITNESSCHECKER_H
#define WITNESSCHECKER_H

#define DEBUG_TYPE "acsl"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "Invariant.h"
#include "TransitionSystem.h"
#include "z3++.h"

#include <map>
#include <unordered_set>
#include <vector>
using namespace llvm;

// State variable prefixes for source and target programs
// Naming convention: S/U for source states, T/V for target states
#define STATE_S "S_" // Source current state
#define STATE_U "U_" // Source next state
#define STATE_T "T_" // Target current state
#define STATE_V "V_" // Target next state

// Transition relation prefixes
#define REL_SOURCE "Rs_" // Source transition relation
#define REL_TARGET "Rt_" // Target transition relation

// Special variables
#define PC_VAR                                                                 \
  "π" // Location (program counter) variable (renamed from PI to avoid
      // conflicts)

/// WitnessChecker validates refinement witnesses between source and target
/// programs.
///
/// State encoding:
///   S - Source current state (variables with S_ prefix)
///   U - Source next state (variables with U_ prefix)
///   T - Target current state (variables with T_ prefix)
///   V - Target next state (variables with V_ prefix)
class WitnessChecker {
public:
  /// Constructor: Initialize witness checker for a function
  WitnessChecker(Function *fun, z3::context *con);

  ~WitnessChecker() {}

  /// Build a Z3 expression for binary operations
  z3::expr buildBinaryExpr(StringRef op, z3::expr a, z3::expr b);

  /// Create a Z3 condition expression from an LLVM ICmpInst
  z3::expr createCondExpr(ICmpInst *CI, StringRef p = "");

  /// Build the transition relation for source or target program
  void buildBlockRelation(bool isTarget);

  /// Create a Z3 array for variable quantification
  z3::expr *createVarArray(StringRef prefix, std::vector<StringRef> &varsVec);

  /// Check validity of the refinement witness
  bool checkWitness();

  /// Check validity of stuttering witness (allows stuttering steps)
  bool checkStutteringWitness();

  /// Propagate invariants with witness relations
  void propagateInvariants();

  /// Debug method: dump all Z3 expressions to stderr
  void dumpExpressions();

  // ===== Getters and Setters =====

  z3::expr getWitnessST() { return WitnessST; }
  z3::expr getWitnessVU() { return WitnessVU; }
  z3::expr getWitnessTU() { return WitnessTU; }
  z3::expr getWitnessVS() { return WitnessVS; }

  void setWitnessST(z3::expr w) { WitnessST = w; }
  void setWitnessVU(z3::expr w) { WitnessVU = w; }
  void setWitnessTU(z3::expr w) { WitnessTU = w; }
  void setWitnessVS(z3::expr w) { WitnessVS = w; }

  int getBlockLabel(BasicBlock *block) { return StateLabels[block]; }
  std::vector<StringRef> getArgs();
  std::vector<StringRef> getSourceVars() { return SourceVars; }
  std::vector<StringRef> getTargetVars() { return TargetVars; }
  std::vector<StringRef> getLiveVarsAtBlock(BasicBlock *block);

  DenseMap<BasicBlock *, int> StateLabels;

private:
  // ===== Core Data Members =====

  Function *F;    // The function being analyzed
  z3::context *C; // Z3 context for symbolic operations

  // Variable sets
  std::vector<StringRef> SourceVars; // Variables in source program
  std::vector<StringRef> TargetVars; // Variables in target program

  // Basic block labels and instructions
  std::vector<BasicBlock *> Labels;
  // Maps blocks to numeric labels
  std::vector<Instruction *> SourceInsts;
  std::vector<Instruction *> TargetInsts;

  // ===== Z3 Expressions =====

  // State sets (currently unused but kept for future use)
  z3::expr InSbu; // States in source program (B, U states)
  z3::expr InSbv; // States in source program (B, V states)
  z3::expr InSas; // States in source program (A, S states)
  z3::expr InSat; // States in source program (A, T states)

  // Initial state subsets
  z3::expr InIbu; // Initial states (B, U)
  z3::expr InIas; // Initial states (A, S)

  // Transition relations
  z3::expr SourceRelation; // Rs(S, U) - Source program transition relation
  z3::expr TargetRelation; // Rt(T, V) - Target program transition relation

  // Witness relations
  z3::expr WitnessST; // W(S, T) - Relates source current to target current
  z3::expr WitnessVU; // W(V, U) - Relates target next to source next
  z3::expr
      WitnessTU; // W(T, U) - For stuttering (target current to source next)
  z3::expr
      WitnessVS; // W(V, S) - For stuttering (target next to source current)

  // Global memory array (int index -> int value) shared across the checker
  // Declared after context pointer C
  z3::sort IdxSort; // Index sort (int)
  z3::sort ValSort; // Value sort (int)
  z3::expr MemorySource;
  z3::expr MemoryTarget; // Global memory array

  // ===== Helper Methods =====
  struct MemoryState {
    int currentLevel;
    z3::expr memoryArray;
  };
  /// Configuration for state encoding
  struct StateConfig {
    int currMemoryLevel;
    std::vector<StringRef> vars;
    std::map<StringRef, int> ptrMap;
    std::map<int, StringRef> stackMap;
    StringRef currPrefix;
    StringRef nextPrefix;
    z3::expr piCurr;
    z3::expr piNext;
    z3::expr currMemory;
    z3::expr nextMemory;
    bool isTarget;
    StateConfig(z3::context *ctx)
        : piCurr(*ctx), piNext(*ctx), currMemory(*ctx), nextMemory(*ctx) {}
  };

  // Helper method declarations
  StateConfig initializeStateConfig(bool isTarget);

  z3::expr processBasicBlock(BasicBlock *block, StateConfig &config);
  std::vector<StringRef>
  createPreservationSet(const std::vector<StringRef> &vars);
  z3::expr processInstruction(Instruction *inst, z3::expr blockTransition,
                              std::vector<StringRef> &preserveVars,
                              StateConfig &config);
  z3::expr processStoreInstruction(StoreInst *storeInst,
                                   z3::expr blockTransition,
                                   std::vector<StringRef> &preserveVars,
                                   StateConfig &config);
  z3::expr processAllocaInstruction(AllocaInst *allocaInst,
                                    z3::expr blockTransition,
                                    const std::vector<StringRef> &preserveVars,
                                    StateConfig &config);
  z3::expr processBranchInstruction(BranchInst *branchInst,
                                    z3::expr blockTransition,
                                    const std::vector<StringRef> &preserveVars,
                                    const StateConfig &config);
  z3::expr createBranchCondition(BranchInst *branchInst, StringRef prefix);
  z3::expr processReturnInstruction(ReturnInst *returnInst,
                                    z3::expr blockTransition,
                                    const std::vector<StringRef> &preserveVars,
                                    const StateConfig &config);
  z3::expr processLoadInstruction(LoadInst *loadInst, z3::expr blockTransition,
                                  StateConfig &config);
  z3::expr processAssignmentInstruction(Instruction *inst,
                                        z3::expr blockTransition,
                                        std::vector<StringRef> &preserveVars,
                                        const StateConfig &config);
  z3::expr createAssignmentExpr(Instruction *inst, z3::expr resultExpr,
                                const StateConfig &config);
  z3::expr createBinaryOpExpr(BinaryOperator *binOp, z3::expr resultExpr,
                              StringRef prefix);
  z3::expr createComparisonExpr(ICmpInst *cmpInst, z3::expr resultExpr,
                                StringRef prefix);
  z3::expr createPhiExpr(PHINode *phiNode, z3::expr resultExpr,
                         StringRef prefix);
  z3::expr createSelectExpr(SelectInst *selInst, z3::expr resultExpr,
                            StringRef prefix);
  z3::expr createStoreExpr(StoreInst *storeInst, z3::context *C,
                           WitnessChecker::StateConfig &config);
  void createVarsAndLabels();
  void createTargetVars();
};

#endif // WITNESSCHECKER_H
