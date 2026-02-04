#include "Invariant.h"

using namespace llvm;

DenseMap<Function*, DenseMap<BasicBlock*,Invariant*>* >
  InvariantManager::GlobalInvariants;

z3::context* InvariantManager::globalContext(){
  return GlobalContext;
}

z3::context* InvariantManager::GlobalContext = new z3::context();

Invariant* InvariantManager::find(BasicBlock* I){
  Function *F = I->getParent();
  DenseMap<BasicBlock*,Invariant*>* funMap = GlobalInvariants[F];
  if(!funMap){
    return 0;
//    funMap = new DenseMap<BasicBlock*,Invariant*>();
//    (GlobalInvariants[F]) = funMap;
  }
  return (*(GlobalInvariants[F]))[I];
}

void InvariantManager::insert(Invariant* Inv){
  Function *F = Inv->instruction()->getParent();
  DenseMap<BasicBlock*,Invariant*>* funMap = (GlobalInvariants[F]);
  if(!funMap){
    funMap = new DenseMap<BasicBlock*,Invariant*>();
    (GlobalInvariants[F]) = funMap;
  }
  (*funMap)[Inv->instruction()] = Inv;
}

void InvariantManager::dumpInvariants(Function *F){
  errs() << "Invariants:\n";
  DenseMap<BasicBlock*,Invariant*>* funMap = GlobalInvariants[F];
  if(!funMap)
    return;
  for(DenseMap<BasicBlock*,Invariant*>::iterator i = funMap->begin(),
      e = funMap->end(); i != e; ++i){
    errs() << i->first->getName() << ": " <<
        Z3_ast_to_string(*GlobalContext,(i->second)->getInv(0)) << "\n";
  }
}

void InvariantManager::addOrCreate(BasicBlock* I,
    z3::expr InvExprS,z3::expr InvExprT,z3::expr InvExprU,z3::expr InvExprV){
  Invariant* Inv = InvariantManager::find(I);
  if(Inv){
    Inv->addInv(0,InvExprS);
    Inv->addInv(1,InvExprT);
    Inv->addInv(2,InvExprU);
    Inv->addInv(3,InvExprV);
  }
  else{
    Inv = new Invariant(InvExprS, InvExprT, InvExprU, InvExprV, I);
    InvariantManager::insert(Inv);
  }
}

void InvariantManager::reset(){
  GlobalContext = new z3::context();
  GlobalInvariants.clear();
}
