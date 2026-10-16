cmake_minimum_required(VERSION 3.21)
project(aggregate_model_tests LANGUAGES CXX)

enable_testing()
add_executable(aggregate_model_tests aggregate_model_tests.cpp ../aggregate_model.cpp)
target_compile_features(aggregate_model_tests PRIVATE cxx_std_20)
target_include_directories(aggregate_model_tests PRIVATE ../../../..)
add_test(NAME aggregate_model_tests COMMAND aggregate_model_tests)
