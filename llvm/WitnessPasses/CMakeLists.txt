cmake_minimum_required(VERSION 3.16)
project(WitnessPasses LANGUAGES CXX)

set(LLVM_DIR "/usr/local/llvm-21/lib/cmake/llvm")

# Add the plugin
add_llvm_pass_plugin(WitnessPasses
  src/HelloWorldPass.cpp
  src/AcslDCE.cpp
  src/AcslRegistration.cpp
  src/Invariant.cpp
  src/WitnessChecker.cpp
  src/DSEWitnessPass.cpp
  src/SimplifyCFGWitness.cpp
  src/SimplifyCFGUtils.cpp
  src/CFGMappingWitness.cpp
)
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

target_include_directories(WitnessPasses PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)