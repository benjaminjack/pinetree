cmake_minimum_required(VERSION 3.8.0)

project(pysinthe_test)

# Modern C++ woooo!
set(CMAKE_CXX_STANDARD 11)

# For VSCode debugger support
include(CMakeToolsHelpers OPTIONAL)

# Set up for Catch unit testing
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/src/cmake.modules/")
enable_testing()

# Tell Cmake that headers are also in source directory
include_directories(src)

# Pull in source and test files
set(SOURCE_DIR "src")
set(TEST_DIR "${SOURCE_DIR}/tests")
set(SOURCE 
    "${SOURCE_DIR}/feature.cpp"
    "${SOURCE_DIR}/polymer.cpp"
    "${SOURCE_DIR}/choices.cpp")
set(SOURCE ${SOURCE}
    "${TEST_DIR}/test_main.cpp"
    "${TEST_DIR}/test_feature.cpp"
    "${TEST_DIR}/test_polymer.cpp"
    "${TEST_DIR}/test_choices.cpp")

# Generate shared library since ultimately this code will interface with python
# and won't contain a main executable
# add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES})

add_executable(${PROJECT_NAME} ${SOURCE})

# Automatically parse Catch unit tests
# Run `make; make test` to run all tests after running cmake
include(ParseAndAddCatchTests)
ParseAndAddCatchTests(${PROJECT_NAME})