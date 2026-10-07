#pragma once
//
// bitio.hpp - LSB-first bit stream reader/writer.
//
// Convention (shared by the whole format, see FORMAT.md):
//   The output bit stream is filled least-significant-bit first *within* each
//   byte. The first bit handed to writeBits lands in bit 0 of byte 0; the ninth
//   bit lands in bit 0 of byte 1; and so on.
//   writeBits(value, n) emits the low n bits of `value`, from bit 0 up to bit
//   n-1. Any set bits above n-1 are ignored.
//
// Why a single convention? Huffman codes are naturally MSB-first, so Stage 2
// bit-reverses each code before writing. Keeping exactly one bit order here
// means there is only one place a bit-order bug can hide.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace lzh {

class BitWriter {
public:
    explicit BitWriter(std::vector<uint8_t>& out) : out_(out) {}

    // Appends the low `nbits` (0..64) of `value` to the stream.
    void writeBits(uint64_t value, unsigned nbits);
    void writeByte(uint8_t b) { writeBits(b, 8); }

    // Emits any pending partial byte, zero-padded in the high bits. Idempotent.
    void flush();

    uint64_t bitCount() const { return totalBits_; }
    bool byteAligned() const { return pendingBits_ == 0; }

private:
    std::vector<uint8_t>& out_;
    uint8_t pending_ = 0;      // bits buffered for the current byte
    uint8_t pendingBits_ = 0;  // number of live bits in pending_ (0..7)
    uint64_t totalBits_ = 0;   // logical bits written (excludes flush padding)
};

class BitReader {
public:
    BitReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    // Reads `nbits` (0..64) LSB-first. Returns false, without consuming input
    // or modifying `out`, if fewer than `nbits` remain.
    bool readBits(uint64_t& out, unsigned nbits);
    bool readByte(uint8_t& b) {
        uint64_t v = 0;
        if (!readBits(v, 8)) return false;
        b = static_cast<uint8_t>(v);
        return true;
    }

    uint64_t bitsRemaining() const {
        return static_cast<uint64_t>(size_ - pos_) * 8u + bufBits_;
    }
    uint64_t bitPos() const { return totalBitsRead_; }
    bool atEnd() const { return bitsRemaining() == 0; }

    // True iff every bit not yet consumed is zero. Used to validate trailing
    // padding so corrupt/truncated streams are rejected instead of ignored.
    bool remainingBitsAreZero() const;

private:
    const uint8_t* data_;
    size_t size_;
    size_t pos_ = 0;
    uint64_t buf_ = 0;      // refill register
    unsigned bufBits_ = 0;  // live bits in buf_ (0..8)
    uint64_t totalBitsRead_ = 0;
};

}  // namespace lzh
