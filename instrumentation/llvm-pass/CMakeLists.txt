cmake_minimum_required(VERSION 3.13)
project(CFlatPass)

find_package(LLVM REQUIRED CONFIG)

add_library(CFlatPass MODULE CFlatPass.cpp)

target_include_directories(CFlatPass PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(CFlatPass PRIVATE ${LLVM_DEFINITIONS})
target_compile_features(CFlatPass PRIVATE cxx_std_14)

# Remove 'lib' prefix
set_target_properties(CFlatPass PROPERTIES PREFIX "")
