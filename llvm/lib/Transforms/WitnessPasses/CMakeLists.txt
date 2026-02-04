cmake_minimum_required(VERSION 3.13)
project(HelloWorldVerifier LANGUAGES CXX)

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)

add_llvm_pass_plugin(HelloWorldVerifier src/HelloWorldPass.cpp)
