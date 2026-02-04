//===- Invariant.h - Invariant for Transformation passes --------*- C++ -*-===//
//
//         The LLVM Compiler Infrastructure - CSFV Annotation Framework
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Instructions.h"
#include "llvm/IR/Function.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Support/raw_ostream.h"
#include "z3++.h"

using namespace llvm;

class Invariant;

class InvariantManager{
public:
  static Invariant *find(BasicBlock* I);
  static z3::context *globalContext();
  static void insert(Invariant* Inv);
  static void dumpInvariants(Function* F);
  static void addOrCreate(BasicBlock* I,
      z3::expr InvExprS,z3::expr InvExprT,z3::expr InvExpU,z3::expr InvExprV);
  static void reset();
private:
  //map of all the invariants
  static DenseMap<Function*, DenseMap<BasicBlock*,Invariant*>* >GlobalInvariants;
  static z3::context *GlobalContext;
};


class Invariant{

public:
  Invariant(z3::expr es,z3::expr et,z3::expr eu,z3::expr ev,BasicBlock *i)
  :ExprS(es),ExprT(et),ExprU(eu),ExprV(ev),Blk(i){
    InvariantManager::insert(this);
  }

    Invariant(BasicBlock *i):ExprS(*InvariantManager::globalContext()),
      ExprT(*InvariantManager::globalContext()),
      ExprU(*InvariantManager::globalContext()),
      ExprV(*InvariantManager::globalContext()),Blk(i){
      InvariantManager::insert(this);
    }

  ~Invariant(){}

  BasicBlock* instruction(){
    return this->Blk;
  }

  z3::expr getInv(int c){
    switch (c) {
      case 0:
        return this->ExprS;
      case 1:
        return this->ExprT;
      case 2:
        return this->ExprU;
      default:
        return this->ExprV;
    }
  }

  void addInv(int c, z3::expr that){
    switch (c) {
      case 0:
        this->ExprS = (this->ExprS && that).simplify();
        break;
      case 1:
        this->ExprT = (this->ExprT && that).simplify();
        break;
      case 2:
        this->ExprU = (this->ExprU && that).simplify();
        break;
      default:
        this->ExprV = (this->ExprV && that).simplify();
        break;
    }
  }

private:
  z3::expr ExprS;
  z3::expr ExprT;
  z3::expr ExprU;
  z3::expr ExprV;
  BasicBlock* Blk;
};
