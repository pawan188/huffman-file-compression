# LZ77 + Canonical Huffman compressor (C++)

A from-scratch, DEFLATE-inspired compressor built stage by stage: bit I/O, canonical
Huffman with a run-length-encoded length table, then LZ77 match finding, a container
format, a decompressor, fuzzing, and a benchmark against gzip/zlib.

> **Status: work in progress.** Stages 1-2 are implemented and tested. There is no
> CLI and no LZ77 yet. This README describes the current state and is updated as
> later stages land.

## Design decisions

- **Whole-file, in memory.** The compressor operates on a single in-memory buffer
  (the container is `magic + original length + tables + token stream`). Streaming /
  block framing is explicitly out of scope for v1; it is noted as future work.
- **DEFLATE-inspired, not bit-compatible.** The format borrows DEFLATE's structure
  (canonical Huffman, RLE code-length table, length/dist buckets later) but is not
  wire-compatible with DEFLATE/gzip.
- **Configurable LZ77 window, default 32 KB** (Stage 3).
- No stdlib compression in the compressor itself; zlib/gzip are used only as the
  benchmark baseline in Stage 7.

## Build & test

Toolchain: MSYS2 UCRT64 `g++` (C++17) and `mingw32-make`. From the project root:

```
mingw32-make clean
mingw32-make
mingw32-make test
```

`mingw32-make` builds both test binaries; `mingw32-make test` runs them. Expected
output:

```
ok: 86874 checks passed
ok: 5150 checks passed
```

Both exit with code 0. To build and run a single suite directly:

```
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Isrc src/bitio.cpp tests/test_bitio.cpp -o build/test_bitio.exe
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Isrc src/bitio.cpp src/huffman.cpp tests/test_huffman.cpp -o build/test_huffman.exe
.\build\test_bitio.exe
.\build\test_huffman.exe
```

## Layout

| Path | Purpose |
| --- | --- |
| `src/bitio.hpp` / `.cpp` | LSB-first bit stream reader/writer (byte-buffered, bounds-checked) |
| `src/huffman.hpp` / `.cpp` | Canonical Huffman, length-limited codes, RLE length table, Stage-2 codec |
| `tests/test_bitio.cpp` | Bit I/O unit + randomized round-trip tests |
| `tests/test_huffman.cpp` | Huffman round-trip, Kraft checks, RLE, truncation detection |
| `Makefile` | Builds and runs the test suites |

Planned modules (not present yet): `lz77`, `container`, `cli`.

## What is implemented

### Stage 1 - bit I/O

`BitWriter` / `BitReader` pack bits **least-significant-bit first within each byte**:
the first bit written goes to bit 0 of byte 0, the ninth to bit 0 of byte 1, and so
on. One convention for the whole format, so there is a single place a bit-order bug
can hide; Huffman codes are emitted MSB-first by bit-reversing each code at the
write site.

Both classes buffer whole bytes (no per-bit `push_back`/indexing). Reads are
bounds-checked: an over-read returns `false` and leaves the output untouched.
`flush()` zero-pads the final partial byte, and `remainingBitsAreZero()` lets the
decoder reject trailing garbage.

Tested: exact bit layout, non-byte-aligned writes, final partial byte, 64-bit
values, masking, zero-width writes, over-read rejection, zero padding, and ~2000
randomized round-trips.

### Stage 2 - canonical Huffman (no LZ77)

- Frequencies feed a `std::priority_queue` Huffman tree.
- **Bounded code length (<= 15).** If the natural tree is deeper than 15, an
  optimal length-limited code is computed with **package-merge** (an earlier
  zlib-style repair was found to produce an over-subscribed table on Fibonacci
  frequencies, so it was replaced).
- Degenerate cases: 0 used symbols -> all lengths 0; exactly 1 used symbol ->
  length 1 (never a 0-length code).
- **Canonical assignment** by `(length, symbol)`; the decoder reconstructs codes
  from the lengths alone.
- The 256 code lengths are run-length-encoded (DEFLATE-style symbols `0..15`,
  `16` = repeat previous 3-6, `17` = zeros 3-10, `18` = zeros 11-138) and that
  RLE stream is itself Huffman-coded over the 19-symbol RLE alphabet, whose 19
  lengths are stored raw as 4-bit values.
- Decode rejects truncated input, over-subscribed tables, and trailing garbage.

Tested: empty input, a single repeated byte, 2 symbols, all 256 values present,
~1 MB skewed input, a Fibonacci-skew file that forces length limiting (plus Kraft
`isPrefixCode` / `isCompleteCode` checks), direct RLE round-trip, and every
truncation length being reported as an error.

## Provisional wire format (Stages 1-2)

This framing is a **Stage-2 stepping stone** so Huffman can be exercised before
LZ77/container exist; Stage 4 replaces it. Bit order is the bitio convention above.

```
origLength                      : 64 bits
rleAlphabetLengths[0..18]       : 19 x 4 bits   (code lengths for symbols 0..18)
rleLengthTable                  : Huffman-coded RLE tokens, each followed by its
                                  extra bits (2/3/7), producing 256 code lengths
data                            : origLength symbols, each Huffman-coded
padding                         : zero bits to the next byte boundary
```

## Roadmap

3. LZ77 match finder - 32 KB window, 3-byte hash table, greedy then lazy matching
   (toggleable); standalone token verifier.
4. Combine tokens + Huffman - literals/lengths and distances trees, base+extra-bit
   bucketing, documented container (`FORMAT.md`).
5. Decompressor - full inverse, tested as rigorously as the encoder.
6. Fuzzing - libFuzzer + ASan/UBSan; `decompress(compress(x)) == x`; minimized
   repros kept as permanent tests.
7. Benchmark - 50+ files vs gzip/zlib, honest ratio/time writeup.

## Known limitations

- Whole-file in memory (memory scales with input size).
- No checksum yet: truncation is detected, but an arbitrary bit flip inside a
  valid-looking stream may decode to wrong bytes silently. A CRC in the Stage-4
  container will address this.