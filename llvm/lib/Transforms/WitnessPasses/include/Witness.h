//===- Witness.h - Witness for Transformation passes ------------*- C++ -*-===//
//
//         The LLVM Compiler Infrastructure - CSFV Annotation Framework
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements a witness to:
// a) check that a transformation is correct
// b) propagate annotations through a transformation
//
//===----------------------------------------------------------------------===//

/*
Label instructions
Create set
Simulate(?)
Evaluate
*/

#include "llvm/IR/Value.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"
#include "z3++.h"

using namespace llvm;

namespace
{
class State;

class Witness{

public:
  Witness(){
    It = States.begin();
  }
  ~Witness(){

  }

  State* getState(Value* Key){
    return States[Key];
  }

  void addState(Value* Key, State* Val){
    States[Key] = Val;
  }

  void reset(){
    It = States.begin();
  }

  Value* getNext(){
    if(It == States.end())
      return 0;
    Value* ret = It->first;
    It++;
    return ret;
  }

private:
  //Key is the label, Value is the state
  DenseMap<Value*,State*> States;
  DenseMapIterator<Value *, State *, DenseMapInfo<Value*>, 0 > It;
};

/*
 * \brief The class exposes a state, i.e. a set of invariants.
 * A state is a set of assignments for every variable at a specified point
 * of the program
 */
class State {
public:
  State():Values(),It(Values.begin()){
  }

  ~State(){

  }

  Constant* getValue(StringRef Key){
    return Values[Key];
  }

  void addValue(StringRef Key, Constant* Val){
    Values[Key] = Val;
    It = Values.begin();
  }

  void reset(){
    It = Values.begin();
  }

  StringRef getNext(){
    if(It == Values.end())
      return 0;
    StringRef ret = It->first();
    It++;
    return ret;
  }

  long label(){
    return Label;
  }

  void label(long l){
    Label = l;
  }

private:
  //Key is the variable, Value is the value
  StringMap<Constant*> Values;
  StringMapIterator<Constant*> It;
  long Label;
//  DenseMap<Value*,Constant*> Values;
//  DenseMapIterator<Value *, Constant *, DenseMapInfo<Value *>, 0 > It;
};

class WitnessRelation{
public:
  WitnessRelation(z3::context *c):C(c),Witness(c->bool_val(false)),W(*c){
  }

  void buildRelation(unsigned arity){
    z3::sort range = C->bool_sort();
    Z3_sort* domain = new Z3_sort[arity*2];
    for (unsigned i = 0; i < arity*2; i++) {
        domain[i] = C->int_sort();
    }
    W = z3::func_decl(*C,Z3_mk_func_decl(*C, C->str_symbol("W"),
        arity*2, domain, range));
    delete[] domain;
  }

  /*
   * \brief dump internal objects (debug method)
   */
  void dump(){
    errs() << "W: " << Z3_ast_to_string(*C,W) << "\n";
    errs() << "Witness: " << Z3_ast_to_string(*C,Witness) << "\n";
  }

  /*
   * \brief add a witness conditon to the transition relation
   */
  void addWitness(z3::expr transition){
    Witness = (Witness || transition).simplify();
  }

  /*
   * \brief return the witness relation of the transition system in an
   * OR formula
   */
  z3::expr getWitness(){
    return Witness;
  }

  z3::func_decl getWFunction(){
    return W;
  }
private:

  z3::context* C;
  z3::expr Witness;
  z3::func_decl W; //Witness Relation

};
}//end of anonymous namespace
