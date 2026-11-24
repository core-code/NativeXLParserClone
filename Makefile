# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Plain Makefile alternative to CMake.  `make` builds the static library, `make test` runs the tests.
CXX      ?= g++
CXXFLAGS ?= -O2
CXXFLAGS += -std=c++17 -Wall -Wextra -pedantic -Iinclude
BUILD    := build

all: $(BUILD)/libxlparser_clone.a

$(BUILD)/xlparser_clone.o: src/xlparser_clone.cpp src/unicode_tables.inc src/excel_functions.inc include/xlparser_clone.h
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/libxlparser_clone.a: $(BUILD)/xlparser_clone.o
	ar rcs $@ $^

$(BUILD)/run_tests: tests/run_tests.cpp $(BUILD)/libxlparser_clone.a
	$(CXX) $(CXXFLAGS) $< -L$(BUILD) -lxlparser_clone -o $@

$(BUILD)/thread_test: tests/thread_test.cpp $(BUILD)/libxlparser_clone.a
	$(CXX) $(CXXFLAGS) $< -L$(BUILD) -lxlparser_clone -pthread -o $@

test: $(BUILD)/run_tests $(BUILD)/thread_test
	$(BUILD)/run_tests tests/data/results.txt
	$(BUILD)/thread_test

clean:
	rm -rf $(BUILD)

.PHONY: all test clean
