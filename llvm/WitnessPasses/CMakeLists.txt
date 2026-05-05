cmake_minimum_required(VERSION 3.16)
project(WitnessPasses LANGUAGES CXX)

set(LLVM_DIR "/workspaces/codespaces-blank/llvm-project/build/lib/cmake/llvm")

# Find Z3
find_package(Z3 REQUIRED)

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
  src/GVNMappingWitness.cpp
  src/GVNWitnessPass.cpp
  src/GVNLocal.cpp
)
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

target_include_directories(WitnessPasses PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${Z3_INCLUDE_DIR}
)

# Link Z3 library
target_link_libraries(WitnessPasses PRIVATE ${Z3_LIBRARIES})