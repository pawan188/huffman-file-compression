#include "huffman.hpp"

#include <algorithm>
#include <queue>
#include <utility>

namespace lzh {
namespace {

// Emits an n-bit value MSB-first through the LSB-first BitWriter.
uint32_t reverseBits(uint32_t v, unsigned n) {
    uint32_t r = 0;
    for (unsigned i = 0; i < n; ++i) {
        r = (r << 1) | (v & 1u);
        v >>= 1;
    }
    return r;
}

struct Node {
    uint64_t freq;
    int left;    // -1 for leaves
    int right;
    int symbol;  // -1 for internal nodes
};

// Package-merge: optimal length-limited prefix codes.
//
// Intuition: think of each level's "coin" value as 2^-level. We repeatedly
// pair up the two cheapest candidates and merge the resulting packages back
// with the original symbols. Taking the 2n-2 cheapest items from the final
// list and counting, per symbol, how many chosen items contain it, yields that
// symbol's code length. The result is a complete code (Kraft sum == 1) with
// every length <= maxLen, which is exactly what the canonical coder needs.
struct PmItem {
    uint64_t weight;
    int sym;  // >= 0 for an original symbol leaf
    int left;
    int right;
};

void packageMergeLengths(const std::vector<uint64_t>& freq,
                         const std::vector<int>& used, int maxLen,
                         std::vector<uint8_t>& lengths) {
    const int n = static_cast<int>(used.size());
    std::vector<PmItem> items;
    items.reserve(static_cast<size_t>(n) * (maxLen + 2));

    std::vector<int> orig;
    orig.reserve(n);
    for (int s : used) {
        items.push_back(PmItem{freq[s], s, -1, -1});
        orig.push_back(static_cast<int>(items.size()) - 1);
    }
    std::sort(orig.begin(), orig.end(), [&](int a, int b) {
        if (items[a].weight != items[b].weight) return items[a].weight < items[b].weight;
        return items[a].sym < items[b].sym;
    });

    std::vector<int> list = orig;
    std::vector<int> pkgs;
    std::vector<int> merged;
    for (int level = 1; level < maxLen; ++level) {
        pkgs.clear();
        for (size_t i = 0; i + 1 < list.size(); i += 2) {
            const int a = list[i];
            const int b = list[i + 1];
            items.push_back(PmItem{items[a].weight + items[b].weight, -1, a, b});
            pkgs.push_back(static_cast<int>(items.size()) - 1);
        }
        // `list` is sorted, so consecutive pair sums are sorted too; merge the
        // packages with the originals to form the next level's candidate list.
        merged.clear();
        merged.reserve(orig.size() + pkgs.size());
        size_t oi = 0, pi = 0;
        auto less = [&](int a, int b) {
            if (items[a].weight != items[b].weight) return items[a].weight < items[b].weight;
            return a < b;
        };
        while (oi < orig.size() && pi < pkgs.size()) {
            if (less(pkgs[pi], orig[oi])) {
                merged.push_back(pkgs[pi++]);
            } else {
                merged.push_back(orig[oi++]);
            }
        }
        while (oi < orig.size()) merged.push_back(orig[oi++]);
        while (pi < pkgs.size()) merged.push_back(pkgs[pi++]);
        list.swap(merged);
    }

    const int need = 2 * n - 2;
    const int take = static_cast<int>(list.size()) < need ? static_cast<int>(list.size()) : need;
    for (int i = 0; i < take; ++i) {
        std::vector<int> stack;
        stack.push_back(list[i]);
        while (!stack.empty()) {
            const int node = stack.back();
            stack.pop_back();
            if (items[node].sym >= 0) {
                ++lengths[items[node].sym];
            } else {
                stack.push_back(items[node].left);
                stack.push_back(items[node].right);
            }
        }
    }
}

}  // namespace

void buildCodeLengths(const std::vector<uint64_t>& freq, int maxLen,
                      std::vector<uint8_t>& lengths) {
    const int n = static_cast<int>(freq.size());
    lengths.assign(n, 0);
    if (maxLen <= 0) maxLen = kMaxCodeLen;

    std::vector<int> used;
    used.reserve(n);
    for (int s = 0; s < n; ++s) {
        if (freq[s] > 0) used.push_back(s);
    }
    if (used.empty()) return;               // degenerate: no symbols
    if (used.size() == 1) {                 // degenerate: one symbol -> length 1
        lengths[used[0]] = 1;
        return;
    }

    // Classic Huffman first: cheap, optimal, and usually already in range.
    std::vector<Node> nodes;
    nodes.reserve(2 * used.size());
    auto cmp = [&nodes](int a, int b) {
        if (nodes[a].freq != nodes[b].freq) return nodes[a].freq > nodes[b].freq;
        return a > b;
    };
    std::priority_queue<int, std::vector<int>, decltype(cmp)> pq(cmp);
    for (int s : used) {
        nodes.push_back(Node{freq[s], -1, -1, s});
        pq.push(static_cast<int>(nodes.size()) - 1);
    }
    while (pq.size() > 1) {
        const int a = pq.top();
        pq.pop();
        const int b = pq.top();
        pq.pop();
        nodes.push_back(Node{nodes[a].freq + nodes[b].freq, a, b, -1});
        pq.push(static_cast<int>(nodes.size()) - 1);
    }
    int naturalMax = 0;
    std::vector<std::pair<int, int>> stack;
    stack.emplace_back(pq.top(), 0);
    while (!stack.empty()) {
        const auto [node, depth] = stack.back();
        stack.pop_back();
        if (nodes[node].symbol >= 0) {
            lengths[nodes[node].symbol] = static_cast<uint8_t>(depth);
            naturalMax = std::max(naturalMax, depth);
        } else {
            stack.emplace_back(nodes[node].left, depth + 1);
            stack.emplace_back(nodes[node].right, depth + 1);
        }
    }
    if (naturalMax <= maxLen) return;

    // Too deep: recompute an optimal bounded code.
    std::fill(lengths.begin(), lengths.end(), static_cast<uint8_t>(0));
    packageMergeLengths(freq, used, maxLen, lengths);
}

void buildCanonicalCodes(const std::vector<uint8_t>& lengths,
                         std::vector<HuffCode>& codes) {
    const int n = static_cast<int>(lengths.size());
    codes.assign(n, HuffCode{});
    int maxLen = 0;
    for (uint8_t l : lengths) maxLen = std::max(maxLen, static_cast<int>(l));
    if (maxLen == 0) return;

    std::vector<int> count(maxLen + 1, 0);
    for (uint8_t l : lengths) {
        if (l > 0) ++count[l];
    }
    std::vector<uint32_t> next(maxLen + 2, 0);
    uint32_t code = 0;
    for (int bits = 1; bits <= maxLen; ++bits) {
        code = (code + static_cast<uint32_t>(count[bits - 1])) << 1;
        next[bits] = code;
    }
    for (int s = 0; s < n; ++s) {
        const int len = lengths[s];
        if (len == 0) continue;
        codes[s].bits = next[len]++;
        codes[s].length = static_cast<uint8_t>(len);
    }
}

void buildHuffman(const std::vector<uint64_t>& freq, int maxLen,
                  std::vector<uint8_t>& lengths, std::vector<HuffCode>& codes) {
    buildCodeLengths(freq, maxLen, lengths);
    buildCanonicalCodes(lengths, codes);
}

bool isPrefixCode(const std::vector<uint8_t>& lengths, int maxLen) {
    uint64_t kraft = 0;
    for (uint8_t l : lengths) {
        if (l == 0) continue;
        if (l > maxLen) return false;
        kraft += (1ull << (maxLen - l));
    }
    return kraft <= (1ull << maxLen);
}

bool isCompleteCode(const std::vector<uint8_t>& lengths, int maxLen) {
    uint64_t kraft = 0;
    bool any = false;
    for (uint8_t l : lengths) {
        if (l == 0) continue;
        if (l > maxLen) return false;
        kraft += (1ull << (maxLen - l));
        any = true;
    }
    return any && kraft == (1ull << maxLen);
}

void encodeSymbol(BitWriter& w, const std::vector<HuffCode>& codes, int symbol) {
    const HuffCode& c = codes[symbol];
    w.writeBits(reverseBits(c.bits, c.length), c.length);
}
bool HuffmanDecoder::build(const std::vector<uint8_t>& lengths) {
    maxLen_ = 0;
    for (uint8_t l : lengths) maxLen_ = std::max(maxLen_, static_cast<int>(l));
    count_.assign(maxLen_ + 1, 0);
    first_.assign(maxLen_ + 2, 0);
    start_.assign(maxLen_ + 2, 0);
    sorted_.clear();
    if (maxLen_ == 0) return true;

    for (uint8_t l : lengths) {
        if (l > 0) ++count_[l];
    }
    uint64_t kraft = 0;
    for (uint8_t l : lengths) {
        if (l > 0) kraft += (1ull << (maxLen_ - l));
    }
    if (kraft > (1ull << maxLen_)) return false;  // over-subscribed

    uint32_t code = 0;
    for (int bits = 1; bits <= maxLen_; ++bits) {
        code = (code + static_cast<uint32_t>(count_[bits - 1])) << 1;
        first_[bits] = code;
    }
    int acc = 0;
    for (int bits = 1; bits <= maxLen_; ++bits) {
        start_[bits] = acc;
        acc += count_[bits];
    }
    std::vector<std::pair<int, int>> tmp;
    tmp.reserve(acc);
    for (int s = 0; s < static_cast<int>(lengths.size()); ++s) {
        if (lengths[s] > 0) tmp.emplace_back(lengths[s], s);
    }
    std::sort(tmp.begin(), tmp.end());
    sorted_.reserve(acc);
    for (auto& p : tmp) sorted_.push_back(p.second);
    return true;
}

int HuffmanDecoder::decode(BitReader& r) const {
    if (maxLen_ == 0) return -1;
    uint32_t code = 0;
    for (int bits = 1; bits <= maxLen_; ++bits) {
        uint64_t b = 0;
        if (!r.readBits(b, 1)) return -1;
        code = (code << 1) | static_cast<uint32_t>(b);
        if (count_[bits] > 0) {
            const uint32_t first = first_[bits];
            if (code >= first &&
                code < first + static_cast<uint32_t>(count_[bits])) {
                return sorted_[start_[bits] + static_cast<int>(code - first)];
            }
        }
    }
    return -1;
}

std::vector<RleToken> rleEncodeLengths(const std::vector<uint8_t>& lengths) {
    std::vector<RleToken> out;
    const size_t n = lengths.size();
    size_t i = 0;
    while (i < n) {
        const uint8_t len = lengths[i];
        if (len == 0) {
            size_t j = i;
            while (j < n && lengths[j] == 0) ++j;
            size_t run = j - i;
            while (run >= 11) {
                const size_t r = run < 138 ? run : 138;
                out.push_back(RleToken{18, static_cast<uint16_t>(r - 11), 7});
                run -= r;
            }
            while (run >= 3) {
                const size_t r = run < 10 ? run : 10;
                out.push_back(RleToken{17, static_cast<uint16_t>(r - 3), 3});
                run -= r;
            }
            while (run > 0) {
                out.push_back(RleToken{0, 0, 0});
                --run;
            }
            i = j;
        } else {
            out.push_back(RleToken{len, 0, 0});
            ++i;
            size_t j = i;
            while (j < n && lengths[j] == len) ++j;
            size_t run = j - i;  // repeats beyond the literal just emitted
            while (run >= 3) {
                const size_t r = run < 6 ? run : 6;
                out.push_back(RleToken{16, static_cast<uint16_t>(r - 3), 2});
                run -= r;
            }
            while (run > 0) {
                out.push_back(RleToken{len, 0, 0});
                --run;
            }
            i = j;
        }
    }
    return out;
}

bool rleDecodeLengths(HuffmanDecoder& rleDecoder, BitReader& r, int symbolCount,
                      std::vector<uint8_t>& lengths, std::string& err) {
    lengths.clear();
    lengths.reserve(static_cast<size_t>(symbolCount));
    int prev = 0;
    while (static_cast<int>(lengths.size()) < symbolCount) {
        const int sym = rleDecoder.decode(r);
        if (sym < 0) {
            err = "invalid or truncated RLE symbol";
            return false;
        }
        if (sym < 16) {
            lengths.push_back(static_cast<uint8_t>(sym));
            prev = sym;
        } else if (sym == 16) {
            uint64_t extra = 0;
            if (!r.readBits(extra, 2)) {
                err = "truncated RLE repeat code";
                return false;
            }
            if (prev == 0) {
                err = "RLE repeat-previous with no previous nonzero length";
                return false;
            }
            const int reps = static_cast<int>(extra) + 3;
            for (int k = 0; k < reps; ++k) lengths.push_back(static_cast<uint8_t>(prev));
        } else if (sym == 17) {
            uint64_t extra = 0;
            if (!r.readBits(extra, 3)) {
                err = "truncated RLE zero run";
                return false;
            }
            const int reps = static_cast<int>(extra) + 3;
            for (int k = 0; k < reps; ++k) lengths.push_back(0);
            prev = 0;
        } else if (sym == 18) {
            uint64_t extra = 0;
            if (!r.readBits(extra, 7)) {
                err = "truncated RLE zero run";
                return false;
            }
            const int reps = static_cast<int>(extra) + 11;
            for (int k = 0; k < reps; ++k) lengths.push_back(0);
            prev = 0;
        } else {
            err = "bad RLE symbol";
            return false;
        }
        if (static_cast<int>(lengths.size()) > symbolCount) {
            err = "RLE length table overruns alphabet";
            return false;
        }
    }
    return true;
}

std::vector<uint8_t> huffmanOnlyCompress(const uint8_t* data, size_t n, int maxLen) {
    if (maxLen > kMaxCodeLen || maxLen <= 0) maxLen = kMaxCodeLen;

    std::vector<uint64_t> freq(256, 0);
    for (size_t i = 0; i < n; ++i) ++freq[data[i]];

    std::vector<uint8_t> lengths;
    std::vector<HuffCode> codes;
    buildHuffman(freq, maxLen, lengths, codes);

    const std::vector<RleToken> rle = rleEncodeLengths(lengths);

    std::vector<uint64_t> rfreq(kLenAlphabetSize, 0);
    for (const RleToken& t : rle) ++rfreq[t.symbol];
    std::vector<uint8_t> rlengths;
    std::vector<HuffCode> rcodes;
    buildHuffman(rfreq, maxLen, rlengths, rcodes);

    std::vector<uint8_t> out;
    BitWriter w(out);
    w.writeBits(static_cast<uint64_t>(n), 64);
    for (int s = 0; s < kLenAlphabetSize; ++s) w.writeBits(rlengths[s], 4);
    for (const RleToken& t : rle) {
        encodeSymbol(w, rcodes, t.symbol);
        if (t.extraBits) w.writeBits(t.extra, t.extraBits);
    }
    for (size_t i = 0; i < n; ++i) encodeSymbol(w, codes, data[i]);
    w.flush();
    return out;
}

bool huffmanOnlyDecompress(const uint8_t* data, size_t n, std::vector<uint8_t>& out,
                           std::string& err) {
    out.clear();
    BitReader r(data, n);

    uint64_t origLen = 0;
    if (!r.readBits(origLen, 64)) {
        err = "truncated header";
        return false;
    }
    if (origLen > (1ull << 40)) {
        err = "implausible original length";
        return false;
    }

    std::vector<uint8_t> rlengths(kLenAlphabetSize, 0);
    for (int s = 0; s < kLenAlphabetSize; ++s) {
        uint64_t v = 0;
        if (!r.readBits(v, 4)) {
            err = "truncated RLE-alphabet lengths";
            return false;
        }
        rlengths[s] = static_cast<uint8_t>(v);
    }
    HuffmanDecoder rdec;
    if (!rdec.build(rlengths)) {
        err = "invalid RLE-alphabet code";
        return false;
    }

    std::vector<uint8_t> lengths;
    if (!rleDecodeLengths(rdec, r, 256, lengths, err)) return false;

    HuffmanDecoder dec;
    if (!dec.build(lengths)) {
        err = "invalid literal/length code";
        return false;
    }

    out.reserve(static_cast<size_t>(origLen));
    for (uint64_t i = 0; i < origLen; ++i) {
        const int sym = dec.decode(r);
        if (sym < 0) {
            err = "truncated or invalid data symbol";
            out.clear();
            return false;
        }
        out.push_back(static_cast<uint8_t>(sym));
    }
    if (!r.remainingBitsAreZero()) {
        err = "trailing garbage after data";
        out.clear();
        return false;
    }
    return true;
}

}  // namespace lzh