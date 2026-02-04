//===- Z3Utils.h - Z3Utils utility functions --------------------*- C++ -*-===//
//
//         The LLVM Compiler Infrastructure - CSFV Annotation Framework
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Contains Z3 utility functions
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/raw_ostream.h"

#include "z3++.h"

#define PC_VAR "π"
#define NEXT "'"
#define TARGET "*"

using namespace llvm;

namespace {

/*
 * \brief create a Z3 integer expression depending on the type of the Value
 * \par V the value to create the expr from
 * \par C z3 context
 * \par p prefix
 */
z3::expr createIntExpr(Value *V, z3::context *C, StringRef p = "") {
  z3::expr Expr(*C);
  Constant *Cnst;
  if ((Cnst = dyn_cast<Constant>(V))) {
    return C->int_val((int)Cnst->getUniqueInteger().getSExtValue());
  } else { // return a variable
    StringRef N = V->getName();
    return C->int_const(concatStrings(p, N).data());
  }
}

/*
 * \brief create a Z3 boolean expression depending on the type of the Value
 * \par V the value to create the expr from
 * \par C z3 context
 * \par p prefix
 */
z3::expr createBoolExpr(Value *V, z3::context *C, StringRef p = "") {
  z3::expr Expr(*C);
  Constant *Cnst;
  if ((Cnst = dyn_cast<Constant>(V)))
    return C->bool_val(Cnst->getUniqueInteger().getSExtValue());
  else { // return a variable
    StringRef N = V->getName();
    return C->bool_const(concatStrings(p, N).data());
  }
}

/*
 * \brief create a list of equalities between two states
 * \par ex (optional) a possible variable to exclude
 * \par pa first prefix
 * \par pb second prefix
 */
z3::expr createEqList(z3::context *C, std::vector<StringRef> Vars, StringRef pa,
                      StringRef pb, StringRef ex1 = "", StringRef ex2 = "") {
  z3::expr Expr = C->bool_val(true);
  for (std::vector<StringRef>::iterator i = Vars.begin(); i != Vars.end();
       ++i) {
    if (ex1 != *i && ex2 != *i) {
      z3::expr A = C->int_const(concatStrings(pa, *i).data());
      z3::expr B = C->int_const(concatStrings(pb, *i).data());
      Expr = Expr && (A == B);
    }
  }
  return Expr.simplify();
}

/*
 * \brief create a list of equalities between two states
 * \par ex (optional) a possible variable to exclude
 * \par pa first prefix
 * \par pb second prefix
 */
// z3::expr createEqList(z3::context* C,std::vector<std::string> Vars,
//     StringRef pa, StringRef pb){
//   z3::expr Expr = C->bool_val(true);
//   for (std::vector<std::string>::iterator i = Vars.begin(); i != Vars.end();
//   ++i){
//     z3::expr A = C->int_const(concatStrings(pa,*i).data());
//     z3::expr B = C->int_const(concatStrings(pb,*i).data());
//     Expr = Expr && (A == B);
//   }
//   return Expr.simplify();
// }

/*
 * \brief create a list of equalities between two states
 * \par ex (optional) a possible variable to exclude
 * \par pa first prefix
 * \par pb second prefix
 */
// z3::expr createEqList(z3::context* C,std::vector<Instruction*> Vars,
//     StringRef pa, StringRef pb){
//   z3::expr Expr = C->bool_val(true);
//   for (std::vector<Instruction*>::iterator i = Vars.begin(); i != Vars.end();
//       ++i){
//     Instruction* I = *i;
//     z3::expr A = C->int_const(concatStrings(pa,I->getName()).data());
//     z3::expr B = C->int_const(concatStrings(pb,I->getName()).data());
//     Expr = Expr && (A == B);
//   }
//   return Expr.simplify();
// }

// inline z3::expr forall(unsigned numArgs, z3::expr *x1, z3::expr b) {
//   Z3_app* vars = new Z3_app[numArgs];
//   for (unsigned i = 0; i < numArgs; ++i) {
//     vars[i] =(Z3_app) x1[i];
//   }
//   Z3_ast r = Z3_mk_forall_const(b.ctx(), 0, numArgs, vars, 0, 0, b);
//   b.check_error();
//   return z3::expr(b.ctx(), r);
// }

inline z3::expr exists(unsigned numArgs, z3::expr *x1, z3::expr b) {
  Z3_app *vars = new Z3_app[numArgs];
  for (unsigned i = 0; i < numArgs; ++i) {
    vars[i] = (Z3_app)x1[i];
  }
  Z3_ast r = Z3_mk_exists_const(b.ctx(), 0, numArgs, vars, 0, 0, b);
  b.check_error();
  return z3::expr(b.ctx(), r);
}

/**
   \brief Display a symbol in the given output stream.
*/
// void display_symbol(Z3_context c, Z3_symbol s)
//{
//     switch (Z3_get_symbol_kind(c, s)) {
//     case Z3_INT_SYMBOL:
//       errs() << "#" << Z3_get_symbol_int(c, s);
//       break;
//     case Z3_STRING_SYMBOL:
//       errs() << Z3_get_symbol_string(c, s);
//       break;
//     default:
//       assert(false && "unreachable!");
//       break;
//     }
// }
//
///**
//   \brief Display the given type.
//*/
// void display_sort(Z3_context c, Z3_sort ty)
//{
//    switch (Z3_get_sort_kind(c, ty)) {
//    case Z3_UNINTERPRETED_SORT:
//      display_symbol(c, Z3_get_sort_name(c, ty));
//      break;
//    case Z3_BOOL_SORT:
//      errs() << "bool";
//      break;
//    case Z3_INT_SORT:
//      errs() << "int";
//      break;
//    case Z3_REAL_SORT:
//      errs() << "real";
//      break;
//    case Z3_BV_SORT:
//      errs() << "bv" << Z3_get_bv_sort_size(c, ty);
//      break;
//    case Z3_ARRAY_SORT:
//      errs() << "[";
//      display_sort(c, Z3_get_array_sort_domain(c, ty));
//      errs() << "->";
//      display_sort(c, Z3_get_array_sort_range(c, ty));
//      errs() << "]";
//      break;
//    case Z3_DATATYPE_SORT:
//    if (Z3_get_datatype_sort_num_constructors(c, ty) != 1)
//    {
//      errs() << Z3_sort_to_string(c,ty);
//      break;
//    }
//    {
//      unsigned num_fields = Z3_get_tuple_sort_num_fields(c, ty);
//      unsigned i;
//      errs() << "(";
//      for (i = 0; i < num_fields; i++) {
//        Z3_func_decl field = Z3_get_tuple_sort_field_decl(c, ty, i);
//        if (i > 0) {
//          errs() << ",";
//        }
//        display_sort(c, Z3_get_range(c, field));
//      }
//      errs() << ")";
//      break;
//    }
//    default:
//      errs() << "unknown[";
//      display_symbol(c, Z3_get_sort_name(c, ty));
//      errs() << "]";
//      break;
//    }
//}

} // end of anonymous namespace
