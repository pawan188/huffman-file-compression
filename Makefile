# LZ77 + Canonical Huffman compressor.
# Toolchain: MSYS2 UCRT64 g++ (mingw32-make).

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Isrc
BUILD    := build

.RECIPEPREFIX := >

TESTS := $(BUILD)/test_bitio.exe $(BUILD)/test_huffman.exe

.PHONY: all test clean
all: $(TESTS)

$(BUILD):
>mkdir "$(BUILD)"

$(BUILD)/test_bitio.exe: src/bitio.cpp tests/test_bitio.cpp src/bitio.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) src/bitio.cpp tests/test_bitio.cpp -o $@

$(BUILD)/test_huffman.exe: src/bitio.cpp src/huffman.cpp tests/test_huffman.cpp src/bitio.hpp src/huffman.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) src/bitio.cpp src/huffman.cpp tests/test_huffman.cpp -o $@

test: $(TESTS)
>@$(BUILD)/test_bitio.exe
>@$(BUILD)/test_huffman.exe

clean:
>rmdir /s /q "$(BUILD)"