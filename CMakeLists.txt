# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

cmake_minimum_required(VERSION 3.10)
project(XLParserClone CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(XLPARSERCLONE_BUILD_TESTS "Build the test programs" ON)

add_library(xlparser_clone src/xlparser_clone.cpp)
target_include_directories(xlparser_clone PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
if(MSVC)
  target_compile_options(xlparser_clone PRIVATE /W4 /utf-8 /bigobj)
else()
  target_compile_options(xlparser_clone PRIVATE -Wall -Wextra -pedantic)
endif()

if(XLPARSERCLONE_BUILD_TESTS)
  enable_testing()
  find_package(Threads)
  add_executable(run_tests tests/run_tests.cpp)
  target_link_libraries(run_tests PRIVATE xlparser_clone)
  add_executable(thread_test tests/thread_test.cpp)
  target_link_libraries(thread_test PRIVATE xlparser_clone Threads::Threads)
  add_test(NAME reference_results COMMAND run_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/results.txt)
  add_test(NAME thread_safety COMMAND thread_test)
endif()
