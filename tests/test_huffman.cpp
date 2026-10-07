#include "huffman.hpp"

#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

using namespace lzh;

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond) \
    do { \
        ++g_checks; \
        if (!(cond)) { \
            ++g_failures; \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

static void roundTrip(const std::vector<uint8_t>& in, const char* label) {
    const std::vector<uint8_t> comp = huffmanOnlyCompress(in.data(), in.size());
    std::vector<uint8_t> back;
    std::string err;
    const bool ok = huffmanOnlyDecompress(comp.data(), comp.size(), back, err);
    CHECK(ok);
    if (!ok) {
        std::printf("  [%s] decompress failed: %s\n", label, err.c_str());
        return;
    }
    CHECK(back.size() == in.size());
    CHECK(back == in);
}

static void test_empty() {
    roundTrip({}, "empty");
}

static void test_single_repeated_byte() {
    std::vector<uint8_t> in(100000, 0x41);
    std::vector<uint64_t> freq(256, 0);
    freq[0x41] = in.size();
    std::vector<uint8_t> lengths;
    std::vector<HuffCode> codes;
    buildHuffman(freq, kMaxCodeLen, lengths, codes);
    int used = 0;
    for (uint8_t l : lengths) {
        if (l) ++used;
    }
    CHECK(used == 1);
    CHECK(lengths[0x41] == 1);   // never a 0-length code for a used symbol
    CHECK(codes[0x41].length == 1);
    CHECK(isPrefixCode(lengths, kMaxCodeLen));
    roundTrip(in, "single-repeated");
}

static void test_two_symbols() {
    std::vector<uint8_t> in;
    in.reserve(10000);
    for (int i = 0; i < 10000; ++i) in.push_back((i & 1) ? 0xFF : 0x00);
    std::vector<uint64_t> freq(256, 0);
    freq[0x00] = 5000;
    freq[0xFF] = 5000;
    std::vector<uint8_t> lengths;
    std::vector<HuffCode> codes;
    buildHuffman(freq, kMaxCodeLen, lengths, codes);
    CHECK(lengths[0x00] == 1 && lengths[0xFF] == 1);
    CHECK(isCompleteCode(lengths, kMaxCodeLen));
    roundTrip(in, "two-symbols");
}

static void test_all_256_values() {
    std::vector<uint8_t> in;
    in.reserve(256 * 1000);
    for (int r = 0; r < 1000; ++r) {
        for (int b = 0; b < 256; ++b) in.push_back(static_cast<uint8_t>(b));
    }
    std::vector<uint64_t> freq(256, 0);
    for (uint8_t b : in) ++freq[b];
    std::vector<uint8_t> lengths;
    std::vector<HuffCode> codes;
    buildHuffman(freq, kMaxCodeLen, lengths, codes);
    CHECK(isCompleteCode(lengths, kMaxCodeLen));
    for (uint8_t l : lengths) CHECK(l == 8);  // 256 equal-weight symbols
    roundTrip(in, "all-256");
}

static void test_large_skewed() {
    std::mt19937_64 rng(1234);
    std::vector<uint8_t> in;
    in.reserve(1u << 20);
    for (int i = 0; i < (1 << 20); ++i) {
        const uint64_t r = rng() % 100;
        if (r < 90) in.push_back('a');
        else if (r < 95) in.push_back('b');
        else if (r < 99) in.push_back('c');
        else in.push_back(static_cast<uint8_t>(rng() & 0xFF));
    }
    roundTrip(in, "large-skewed");
}
 static std::vector<uint64_t> fibonacciFreq(int count) {
    std::vector<uint64_t> f(count, 0);
    uint64_t a = 1, b = 1;
    for (int i = 0; i < count; ++i) {
        f[i] = a;
        const uint64_t next = a + b;
        a = b;
        b = next;
    }
    return f;
}

// Fibonacci frequencies make an unbounded Huffman tree as deep as ~count, so
// this exercises the length-limit repair.
static void test_length_limit() {
    const int count = 30;
    const std::vector<uint64_t> freq = fibonacciFreq(count);
    std::vector<uint8_t> lengths;
    std::vector<HuffCode> codes;
    buildHuffman(freq, kMaxCodeLen, lengths, codes);

    int maxLen = 0;
    for (uint8_t l : lengths) {
        if (l > maxLen) maxLen = l;
    }
    CHECK(maxLen <= kMaxCodeLen);
    CHECK(isPrefixCode(lengths, kMaxCodeLen));
    CHECK(isCompleteCode(lengths, kMaxCodeLen));
    for (int s = 0; s < count; ++s) {
        CHECK(lengths[s] >= 1 && lengths[s] <= kMaxCodeLen);
        CHECK(codes[s].length == lengths[s]);
        CHECK(codes[s].bits < (1u << lengths[s]));
    }

    std::vector<uint8_t> in;
    for (int s = 0; s < count; ++s) {
        for (uint64_t k = 0; k < freq[s]; ++k) {
            in.push_back(static_cast<uint8_t>(s));
        }
    }
    roundTrip(in, "fibonacci-skew");
}

static void test_prefix_checks() {
    // Two rare + one common symbol -> lengths {1,2,2}.
    std::vector<uint64_t> freq(3, 0);
    freq[0] = 100;
    freq[1] = 1;
    freq[2] = 1;
    std::vector<uint8_t> lengths;
    std::vector<HuffCode> codes;
    buildHuffman(freq, kMaxCodeLen, lengths, codes);
    CHECK(lengths[0] == 1 && lengths[1] == 2 && lengths[2] == 2);
    CHECK(isCompleteCode(lengths, kMaxCodeLen));

    // Over-subscribed tables must be rejected.
    const std::vector<uint8_t> bad{1, 1, 1};
    HuffmanDecoder decBad;
    CHECK(decBad.build(bad) == false);

    // Valid but incomplete prefix code still builds (single-symbol case).
    const std::vector<uint8_t> single{0, 0, 1};
    HuffmanDecoder decSingle;
    CHECK(decSingle.build(single) == true);
}

static void test_rle_direct() {
    std::vector<std::vector<uint8_t>> cases;
    cases.push_back(std::vector<uint8_t>(256, 0));
    {
        std::vector<uint8_t> v(256, 0);
        v[42] = 1;
        cases.push_back(v);
    }
    cases.push_back(std::vector<uint8_t>(256, 8));
    {
        std::vector<uint8_t> v(256, 0);
        for (int i = 0; i < 256; ++i) v[i] = static_cast<uint8_t>(1 + (i % 15));
        cases.push_back(v);
    }
    {
        std::mt19937_64 rng(99);
        std::vector<uint8_t> v(256, 0);
        for (auto& x : v) x = static_cast<uint8_t>(rng() % 10);
        cases.push_back(v);
    }

    for (auto& lengths : cases) {
        const std::vector<RleToken> toks = rleEncodeLengths(lengths);
        std::vector<uint64_t> rfreq(kLenAlphabetSize, 0);
        for (const RleToken& t : toks) ++rfreq[t.symbol];
        std::vector<uint8_t> rlen;
        std::vector<HuffCode> rcode;
        buildHuffman(rfreq, kMaxCodeLen, rlen, rcode);

        std::vector<uint8_t> buf;
        BitWriter w(buf);
        for (const RleToken& t : toks) {
            encodeSymbol(w, rcode, t.symbol);
            if (t.extraBits) w.writeBits(t.extra, t.extraBits);
        }
        w.flush();

        BitReader r(buf.data(), buf.size());
        HuffmanDecoder rdec;
        CHECK(rdec.build(rlen));
        std::vector<uint8_t> back;
        std::string err;
        CHECK(rleDecodeLengths(rdec, r, 256, back, err));
        CHECK(back == lengths);
    }
}

static void test_truncation_is_detected() {
    std::vector<uint8_t> in;
    for (int i = 0; i < 5000; ++i) in.push_back(static_cast<uint8_t>((i * 7) % 13));
    const std::vector<uint8_t> comp = huffmanOnlyCompress(in.data(), in.size());
    CHECK(comp.size() > 8);
    for (size_t cut = 0; cut < comp.size(); ++cut) {
        std::vector<uint8_t> out;
        std::string err;
        const bool ok = huffmanOnlyDecompress(comp.data(), cut, out, err);
        CHECK(!ok);
        CHECK(!err.empty());
    }
}

int main() {
    test_empty();
    test_single_repeated_byte();
    test_two_symbols();
    test_all_256_values();
    test_large_skewed();
    test_length_limit();
    test_prefix_checks();
    test_rle_direct();
    test_truncation_is_detected();

    if (g_failures == 0) {
        std::printf("ok: %d checks passed\n", g_checks);
        return 0;
    }
    std::printf("FAILED: %d/%d checks failed\n", g_failures, g_checks);
    return 1;
}