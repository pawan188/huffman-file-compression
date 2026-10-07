#pragma once
//
// huffman.hpp - canonical Huffman coding with a bounded code length, plus a
// DEFLATE-style run-length encoding of the code-length table.
//
// Wire convention: bitio packs bits LSB-first, but Huffman codes are naturally
// MSB-first, so encodeSymbol() bit-reverses each code. The decoder therefore
// accumulates bits MSB-first. One reversal point, one decode rule.
//
// Stage 2 also ships a self-contained whole-buffer codec (huffmanOnlyCompress /
// huffmanOnlyDecompress) purely to exercise the coder before LZ77 exists. The
// Stage 4 container replaces that framing; the primitives here are reused.

#include "bitio.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace lzh {

// A single canonical code: `bits` holds the code MSB-first in its low `length`
// bits. length == 0 means the symbol is unused.
struct HuffCode {
    uint32_t bits = 0;
    uint8_t length = 0;
};

// DEFLATE-style code-length alphabet: symbols 0..15 are literal lengths, 16
// repeats the previous length 3..6 times, 17/18 repeat zero 3..10 / 11..138.
constexpr int kLenAlphabetSize = 19;
constexpr int kMaxCodeLen = 15;

// Fills `lengths` (indexed by symbol) from `freq` so that no used symbol gets
// length 0 and no length exceeds maxLen. 0 used symbols -> all zeros; exactly
// 1 used symbol -> that symbol gets length 1.
void buildCodeLengths(const std::vector<uint64_t>& freq, int maxLen,
                      std::vector<uint8_t>& lengths);

// Canonical assignment: sort by (length, symbol), then hand out increasing
// codes per length (MSB-first). Requires lengths to form a prefix code.
void buildCanonicalCodes(const std::vector<uint8_t>& lengths,
                         std::vector<HuffCode>& codes);

// buildCodeLengths + buildCanonicalCodes.
void buildHuffman(const std::vector<uint64_t>& freq, int maxLen,
                  std::vector<uint8_t>& lengths, std::vector<HuffCode>& codes);

// Kraft checks (fixed-point with 2^maxLen). isPrefixCode: every length <= maxLen
// and Kraft sum <= 1 (so the code is prefix-free / decodable). isCompleteCode:
// additionally Kraft sum == 1 (a full code, expected whenever >= 2 symbols are
// used). Both return false for the 0- and 1-symbol degenerate cases.
bool isPrefixCode(const std::vector<uint8_t>& lengths, int maxLen);
bool isCompleteCode(const std::vector<uint8_t>& lengths, int maxLen);

// Emits one symbol using a prebuilt code table.
void encodeSymbol(BitWriter& w, const std::vector<HuffCode>& codes, int symbol);

// Incremental canonical decoder. Built from lengths alone, exactly what the
// decoder side of the format has.
class HuffmanDecoder {
public:
    // Returns false if the table is over-subscribed (Kraft sum > 1).
    bool build(const std::vector<uint8_t>& lengths);
    // Returns the symbol, or -1 on an invalid code / end of input.
    int decode(BitReader& r) const;
    int maxLength() const { return maxLen_; }

private:
    int maxLen_ = 0;
    std::vector<int> count_;       // index 1..maxLen_: codes of each length
    std::vector<uint32_t> first_;  // index 1..maxLen_: first code of that length
    std::vector<int> start_;       // index 1..maxLen_: block start in sorted_
    std::vector<int> sorted_;      // symbols ordered by (length, symbol)
};

// One RLE token for the code-length table.
struct RleToken {
    uint8_t symbol = 0;     // 0..18
    uint16_t extra = 0;     // value carried by the extra bits
    uint8_t extraBits = 0;  // 0, 2, 3, or 7
};

// RLE-encodes a length table into tokens (greedy, DEFLATE-compatible ranges).
std::vector<RleToken> rleEncodeLengths(const std::vector<uint8_t>& lengths);

// Decodes exactly `symbolCount` lengths using `rleDecoder` to read RLE symbols.
// Returns false and sets `err` on invalid/truncated input.
bool rleDecodeLengths(HuffmanDecoder& rleDecoder, BitReader& r,
                      int symbolCount, std::vector<uint8_t>& lengths,
                      std::string& err);

// ---- Stage 2 stepping-stone whole-buffer codec -----------------------------
// Stream (bits, per bitio order): origLength (64), 19x4-bit RLE-alphabet code
// lengths, RLE length table for 256 symbols, Huffman-coded data, zero padding.
std::vector<uint8_t> huffmanOnlyCompress(const uint8_t* data, size_t n,
                                         int maxLen = kMaxCodeLen);
bool huffmanOnlyDecompress(const uint8_t* data, size_t n,
                           std::vector<uint8_t>& out, std::string& err);

}  // namespace lzh