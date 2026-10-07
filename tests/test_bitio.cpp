#include "bitio.hpp"

#include <cstdint>
#include <cstdio>
#include <random>
#include <utility>
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

static uint64_t maskOf(unsigned n) {
    return n >= 64 ? ~0ull : ((1ull << n) - 1);
}

// Exact bit order: 1,0,1,1 then four zeros -> 0b00001101 == 0x0D.
static void test_known_layout() {
    std::vector<uint8_t> buf;
    BitWriter w(buf);
    const unsigned bits[8] = {1, 0, 1, 1, 0, 0, 0, 0};
    for (unsigned b : bits) w.writeBits(b, 1);
    w.flush();
    CHECK(buf.size() == 1);
    CHECK(buf[0] == 0x0D);

    BitReader r(buf.data(), buf.size());
    for (unsigned i = 0; i < 8; ++i) {
        uint64_t v = 9;
        CHECK(r.readBits(v, 1));
        CHECK(v == bits[i]);
    }
    CHECK(r.bitsRemaining() == 0);
}

static void test_simple_non_aligned() {
    std::vector<uint8_t> buf;
    BitWriter w(buf);
    w.writeBits(0b101, 3);
    CHECK(w.bitCount() == 3);
    CHECK(!w.byteAligned());
    w.flush();
    CHECK(buf.size() == 1);
    CHECK(buf[0] == 0x05);
    CHECK(w.byteAligned());

    BitReader r(buf.data(), buf.size());
    uint64_t v = 12345;
    CHECK(r.readBits(v, 3));
    CHECK(v == 0b101);
    CHECK(r.bitsRemaining() == 5);
    CHECK(r.remainingBitsAreZero());
}

static void test_alignment_across_bytes() {
    std::vector<uint8_t> buf;
    BitWriter w(buf);
    w.writeByte(0xAB);
    CHECK(w.byteAligned());
    w.writeBits(0x5, 4);  // low nibble first
    CHECK(!w.byteAligned());
    w.writeBits(0xF, 4);  // -> byte 0xF5
    CHECK(w.byteAligned());
    w.flush();
    CHECK(buf.size() == 2);
    CHECK(buf[0] == 0xAB);
    CHECK(buf[1] == 0xF5);
}

static void test_partial_final_byte() {
    // 13 bits of 0x1ABC -> low 8 = 0xBC, remaining 5 = 0x1A.
    std::vector<uint8_t> buf;
    BitWriter w(buf);
    w.writeBits(0x1ABC, 13);
    CHECK(w.bitCount() == 13);
    w.flush();
    CHECK(buf.size() == 2);
    CHECK(buf[0] == 0xBC);
    CHECK(buf[1] == 0x1A);

    BitReader r(buf.data(), buf.size());
    uint64_t v = 0;
    CHECK(r.readBits(v, 13));
    CHECK(v == 0x1ABC);
    CHECK(r.bitsRemaining() == 3);    // padding present
    CHECK(r.remainingBitsAreZero());  // ...and zero
}
static void test_write_64_bits() {
    std::vector<uint8_t> buf;
    BitWriter w(buf);
    w.writeBits(0x0123456789ABCDEFull, 64);
    w.flush();
    CHECK(buf.size() == 8);
    BitReader r(buf.data(), buf.size());
    uint64_t v = 0;
    CHECK(r.readBits(v, 64));
    CHECK(v == 0x0123456789ABCDEFull);
}

static void test_read_past_end() {
    std::vector<uint8_t> buf;
    BitWriter w(buf);
    w.writeBits(0x3, 2);
    w.flush();  // one full physical byte = 8 readable bits (6 are zero padding)
    CHECK(buf.size() == 1);
    BitReader r(buf.data(), buf.size());
    uint64_t v = 777;
    CHECK(r.readBits(v, 9) == false);  // only 8 physical bits exist
    CHECK(v == 777);                   // out untouched on failure
    CHECK(r.readBits(v, 8) == true);
    CHECK(v == 0x03);                  // 2 written bits + 6 zero padding
    CHECK(r.bitsRemaining() == 0);
}

static void test_masking_and_zero_width() {
    std::vector<uint8_t> buf;
    BitWriter w(buf);
    w.writeBits(0xFFFFFFFFull, 0);  // writes nothing
    w.writeBits(0xFFFFull, 4);      // only low 4 bits = 0xF
    w.flush();
    CHECK(buf.size() == 1);
    CHECK(buf[0] == 0x0F);
}

static void test_random_roundtrip() {
    std::mt19937_64 rng(0xC0FFEE);
    for (int iter = 0; iter < 2000; ++iter) {
        const unsigned count = static_cast<unsigned>(rng() % 40) + 1;
        std::vector<std::pair<uint64_t, unsigned>> items;
        items.reserve(count);

        std::vector<uint8_t> buf;
        BitWriter w(buf);
        uint64_t totalBits = 0;
        for (unsigned i = 0; i < count; ++i) {
            const unsigned n = static_cast<unsigned>(rng() % 57) + 1;  // 1..57
            const uint64_t raw = rng();  // high garbage must be masked off
            w.writeBits(raw, n);
            items.emplace_back(raw & maskOf(n), n);
            totalBits += n;
        }
        CHECK(w.bitCount() == totalBits);
        w.flush();

        BitReader r(buf.data(), buf.size());
        for (auto& [expect, n] : items) {
            uint64_t got = ~0ull;
            CHECK(r.readBits(got, n));
            CHECK(got == expect);
        }
        CHECK(r.remainingBitsAreZero());
    }
}

static void test_flush_idempotent() {
    std::vector<uint8_t> buf;
    BitWriter w(buf);
    w.writeBits(0x7, 3);
    w.flush();
    const size_t n = buf.size();
    w.flush();
    CHECK(buf.size() == n);
}

int main() {
    test_known_layout();
    test_simple_non_aligned();
    test_alignment_across_bytes();
    test_partial_final_byte();
    test_write_64_bits();
    test_read_past_end();
    test_masking_and_zero_width();
    test_random_roundtrip();
    test_flush_idempotent();

    if (g_failures == 0) {
        std::printf("ok: %d checks passed\n", g_checks);
        return 0;
    }
    std::printf("FAILED: %d/%d checks failed\n", g_failures, g_checks);
    return 1;
}