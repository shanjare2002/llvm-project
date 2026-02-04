//===- TransitionSystem.h - Transition System definition --------*- C++ -*-===//
//
//         The LLVM Compiler Infrastructure - CSFV Annotation Framework
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// A transition system is given by a tuple (S,I,T), where S is a set of states,
// I is the subset of initial states and T = S x S is a transition relation
//
//===----------------------------------------------------------------------===//

#include "z3++.h"

#include "Utils.h"
#include "Z3Utils.h"

using namespace llvm;

namespace
{
class TransitionSystem{
public:

  TransitionSystem(z3::context* c,StringRef ch):
    id(ch),
    C(c),
    States(c->bool_val(false)),
    InitialStates(c->bool_val(false)),
    TransitionConditions(c->bool_val(false)),
    S(*c),I(*c),T(*c){}

  ~TransitionSystem(){}

  /*
   * \brief Initializes the predicates relations.
   * \par arity is the number of variables of the system (included the
   * location variable π)
   */
  void buildPredicates(unsigned arity){
    z3::sort range = C->bool_sort();
    Z3_sort* domain = new Z3_sort[arity*2];
    for (unsigned i = 0; i < arity*2; i++) {
        domain[i] = C->int_sort();
    }
    S = z3::func_decl(*C,Z3_mk_func_decl(*C, C->str_symbol(concatStrings("S",id)
        .data()), arity, domain, range));
    I = z3::func_decl(*C,Z3_mk_func_decl(*C, C->str_symbol(concatStrings("I",id)
        .data()), arity, domain, range));
    T = z3::func_decl(*C,Z3_mk_func_decl(*C, C->str_symbol(concatStrings("T",id)
        .data()), arity*2, domain, range));
    delete[] domain;
  }

  /*
   * \brief add a state to the set of states
   */
  void addState(z3::expr state, bool isInitial){
    States = (States || state).simplify();
    if(isInitial)
      InitialStates = (InitialStates || state).simplify();
  }

  /*
   * \brief add a transition conditon to the transition relation
   */
  void addTransition(z3::expr transition){
    TransitionConditions = (TransitionConditions || transition).simplify();
  }

  /*
   * \brief return all the states of the transition system in an OR formula
   */
  z3::expr getStates(){
    return States;
  }

  /*
   * \brief return all the initial states of the transition system in
   * an OR formula
   */
  z3::expr getInitialStates(){
    return InitialStates;
  }

  /*
   * \brief return the transition relation of the transition system in
   * an OR formula
   */
  z3::expr getTransition(){
    return TransitionConditions;
  }

  z3::func_decl getTFunction(){
    return T;
  }

  /*
   * \brief dump internal objects (debug method)
   */
  void dump(){
    errs() << "S: " << Z3_ast_to_string(*C,S) << "\n";
    errs() << "I: " << Z3_ast_to_string(*C,I) << "\n";
    errs() << "T: " << Z3_ast_to_string(*C,T) << "\n";
    errs() << "States: " << Z3_ast_to_string(*C,States) << "\n";
    errs() << "Initial States: "
        << Z3_ast_to_string(*C,InitialStates) << "\n";
    errs() << "Transition Conditions: "
        << Z3_ast_to_string(*C,TransitionConditions) << "\n";
  }

private:
  StringRef id;
  z3::context* C;

  z3::expr States;
  z3::expr InitialStates;
  z3::expr TransitionConditions;

  z3::func_decl S; //states
  z3::func_decl I; //initial states
  z3::func_decl T; //transition relation
};
}//end of anonymous namespace
