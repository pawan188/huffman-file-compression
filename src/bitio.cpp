#include "bitio.hpp"

#include <cassert>

namespace lzh {

void BitWriter::writeBits(uint64_t value, unsigned nbits) {
    assert(nbits <= 64);
    unsigned remaining = nbits;
    while (remaining > 0) {
        const unsigned space = 8u - pendingBits_;
        const unsigned take = remaining < space ? remaining : space;
        const uint64_t mask = (1ull << take) - 1;
        // take <= space, so the shifted bits never spill past this byte.
        pending_ = static_cast<uint8_t>(pending_ | ((value & mask) << pendingBits_));
        pendingBits_ = static_cast<uint8_t>(pendingBits_ + take);
        value >>= take;
        remaining -= take;
        totalBits_ += take;
        if (pendingBits_ == 8) {
            out_.push_back(pending_);
            pending_ = 0;
            pendingBits_ = 0;
        }
    }
}

void BitWriter::flush() {
    if (pendingBits_ > 0) {
        out_.push_back(pending_);
        pending_ = 0;
        pendingBits_ = 0;
    }
}

bool BitReader::readBits(uint64_t& out, unsigned nbits) {
    assert(nbits <= 64);
    if (bitsRemaining() < nbits) return false;

    uint64_t result = 0;
    unsigned got = 0;
    while (got < nbits) {
        if (bufBits_ == 0) {
            buf_ = data_[pos_++];
            bufBits_ = 8;
        }
        const unsigned want = nbits - got;
        const unsigned take = want < bufBits_ ? want : bufBits_;
        const uint64_t mask = (1ull << take) - 1;
        result |= (buf_ & mask) << got;
        buf_ >>= take;
        bufBits_ -= take;
        got += take;
    }
    totalBitsRead_ += nbits;
    out = result;
    return true;
}

bool BitReader::remainingBitsAreZero() const {
    if (bufBits_ != 0 && (buf_ & ((1ull << bufBits_) - 1)) != 0) return false;
    for (size_t i = pos_; i < size_; ++i) {
        if (data_[i] != 0) return false;
    }
    return true;
}

}  // namespace lzh
