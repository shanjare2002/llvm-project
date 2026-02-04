//===- Utils.h - Utility functions ------------------------------*- C++ -*-===//
//
//         The LLVM Compiler Infrastructure - CSFV Annotation Framework
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Utils file long description
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringRef.h"

using namespace llvm;

namespace
{

StringRef concatStrings(StringRef P,StringRef S){
  std::stringstream ss;
  ss << P.data() << S.data();
  return ss.str();
}

//std::string concatStdStrings(std::string P,std::string S){
//  std::stringstream ss;
//  ss << P.c_str() << S.c_str();
//  return ss.str();
//}

//inst_iterator inst_it(Instruction* I){
//  Function *F = I->getParent()->getParent();
//  for(inst_iterator i = inst_begin(F), e = inst_end(F); i != e; ++i)
//    if(I == &*i)
//      return i;
//  assert(false && "unreachable!");
//  return inst_end(F);
//}

}//end of anonymous namespace
