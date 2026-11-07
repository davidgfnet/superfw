// apultra.cc - aPLib-derived compressor, single translation unit
//
// This is a similar bitstream format to aplib, but with minor changes.
// See farOffsetSize and shortOffsetEncodable to see how it's tuned for 16 bit data.
//
// A modern C++ reimplementation of the apultra compressor by Emmanuel Marty
// (https://github.com/emmanuel-marty/apultra). Mostly LLM generated.
//
// The original algorithm, byte stream format and parser heuristics are the work
// of Emmanuel Marty and spke. The lcp-interval matchfinder method derives from
// wimlib's lcpit_matchfinder (CC0). The suffix array is built with SA-IS here
// rather than libdivsufsort; the suffix array of a string is unique, so this
// changes nothing about the output.

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

// ============================================================ public interface

namespace apultra {

/// Upper bound on the compressed size for a given input size.
[[nodiscard]] constexpr std::size_t maxCompressedSize(std::size_t inputSize) noexcept {
    return ((inputSize * 9 + 1 + 2 + 8) + 7) >> 3;
}

/// Compress `input` and return the raw aPLib stream.
/// `maxWindowSize` of 0 selects the format maximum (2 MB - 1).
/// Throws std::runtime_error on failure (only possible on allocation or
/// internal invariant failure).
[[nodiscard]] std::vector<std::uint8_t> compress(std::span<const std::uint8_t> input,
                                                 std::size_t maxWindowSize = 0);

}  // namespace apultra

// ================================================== constants and helpers

namespace apultra {

using std::int32_t;
using std::uint16_t;
using std::uint32_t;
using std::uint64_t;
using std::uint8_t;

// ---------------------------------------------------------------- byte stream

inline constexpr int kMinOffset = 1;
inline constexpr int kMaxOffset = 0x1fffff;
inline constexpr int kMaxVarLen = 0x1fffff;
inline constexpr int kBlockSize = 0x100000;
inline constexpr int kMinMatch3Offset = 1280;
inline constexpr int kMinMatch4Offset = 32000;

inline constexpr int kCodeLargeMatch = 2;  // 10
inline constexpr int kSizeLargeMatch = 2;
inline constexpr int kCode7BitMatch = 6;  // 110
inline constexpr int kSize7BitMatch = 3;
inline constexpr int kFarRawBits = 2;      // Raw offset bits between gamma and LSB byte
inline constexpr int kCodeShortMatch = 7;  // 111
inline constexpr int kSizeShortMatch = 3;
inline constexpr int kShortOffsetBits = 3;  // Offset field, in halfwords

// ------------------------------------------------------ matchfinder / parser

inline constexpr int kLcpBits = 15;
inline constexpr int kTagBits = 4;
inline constexpr int kLcpMax = (1 << (kLcpBits - kTagBits)) - 1;  // 2047
inline constexpr int kLcpAndTagMax = (1 << kLcpBits) - 1;
inline constexpr int kLcpShift = 63 - kLcpBits;  // 48
inline constexpr uint64_t kLcpMask = ((1ull << kLcpBits) - 1) << kLcpShift;
inline constexpr uint64_t kPosMask = (1ull << kLcpShift) - 1;
inline constexpr uint64_t kVisitedFlag = 1ull << 63;
inline constexpr uint64_t kExclVisitedMask = ~kVisitedFlag;

inline constexpr int kArrivalsMax = 62;
inline constexpr int kArrivalsNormal = 46;
inline constexpr int kArrivalsSmall = 9;
inline constexpr int kMatchesPerIndex = 64;
inline constexpr int kMatchesPerIndexShift = 6;
inline constexpr int kLeaveAloneMatchSize = 120;

struct Match {
    uint32_t length : 11;
    uint32_t offset : 21;
};

struct FinalMatch {
    int length;
    int offset;
};

/// Forward arrival slot of the optimal parser.
struct Arrival {
    int cost;
    uint32_t from_pos : 21;
    int32_t from_slot : 7;
    uint32_t follows_literal : 1;
    uint32_t rep_offset : 21;
    uint32_t short_offset : 4;
    uint32_t rep_pos : 21;
    uint32_t match_len : 11;
    int score;
};

// ---------------------------------------------------------------- cost model

/// Bits needed for an interlaced-gamma2 coded value (0 for 0 and 1).
[[nodiscard]] constexpr int gamma2Size(int value) noexcept {
    return value < 2 ? 0 : 2 * (std::bit_width(static_cast<uint32_t>(value)) - 1);
}

/// Far match offsets are always even and are encoded as halfword distances:
///   gamma2((offset >> 11) + 2 + followsLiteral), bits [10:9], byte with bits [8:1]
[[nodiscard]] constexpr int farOffsetGamma(int matchOffset, int followsLiteral) noexcept {
    return (matchOffset >> (9 + kFarRawBits)) + 2 + followsLiteral;
}

/// Bits for the token plus offset of a far match.
[[nodiscard]] constexpr int farOffsetSize(int matchOffset, int followsLiteral) noexcept {
    return 8 + kFarRawBits + kSizeLargeMatch + gamma2Size(farOffsetGamma(matchOffset, followsLiteral));
}

/// Single byte matches use even offsets from 2 to 14 (0 encodes a zero byte).
[[nodiscard]] constexpr bool shortOffsetEncodable(int matchOffset) noexcept {
    return !(matchOffset & 1) && matchOffset > 0 && matchOffset < (2 << kShortOffsetBits);
}

/// Odd offsets can only be encoded by the 7 bit match (or a rep-match following it).
[[nodiscard]] constexpr bool offsetEncodable(int matchOffset) noexcept {
    return !(matchOffset & 1) || matchOffset < 128;
}

/// Prevents the optimizer from turning a match into an unencodable far match.
inline constexpr int kUnencodablePenalty = 1 << 24;

/// Bits for the token plus offset of a match.
[[nodiscard]] constexpr int offsetVarLenSize(int length, int matchOffset,
                                             int followsLiteral) noexcept {
    if (length <= 3 && matchOffset < 128) return 8 + kSize7BitMatch;
    return farOffsetSize(matchOffset, followsLiteral ? 1 : 0) +
           ((matchOffset & 1) ? kUnencodablePenalty : 0);
}

/// Bits for the length of a match.
[[nodiscard]] constexpr int matchVarLenSize(int length, int matchOffset) noexcept {
    if (length <= 3 && matchOffset < 128) return 0;
    if (matchOffset < 128 || matchOffset >= kMinMatch4Offset) return gamma2Size(length - 2);
    if (matchOffset < kMinMatch3Offset) return gamma2Size(length);
    return gamma2Size(length - 1);
}

// --------------------------------------------------------------- bit writer

/// Writes the aPLib bit/byte stream: bits are packed MSB-first into a byte that
/// is allocated in the stream at the point the first of its bits is needed.
class BitWriter {
public:
    BitWriter(uint8_t* out, int capacity) noexcept : out_(out), capacity_(capacity) {}

    void writeBits(int value, int bits) noexcept {
        for (int i = bits - 1; i >= 0; --i) {
            if (bitShift_ < 0) {
                if (pos_ >= capacity_) {
                    failed_ = true;
                    return;
                }
                bitsPos_ = pos_;
                bitShift_ = 7;
                out_[pos_++] = 0;
            }
            out_[bitsPos_] |= static_cast<uint8_t>(((value >> i) & 1) << bitShift_);
            --bitShift_;
        }
    }

    void writeGamma2(int value) noexcept {
        for (int bit = std::bit_width(static_cast<uint32_t>(value)) - 2; bit >= 1; --bit)
            writeBits((((value >> bit) & 1) << 1) | 1, 2);
        writeBits((value & 1) << 1, 2);
    }

    void writeByte(uint8_t byte) noexcept {
        if (failed_ || pos_ >= capacity_) {
            failed_ = true;
            return;
        }
        out_[pos_++] = byte;
    }

    [[nodiscard]] int pos() const noexcept { return pos_; }
    [[nodiscard]] bool failed() const noexcept { return failed_; }
    void fail() noexcept { failed_ = true; }

private:
    uint8_t* out_;
    int capacity_;
    int pos_ = 0;
    int bitsPos_ = 0;
    int bitShift_ = -1;
    bool failed_ = false;
};

// ---------------------------------------------------------------- compressor

class Compressor {
public:
    Compressor(int blockSize, int maxWindowSize, int maxArrivals, int maxOffset);

    /// Compress one block. Returns false on failure.
    bool shrinkBlock(std::span<const uint8_t> window, int prevBlockSize, int inDataSize,
                     BitWriter& writer, int& followsLiteral, int& repMatchOffset, int blockFlags);

private:
    // matchfinder
    void buildSuffixArray(std::span<const uint8_t> window);
    int findMatchesAt(int offset, Match* matches, uint16_t* matchDepth, uint8_t& match1,
                      int maxMatches, bool selfContained);
    void skipMatches(int startOffset, int endOffset);
    void findAllMatches(int startOffset, int endOffset, int blockFlags);
    void findShortMatches(std::span<const uint8_t> window, int startOffset, int endOffset);

    // optimal parser
    void insertForwardMatch(const uint8_t* window, int i, int matchOffset, int startOffset,
                            int endOffset, int depth);
    void optimizeForward(const uint8_t* window, int startOffset, int endOffset,
                         bool insertForwardReps, int curRepMatchOffset, int blockFlags);
    bool reduceCommands(const uint8_t* window, int startOffset, int endOffset,
                        int curRepMatchOffset, int blockFlags);
    bool writeBlock(const uint8_t* window, int startOffset, int endOffset, BitWriter& writer,
                    int& followsLiteral, int& repMatchOffset, int blockFlags);
    void supplementMatches(const uint8_t* window, int prevBlockSize, int inDataSize, int endOffset,
                           int blockFlags);
    void supplementMatchesFurther(const uint8_t* window, int prevBlockSize, int endOffset);

    Match* matchesAt(int index) noexcept {
        return match_.data() + (static_cast<std::size_t>(index) << kMatchesPerIndexShift);
    }
    uint16_t* depthsAt(int index) noexcept {
        return match_depth_.data() + (static_cast<std::size_t>(index) << kMatchesPerIndexShift);
    }
    Arrival* arrivalsAt(int index) noexcept {
        return arrival_.data() + static_cast<std::size_t>(index) * arrivals_;
    }
    /// best_match_ is indexed by absolute window position.
    FinalMatch& bestMatch(int pos) noexcept { return best_match_[pos - startOffset_]; }

    int block_size_;
    int arrivals_;
    int max_offset_;
    int startOffset_ = 0;

    std::vector<uint64_t> intervals_, pos_data_, open_intervals_;
    std::vector<int32_t> suffix_array_, plcp_, rle_len_, visited_;
    std::vector<int32_t> first_offset_for_byte_, next_offset_for_pos_, offset_cache_;
    std::vector<Match> match_;
    std::vector<uint16_t> match_depth_;
    std::vector<uint8_t> match1_;
    std::vector<FinalMatch> best_match_;
    std::vector<Arrival> arrival_;
};

}  // namespace apultra

// ============================================ suffix array (SA-IS)

namespace apultra {

namespace sais {

namespace detail {

/// Bucket boundaries: `heads` = first slot of each bucket, otherwise last+1.
inline void buckets(std::span<const std::int32_t> s, int alphabet, std::vector<std::int32_t>& b,
                    bool heads) {
    b.assign(alphabet, 0);
    for (std::int32_t c : s) ++b[c];
    std::int32_t sum = 0;
    for (int c = 0; c < alphabet; ++c) {
        sum += b[c];
        b[c] = heads ? sum - b[c] : sum;
    }
}

/// Induced sorting: given LMS suffixes placed at their bucket ends, derive the
/// full ordering of L-type then S-type suffixes.
inline void induce(std::span<const std::int32_t> s, std::span<std::int32_t> sa,
                   std::span<const std::uint8_t> isS, int alphabet, std::vector<std::int32_t>& b) {
    const auto n = static_cast<std::int32_t>(s.size());

    buckets(s, alphabet, b, /*heads=*/true);
    for (std::int32_t i = 0; i < n; ++i) {
        const std::int32_t j = sa[i] - 1;
        if (sa[i] > 0 && !isS[j]) sa[b[s[j]]++] = j;
    }

    buckets(s, alphabet, b, /*heads=*/false);
    for (std::int32_t i = n - 1; i >= 0; --i) {
        const std::int32_t j = sa[i] - 1;
        if (sa[i] > 0 && isS[j]) sa[--b[s[j]]] = j;
    }
}

/// `s` must end with a unique sentinel that is smaller than every other symbol.
inline void build(std::span<const std::int32_t> s, std::span<std::int32_t> sa, int alphabet) {
    const auto n = static_cast<std::int32_t>(s.size());
    if (n == 1) {
        sa[0] = 0;
        return;
    }

    std::vector<std::uint8_t> isS(n);
    isS[n - 1] = true;
    for (std::int32_t i = n - 2; i >= 0; --i)
        isS[i] = (s[i] < s[i + 1] || (s[i] == s[i + 1] && isS[i + 1])) ? 1 : 0;
    const auto isLms = [&](std::int32_t i) { return i > 0 && isS[i] && !isS[i - 1]; };

    std::vector<std::int32_t> b;

    // Pass 1: sort LMS substrings by induction from an arbitrary LMS order.
    std::fill(sa.begin(), sa.end(), -1);
    buckets(s, alphabet, b, /*heads=*/false);
    for (std::int32_t i = 1; i < n; ++i)
        if (isLms(i)) sa[--b[s[i]]] = i;
    induce(s, sa, isS, alphabet, b);

    // Gather LMS positions in sorted order and name their substrings.
    std::vector<std::int32_t> lms;
    lms.reserve(static_cast<std::size_t>(n) / 2 + 1);
    for (std::int32_t i = 0; i < n; ++i)
        if (isLms(sa[i])) lms.push_back(sa[i]);

    const auto nLms = static_cast<std::int32_t>(lms.size());
    std::vector<std::int32_t> name(static_cast<std::size_t>(n) / 2 + 1, 0);
    std::int32_t names = 0, prev = -1;
    for (std::int32_t k = 0; k < nLms; ++k) {
        const std::int32_t cur = lms[k];
        bool diff = prev < 0;
        for (std::int32_t d = 0; !diff; ++d) {
            if (s[cur + d] != s[prev + d] || isS[cur + d] != isS[prev + d]) {
                diff = true;
            } else if (d > 0 && (isLms(cur + d) || isLms(prev + d))) {
                break;  // identical substrings
            }
        }
        if (diff) ++names;
        name[static_cast<std::size_t>(cur) / 2] = names - 1;
        prev = cur;
    }

    // Pass 2: order the LMS suffixes, recursing only if names are not unique.
    std::vector<std::int32_t> lmsOrder(nLms);
    if (names < nLms) {
        std::vector<std::int32_t> reduced(nLms);
        std::int32_t w = 0;
        for (std::int32_t i = 1; i < n; ++i)
            if (isLms(i)) reduced[w++] = name[static_cast<std::size_t>(i) / 2];
        build(reduced, lmsOrder, names);
        std::vector<std::int32_t> lmsPos(nLms);
        w = 0;
        for (std::int32_t i = 1; i < n; ++i)
            if (isLms(i)) lmsPos[w++] = i;
        for (std::int32_t i = 0; i < nLms; ++i) lmsOrder[i] = lmsPos[lmsOrder[i]];
    } else {
        for (std::int32_t i = 1; i < n; ++i)
            if (isLms(i)) lmsOrder[name[static_cast<std::size_t>(i) / 2]] = i;
    }

    // Pass 3: final induction from the correctly ordered LMS suffixes.
    std::fill(sa.begin(), sa.end(), -1);
    buckets(s, alphabet, b, /*heads=*/false);
    for (std::int32_t i = nLms - 1; i >= 0; --i) sa[--b[s[lmsOrder[i]]]] = lmsOrder[i];
    induce(s, sa, isS, alphabet, b);
}

}  // namespace detail

/// Build the suffix array of `data` into `sa` (which must have data.size() entries).
inline void buildSuffixArray(std::span<const std::uint8_t> data, std::span<std::int32_t> sa) {
    const auto n = static_cast<std::int32_t>(data.size());
    if (n == 0) return;

    // Shift the alphabet up by one so that 0 can serve as the sentinel.
    std::vector<std::int32_t> s(static_cast<std::size_t>(n) + 1);
    for (std::int32_t i = 0; i < n; ++i) s[i] = data[i] + 1;
    s[n] = 0;

    std::vector<std::int32_t> full(static_cast<std::size_t>(n) + 1);
    detail::build(s, full, 257);

    // full[0] is the sentinel suffix; drop it.
    std::copy(full.begin() + 1, full.end(), sa.begin());
}

}  // namespace sais

}  // namespace apultra

// ================================================== matchfinder

namespace apultra {
namespace {

/// Fibonacci-hash an index down to TAG_BITS, used to disambiguate equal LCPs.
[[nodiscard]] inline int indexTag(uint32_t index) noexcept {
    return static_cast<int>((static_cast<uint64_t>(index) * 11400714819323198485ull) >>
                            (64 - kTagBits));
}

}  // namespace

void Compressor::buildSuffixArray(std::span<const uint8_t> window) {
    const auto n = static_cast<int>(window.size());

    sais::buildSuffixArray(window, std::span{suffix_array_}.first(n));
    for (int i = 0; i < n; ++i) intervals_[i] = static_cast<uint64_t>(suffix_array_[i]);

    // Permuted LCP (Karkkainen's method): cheaper and more cache friendly than
    // Kasai, and it needs no inverse suffix array.
    int32_t* phi = plcp_.data();
    phi[intervals_[0]] = -1;
    for (int i = 1; i < n; ++i) phi[intervals_[i]] = static_cast<int32_t>(intervals_[i - 1]);

    int curLen = 0;
    for (int i = 0; i < n; ++i) {
        if (phi[i] == -1) {
            plcp_[i] = 0;
            continue;
        }
        const int maxLen = (i > phi[i]) ? (n - i) : (n - phi[i]);
        while (curLen < maxLen && window[i + curLen] == window[phi[i] + curLen]) ++curLen;
        plcp_[i] = curLen;
        if (curLen > 0) --curLen;
    }

    // Rotate the permuted LCP into SA order, packing (tagged LCP | position).
    intervals_[0] &= kPosMask;
    for (int i = 1; i < n; ++i) {
        const auto index = static_cast<int>(intervals_[i] & kPosMask);
        int len = plcp_[index];
        if (len < 1) len = 0;
        if (len > kLcpMax) len = kLcpMax;
        const int taggedLen = len ? ((len << kTagBits) | (indexTag(static_cast<uint32_t>(index)) &
                                                          ((1 << kTagBits) - 1)))
                                  : 0;
        intervals_[i] = static_cast<uint64_t>(index) |
                        (static_cast<uint64_t>(taggedLen) << kLcpShift);
    }

    // Build the lcp-interval tree in place over intervals_/pos_data_.
    const uint64_t* saAndLcp = intervals_.data();
    uint64_t* posData = pos_data_.data();
    uint64_t* top = open_intervals_.data();
    uint64_t prevPos = saAndLcp[0] & kPosMask;
    uint64_t nextIntervalIdx = 1;

    *top = 0;
    intervals_[0] = 0;

    for (int r = 1; r < n; ++r) {
        const uint64_t nextPos = saAndLcp[r] & kPosMask;
        const uint64_t nextLcp = saAndLcp[r] & kLcpMask;
        const uint64_t topLcp = *top & kLcpMask;

        if (nextLcp == topLcp) {  // continuing the deepest open interval
            posData[prevPos] = *top;
        } else if (nextLcp > topLcp) {  // opening a new interval
            *++top = nextLcp | nextIntervalIdx++;
            posData[prevPos] = *top;
        } else {  // closing the deepest open interval
            posData[prevPos] = *top;
            for (;;) {
                const uint64_t closedIdx = *top-- & kPosMask;
                const uint64_t superLcp = *top & kLcpMask;

                if (nextLcp == superLcp) {  // continuing the superinterval
                    intervals_[closedIdx] = *top;
                    break;
                }
                if (nextLcp > superLcp) {  // new interval between the two
                    *++top = nextLcp | nextIntervalIdx++;
                    intervals_[closedIdx] = *top;
                    break;
                }
                intervals_[closedIdx] = *top;  // also closing the superinterval
            }
        }
        prevPos = nextPos;
    }

    posData[prevPos] = *top;
    for (; top > open_intervals_.data(); --top) intervals_[*top & kPosMask] = *(top - 1);
}

int Compressor::findMatchesAt(int offset, Match* matches, uint16_t* matchDepth, uint8_t& match1,
                              int maxMatches, bool selfContained) {
    uint64_t* intervals = intervals_.data();
    uint64_t* posData = pos_data_.data();

    match1 = 0;

    // Deepest lcp-interval containing the current suffix.
    uint64_t ref = posData[offset];
    posData[offset] = 0;

    // Ascend until we reach a visited interval, the root, or a child of the
    // root, linking unvisited intervals to the current suffix on the way.
    uint64_t superRef;
    while ((superRef = intervals[ref & kPosMask]) & kLcpMask) {
        intervals[ref & kPosMask] = static_cast<uint64_t>(offset) | kVisitedFlag;
        ref = superRef;
    }

    if (superRef == 0) {
        if (ref != 0) intervals[ref & kPosMask] = static_cast<uint64_t>(offset) | kVisitedFlag;
        return 0;
    }

    uint64_t matchPos = superRef & kExclVisitedMask;
    Match* matchPtr = matches;
    uint16_t* depthPtr = matchDepth;
    int prevOffset = 0;
    int prevLen = 0;
    int curDepth = 0;
    uint16_t* curDepthPtr = nullptr;

    // Record a candidate, collapsing runs of (offset-1, length-1) into a depth.
    const auto record = [&](int matchOffset, int matchLen, bool tagged) {
        if (!offsetEncodable(matchOffset))
            return;

        if (prevOffset && prevLen > 2 && matchOffset == prevOffset - 1 &&
            matchLen == prevLen - 1 && curDepthPtr && curDepth < kLcpMax) {
            *curDepthPtr = static_cast<uint16_t>((++curDepth) | (tagged ? 0x8000 : 0));
        } else {
            matchPtr->length = static_cast<uint32_t>(matchLen);
            matchPtr->offset = static_cast<uint32_t>(matchOffset);
            ++matchPtr;
            curDepth = 0;
            *depthPtr = tagged ? 0x8000 : 0;
            curDepthPtr = depthPtr++;
        }
        prevLen = matchLen;
        prevOffset = matchOffset;
    };

    if (selfContained && (matchPtr - matches) < maxMatches) {
        const auto matchOffset = static_cast<int>(offset - matchPos);
        const auto matchLen = static_cast<int>(ref >> (kLcpShift + kTagBits));
        if (matchOffset <= max_offset_ && offsetEncodable(matchOffset)) {
            matchPtr->length = static_cast<uint32_t>(matchLen);
            matchPtr->offset = static_cast<uint32_t>(matchOffset);
            ++matchPtr;
            *depthPtr = 0;
            curDepth = 0;
            curDepthPtr = depthPtr++;
            prevLen = matchLen;
            prevOffset = matchOffset;
        }
    }

    for (;;) {
        // Ascend indirectly via pos_data links.
        if ((superRef = posData[matchPos]) > ref) {
            matchPos = intervals[superRef & kPosMask] & kExclVisitedMask;

            if (selfContained && (matchPtr - matches) < maxMatches) {
                const auto matchOffset = static_cast<int>(offset - matchPos);
                const auto matchLen = static_cast<int>(ref >> (kLcpShift + kTagBits));
                if (matchOffset <= max_offset_ && (prevOffset - matchOffset) >= 128)
                    record(matchOffset, matchLen, true);
            }
        }

        while ((superRef = posData[matchPos]) > ref) {
            matchPos = intervals[superRef & kPosMask] & kExclVisitedMask;

            if (selfContained && (matchPtr - matches) < maxMatches) {
                const auto matchOffset = static_cast<int>(offset - matchPos);
                const auto matchLen = static_cast<int>(ref >> (kLcpShift + kTagBits));
                if (matchOffset <= max_offset_ &&
                    (matchLen >= 3 ||
                     (matchLen >= 2 && (matchPtr - matches) < (maxMatches - 1))) &&
                    matchLen < 1280 && (prevOffset - matchOffset) >= 128)
                    record(matchOffset, matchLen, true);
            }
        }

        intervals[ref & kPosMask] = static_cast<uint64_t>(offset) | kVisitedFlag;
        posData[matchPos] = ref;

        const auto mainOffset = static_cast<int>(offset - matchPos);
        const auto mainLen = static_cast<int>(ref >> (kLcpShift + kTagBits));

        if ((matchPtr - matches) < maxMatches && mainOffset <= max_offset_ &&
            mainOffset != prevOffset)
            record(mainOffset, mainLen, false);

        if (shortOffsetEncodable(mainOffset) && mainLen) match1 = static_cast<uint8_t>(mainOffset);

        if (superRef == 0) break;
        ref = superRef;
        matchPos = intervals[ref & kPosMask] & kExclVisitedMask;

        if (selfContained && (matchPtr - matches) < maxMatches) {
            const auto matchOffset = static_cast<int>(offset - matchPos);
            const auto matchLen = static_cast<int>(ref >> (kLcpShift + kTagBits));
            if (matchOffset <= max_offset_ && matchLen >= 2 && (prevOffset - matchOffset) >= 128)
                record(matchOffset, matchLen, true);
        }
    }

    return static_cast<int>(matchPtr - matches);
}

void Compressor::skipMatches(int startOffset, int endOffset) {
    // Skipping still has to scan, as that lazily updates the intervals; the
    // matches themselves are discarded.
    Match match{};
    uint16_t depth = 0;
    uint8_t match1 = 0;
    for (int i = startOffset; i < endOffset; ++i)
        findMatchesAt(i, &match, &depth, match1, 0, false);
}

/// The matchfinder only reports offsets with longer matches, look for any single byte match too.
void Compressor::findShortMatches(std::span<const uint8_t> window, int startOffset, int endOffset) {
    for (int i = startOffset; i < endOffset; ++i) {
        uint8_t& match1 = match1_[i - startOffset];
        for (int off = 2; !match1 && shortOffsetEncodable(off) && off <= i; off += 2)
            if (window[i] && window[i - off] == window[i]) match1 = static_cast<uint8_t>(off);
    }
}

void Compressor::findAllMatches(int startOffset, int endOffset, int blockFlags) {
    const bool selfContained = (blockFlags & 3) == 3;

    for (int i = startOffset; i < endOffset; ++i) {
        const int index = i - startOffset;
        Match* matches = matchesAt(index);
        uint16_t* depths = depthsAt(index);
        const int found =
            findMatchesAt(i, matches, depths, match1_[index], kMatchesPerIndex, selfContained);

        if (found < kMatchesPerIndex) {
            std::memset(matches + found, 0, sizeof(Match) * (kMatchesPerIndex - found));
            std::memset(depths + found, 0, sizeof(uint16_t) * (kMatchesPerIndex - found));
        }
    }
}

}  // namespace apultra

// ================================================== optimal parser / writer

namespace apultra {
namespace {

/// Longest common prefix of window[a...] and window[b...], bounded by `max`.
[[nodiscard]] inline int matchLen(const uint8_t* a, const uint8_t* b, int max) noexcept {
    int len = 0;
    while (len + 8 <= max && std::memcmp(a + len, b + len, 8) == 0) len += 8;
    while (len < max && a[len] == b[len]) ++len;
    return len;
}

}  // namespace

Compressor::Compressor(int blockSize, int maxWindowSize, int maxArrivals, int maxOffset)
    : block_size_(blockSize), arrivals_(maxArrivals), max_offset_(maxOffset) {
    const auto window = static_cast<std::size_t>(maxWindowSize);
    const auto block = static_cast<std::size_t>(blockSize);

    intervals_.resize(window);
    pos_data_.resize(window);
    open_intervals_.resize(kLcpAndTagMax + 1);
    suffix_array_.resize(window);
    plcp_.resize(window);
    rle_len_.resize(window);
    visited_.resize(block);
    first_offset_for_byte_.resize(65536);
    next_offset_for_pos_.resize(block);
    if (maxArrivals == kArrivalsMax) offset_cache_.resize(2048);
    match_.resize(block * kMatchesPerIndex);
    match_depth_.resize(block * kMatchesPerIndex);
    match1_.resize(block);
    best_match_.resize(block);
    arrival_.resize((block + 1) * static_cast<std::size_t>(maxArrivals));
}

// --------------------------------------------------------- forward rep matches

void Compressor::insertForwardMatch(const uint8_t* window, int i, int matchOffset, int startOffset,
                                    int endOffset, int depth) {
    const Arrival* arrival = arrivalsAt(i - startOffset);

    for (int j = 0; j < arrivals_ && arrival[j].from_slot; ++j) {
        if (!arrival[j].follows_literal) continue;

        const auto repOffset = static_cast<int>(arrival[j].rep_offset);
        if (matchOffset == repOffset || repOffset == 0) continue;

        const auto repPos = static_cast<int>(arrival[j].rep_pos);
        if (repPos < startOffset || (repPos + 1) >= endOffset ||
            visited_[repPos - startOffset] == matchOffset)
            continue;
        visited_[repPos - startOffset] = matchOffset;

        Match* fwdMatch = matchesAt(repPos - startOffset);
        if (fwdMatch[kMatchesPerIndex - 1].length != 0) continue;
        if (repPos < matchOffset) continue;

        const uint8_t* start = window + repPos;
        if (std::memcmp(start, start - matchOffset, 2) != 0) continue;

        // The run lengths give a lower bound on the match length for free.
        const int len0 = rle_len_[repPos - matchOffset];
        const int len1 = rle_len_[repPos];
        int maxRepLen = endOffset - repPos;
        if (maxRepLen > kLcpMax) maxRepLen = kLcpMax;
        int at = std::min(std::min(len0, len1), maxRepLen);
        at += matchLen(start + at, start + at - matchOffset, maxRepLen - at);
        const int curRepLen = at;

        uint16_t* fwdDepth = depthsAt(repPos - startOffset);
        int r = 0;
        for (; fwdMatch[r].length; ++r) {
            if (fwdMatch[r].offset == static_cast<uint32_t>(matchOffset) &&
                (fwdDepth[r] & 0x3fff) == 0) {
                if (static_cast<int>(fwdMatch[r].length) < curRepLen) {
                    fwdMatch[r].length = static_cast<uint32_t>(curRepLen);
                    fwdDepth[r] = 0;
                }
                break;
            }
        }

        if (fwdMatch[r].length == 0) {
            fwdMatch[r].length = static_cast<uint32_t>(curRepLen);
            fwdMatch[r].offset = static_cast<uint32_t>(matchOffset);
            fwdDepth[r] = 0;

            if (depth < 9)
                insertForwardMatch(window, repPos, matchOffset, startOffset, endOffset, depth + 1);
        }
    }
}

// ------------------------------------------------------------- optimal parser

void Compressor::optimizeForward(const uint8_t* window, int startOffset, int endOffset,
                                 bool insertForwardReps, int curRepMatchOffset, int blockFlags) {
    if ((endOffset - startOffset) > block_size_) return;

    for (int pos = 0; pos <= endOffset - startOffset; ++pos) {
        Arrival* slots = arrivalsAt(pos);
        std::memset(slots, 0, sizeof(Arrival) * arrivals_);
        for (int j = 0; j < arrivals_; ++j) slots[j].cost = 0x40000000;
    }

    Arrival* first = arrivalsAt(0);
    first->cost = 0;
    first->from_slot = -1;
    first->rep_offset = static_cast<uint32_t>(curRepMatchOffset);

    if (insertForwardReps)
        std::memset(visited_.data(), 0, sizeof(int32_t) * (endOffset - startOffset));

    int i = startOffset;
    Arrival* cur_arrival = arrivalsAt(0);
    int repMatchArrivalIdx[(2 * kArrivalsMax) + 1];

    for (; i != endOffset; ++i, cur_arrival += arrivals_) {
        int j;

        // Cost of coding position i as a literal or as a short offset match.
        const uint8_t match1Offs = match1_[i - startOffset];
        int shortOffset, shortLen, literalScore, literalCost;

        if ((window[i] != 0 && match1Offs == 0) || (i == startOffset && (blockFlags & 1))) {
            shortOffset = 0;
            shortLen = 0;
            literalScore = 1;
            literalCost = 9;  // literal bit + literal byte
        } else {
            shortOffset = (window[i] != 0) ? match1Offs : 0;
            shortLen = 1;
            literalScore = shortOffset ? 3 : 1;
            literalCost = kShortOffsetBits + kSizeShortMatch;  // command and offset, no length
        }

        if (cur_arrival[arrivals_].from_slot) {
            Arrival* dest = &cur_arrival[arrivals_];

            for (j = 0; j < arrivals_ && cur_arrival[j].from_slot; ++j) {
                const int cost = cur_arrival[j].cost + literalCost;
                const int score = cur_arrival[j].score + literalScore;
                const auto repOffset = static_cast<int>(cur_arrival[j].rep_offset);

                if (cost < dest[arrivals_ - 1].cost ||
                    (cost == dest[arrivals_ - 1].cost && score < dest[arrivals_ - 1].score &&
                     repOffset != static_cast<int>(dest[arrivals_ - 1].rep_offset))) {
                    bool exists = false;
                    int n = 0;

                    for (; dest[n].cost < cost; ++n) {
                        if (static_cast<int>(dest[n].rep_offset) == repOffset) {
                            exists = true;
                            break;
                        }
                    }

                    if (exists) continue;

                    for (; dest[n].cost == cost && score >= dest[n].score; ++n) {
                        if (static_cast<int>(dest[n].rep_offset) == repOffset) {
                            exists = true;
                            break;
                        }
                    }

                    if (exists) continue;

                    int z = n;
                    for (; z < arrivals_ - 1 && dest[z].cost == cost; ++z) {
                        if (static_cast<int>(dest[z].rep_offset) == repOffset) {
                            exists = true;
                            break;
                        }
                    }

                    if (exists) continue;

                    for (; z < arrivals_ - 1 && dest[z].from_slot; ++z)
                        if (static_cast<int>(dest[z].rep_offset) == repOffset) break;

                    std::memmove(&dest[n + 1], &dest[n], sizeof(Arrival) * (z - n));

                    Arrival& a = dest[n];
                    a.cost = cost;
                    a.from_pos = static_cast<uint32_t>(i);
                    a.from_slot = static_cast<int32_t>(j + 1);
                    a.follows_literal = 1;
                    a.rep_offset = static_cast<uint32_t>(repOffset);
                    a.short_offset = static_cast<uint32_t>(shortOffset);
                    a.rep_pos = cur_arrival[j].rep_pos;
                    a.match_len = static_cast<uint32_t>(shortLen);
                    a.score = score;
                }
            }
        } else {
            Arrival* dest = &cur_arrival[arrivals_];

            for (j = 0; j < arrivals_ && cur_arrival[j].from_slot; ++j, ++dest) {
                dest->cost = cur_arrival[j].cost + literalCost;
                dest->from_pos = static_cast<uint32_t>(i);
                dest->from_slot = static_cast<int32_t>(j + 1);
                dest->follows_literal = 1;
                dest->rep_offset = cur_arrival[j].rep_offset;
                dest->short_offset = static_cast<uint32_t>(shortOffset);
                dest->rep_pos = cur_arrival[j].rep_pos;
                dest->match_len = static_cast<uint32_t>(shortLen);
                dest->score = cur_arrival[j].score + literalScore;
            }
        }

        if (i == startOffset && (blockFlags & 1)) continue;

        const Match* match = matchesAt(i - startOffset);
        const uint16_t* matchDepth = depthsAt(i - startOffset);
        const int numArrivalsForThisPos = j;
        int overallMinRepLen = 0, overallMaxRepLen = 0;
        int numRepMatchArrivals = 0;

        // Collect the reachable rep-match lengths for this position.
        if ((i + 2) <= endOffset) {
            int maxRepLenForPos = endOffset - i;
            if (maxRepLenForPos > kLcpMax) maxRepLenForPos = kLcpMax;
            const uint8_t* start = window + i;

            for (j = 0; j < numArrivalsForThisPos; ++j) {
                if (!cur_arrival[j].follows_literal) continue;

                const auto repOffset = static_cast<int>(cur_arrival[j].rep_offset);
                if (i < repOffset || repOffset == 0) continue;
                if (std::memcmp(start, start - repOffset, 2) != 0) continue;

                const int len0 = rle_len_[i - repOffset];
                const int len1 = rle_len_[i];
                int at = std::min(std::min(len0, len1), maxRepLenForPos);
                at += matchLen(start + at, start + at - repOffset, maxRepLenForPos - at);

                repMatchArrivalIdx[numRepMatchArrivals++] = j;
                repMatchArrivalIdx[numRepMatchArrivals++] = at;
                if (overallMaxRepLen < at) overallMaxRepLen = at;
            }
        }
        repMatchArrivalIdx[numRepMatchArrivals] = -1;

        for (int m = 0; m < kMatchesPerIndex && match[m].length; ++m) {
            int origMatchLen = static_cast<int>(match[m].length);
            const auto origMatchOffset = static_cast<int>(match[m].offset);
            const unsigned origMatchDepth = matchDepth[m] & 0x3fff;
            const int scorePenalty = 3 + (matchDepth[m] >> 15);

            if ((i + origMatchLen) > endOffset) origMatchLen = endOffset - i;

            // Consider the match at its own offset and, for collapsed runs, at
            // the far end of the run.
            for (unsigned d = 0; d <= origMatchDepth; d += (origMatchDepth ? origMatchDepth : 1)) {
                const int nMatchLen = origMatchLen - static_cast<int>(d);
                const int nMatchOffset = origMatchOffset - static_cast<int>(d);

                if (insertForwardReps)
                    insertForwardMatch(window, i, nMatchOffset, startOffset, endOffset, 0);

                if (nMatchLen >= 2) {
                    const int noRepCostAdjustment = (nMatchLen >= kLcpMax) ? 1 : 0;

                    int minMatchLenForOffset;
                    if (nMatchOffset < kMinMatch3Offset)
                        minMatchLenForOffset = 2;
                    else if (nMatchOffset < kMinMatch4Offset)
                        minMatchLenForOffset = 3;
                    else
                        minMatchLenForOffset = 4;

                    const int startingMatchLen =
                        (nMatchLen >= kLeaveAloneMatchSize && i >= nMatchLen) ? nMatchLen : 2;
                    const int jumpMatchLen =
                        ((blockFlags & 3) == 3 && nMatchLen > 90 && i >= 90) ? 90 : nMatchLen + 1;

                    int noRepOffsetCostForLit[2];
                    if (startingMatchLen <= 3 && nMatchOffset < 128) {
                        noRepOffsetCostForLit[0] = noRepOffsetCostForLit[1] = 8 + kSize7BitMatch;
                    } else {
                        noRepOffsetCostForLit[0] = farOffsetSize(nMatchOffset, 0);
                        noRepOffsetCostForLit[1] = farOffsetSize(nMatchOffset, 1);
                    }
                    const int noRepOffsetCostDelta =
                        noRepOffsetCostForLit[1] - noRepOffsetCostForLit[0];

                    for (int k = startingMatchLen; k <= nMatchLen; ++k) {
                        const int repMatchLenCost = gamma2Size(k);
                        Arrival* dest = &cur_arrival[static_cast<std::size_t>(k) * arrivals_];

                        // --- non-rep match candidate
                        // (odd offsets can only use the 7 bit match encoding)
                        if (k >= minMatchLenForOffset &&
                            (!(nMatchOffset & 1) || (k <= 3 && nMatchOffset < 128))) {
                            int noRepMatchLenCost;
                            if (k <= 3 && nMatchOffset < 128)
                                noRepMatchLenCost = 0;
                            else if (nMatchOffset < 128 || nMatchOffset >= kMinMatch4Offset)
                                noRepMatchLenCost = gamma2Size(k - 2);
                            else if (nMatchOffset < kMinMatch3Offset)
                                noRepMatchLenCost = repMatchLenCost;
                            else
                                noRepMatchLenCost = gamma2Size(k - 1);

                            for (j = 0; j < numArrivalsForThisPos; ++j) {
                                const unsigned followsLiteral = cur_arrival[j].follows_literal;
                                if (nMatchOffset == static_cast<int>(cur_arrival[j].rep_offset) &&
                                    followsLiteral != 0)
                                    continue;

                                const int cost = cur_arrival[j].cost + noRepMatchLenCost +
                                                 noRepOffsetCostForLit[followsLiteral];
                                if (cost > (dest[arrivals_ - 1].cost + 1)) break;

                                const int score = cur_arrival[j].score + scorePenalty;

                                if (cost < dest[arrivals_ - 2].cost ||
                                    (cost == dest[arrivals_ - 2].cost &&
                                     score < dest[arrivals_ - 2].score &&
                                     (cost != dest[arrivals_ - 1].cost ||
                                      nMatchOffset !=
                                          static_cast<int>(dest[arrivals_ - 1].rep_offset)))) {
                                    bool exists = false;
                                    int n = 0;

                                    for (; dest[n].cost < cost; ++n) {
                                        if (static_cast<int>(dest[n].rep_offset) == nMatchOffset) {
                                            exists = true;
                                            break;
                                        }
                                    }

                                    if (exists) {
                                        if ((cost - dest[n].cost) >= noRepOffsetCostDelta) break;
                                    } else {
                                        const int revisedCost = cost - noRepCostAdjustment;

                                        for (; n < arrivals_ - 1 && dest[n].cost == revisedCost &&
                                               score >= dest[n].score;
                                             ++n) {
                                            if (static_cast<int>(dest[n].rep_offset) ==
                                                nMatchOffset) {
                                                exists = true;
                                                break;
                                            }
                                        }

                                        if (!exists && n < arrivals_ - 1) {
                                            int z = n;
                                            for (; z < arrivals_ - 1 && dest[z].cost == cost; ++z) {
                                                if (static_cast<int>(dest[z].rep_offset) ==
                                                    nMatchOffset) {
                                                    exists = true;
                                                    break;
                                                }
                                            }

                                            if (!exists) {
                                                for (; z < arrivals_ - 1 && dest[z].from_slot; ++z)
                                                    if (static_cast<int>(dest[z].rep_offset) ==
                                                        nMatchOffset)
                                                        break;

                                                std::memmove(&dest[n + 1], &dest[n],
                                                             sizeof(Arrival) * (z - n));

                                                Arrival& a = dest[n];
                                                a.cost = revisedCost;
                                                a.from_pos = static_cast<uint32_t>(i);
                                                a.from_slot = static_cast<int32_t>(j + 1);
                                                a.follows_literal = 0;
                                                a.rep_offset = static_cast<uint32_t>(nMatchOffset);
                                                a.short_offset = 0;
                                                a.rep_pos = static_cast<uint32_t>(i);
                                                a.match_len = static_cast<uint32_t>(k);
                                                a.score = score;
                                            }
                                        }
                                    }
                                }
                                if (followsLiteral == 0 || noRepOffsetCostDelta == 0) break;
                            }
                        }

                        if (k == 3 && nMatchOffset < 128)
                            noRepOffsetCostForLit[0] = noRepOffsetCostForLit[1] =
                                8 + kFarRawBits + kSizeLargeMatch + 2;

                        // --- rep-match candidate
                        if (k > overallMinRepLen && k <= overallMaxRepLen) {
                            const int repCmdCost = kSizeLargeMatch + 2 + repMatchLenCost;

                            if (k <= 90)
                                overallMinRepLen = k;
                            else if (overallMaxRepLen == k)
                                --overallMaxRepLen;

                            for (int r = 0; (j = repMatchArrivalIdx[r]) >= 0; r += 2) {
                                if (repMatchArrivalIdx[r + 1] < k) continue;

                                const int cost = cur_arrival[j].cost + repCmdCost;
                                const int score = cur_arrival[j].score + 2;
                                const auto repOffset = static_cast<int>(cur_arrival[j].rep_offset);

                                if (!(cost < dest[arrivals_ - 1].cost ||
                                      (cost == dest[arrivals_ - 1].cost &&
                                       score < dest[arrivals_ - 1].score &&
                                       repOffset !=
                                           static_cast<int>(dest[arrivals_ - 1].rep_offset))))
                                    break;

                                bool exists = false;
                                int n = 0;

                                for (; dest[n].cost < cost; ++n) {
                                    if (static_cast<int>(dest[n].rep_offset) == repOffset) {
                                        exists = true;
                                        break;
                                    }
                                }

                                if (exists) continue;

                                for (; dest[n].cost == cost && score >= dest[n].score; ++n) {
                                    if (static_cast<int>(dest[n].rep_offset) == repOffset) {
                                        exists = true;
                                        break;
                                    }
                                }

                                if (exists) continue;

                                int z = n;
                                for (; z < arrivals_ - 1 && dest[z].cost == cost; ++z) {
                                    if (static_cast<int>(dest[z].rep_offset) == repOffset) {
                                        exists = true;
                                        break;
                                    }
                                }

                                if (exists) continue;

                                for (; z < arrivals_ - 1 && dest[z].from_slot; ++z)
                                    if (static_cast<int>(dest[z].rep_offset) == repOffset) break;

                                std::memmove(&dest[n + 1], &dest[n], sizeof(Arrival) * (z - n));

                                Arrival& a = dest[n];
                                a.cost = cost;
                                a.from_pos = static_cast<uint32_t>(i);
                                a.from_slot = static_cast<int32_t>(j + 1);
                                a.follows_literal = 0;
                                a.rep_offset = static_cast<uint32_t>(repOffset);
                                a.short_offset = 0;
                                a.rep_pos = static_cast<uint32_t>(i);
                                a.match_len = static_cast<uint32_t>(k);
                                a.score = score;
                            }
                        }

                        if (k == jumpMatchLen) k = nMatchLen - 1;
                    }
                }

                if (origMatchLen >= 512) break;
            }
        }
    }

    if (!insertForwardReps) {
        const Arrival* end = arrivalsAt(i - startOffset);

        while (end->from_slot > 0 && end->from_pos < static_cast<uint32_t>(endOffset)) {
            const auto fromPos = static_cast<int>(end->from_pos);
            bestMatch(fromPos).length = static_cast<int>(end->match_len);
            bestMatch(fromPos).offset = static_cast<int>(
                (end->match_len >= 2) ? end->rep_offset : end->short_offset);
            end = arrivalsAt(fromPos - startOffset) + (end->from_slot - 1);
        }
    }
}

// ------------------------------------------------------------ command reducer

bool Compressor::reduceCommands(const uint8_t* window, int startOffset, int endOffset,
                                int curRepMatchOffset, int blockFlags) {
    int repMatchOffset = curRepMatchOffset;
    bool followsLiteral = false;
    bool didReduce = false;
    int lastMatchLen = 0;

    const auto match1 = [&](int pos) { return static_cast<int>(match1_[pos - startOffset]); };

    for (int i = startOffset + (blockFlags & 1); i < endOffset;) {
        FinalMatch& match = bestMatch(i);

        // Try to grow the following match backwards over this literal.
        if (match.length <= 1 && (i + 1) < endOffset && bestMatch(i + 1).length >= 2 &&
            bestMatch(i + 1).length < kMaxVarLen && bestMatch(i + 1).offset &&
            i >= bestMatch(i + 1).offset && (i + bestMatch(i + 1).length + 1) <= endOffset &&
            std::memcmp(window + i - bestMatch(i + 1).offset, window + i,
                        bestMatch(i + 1).length + 1) == 0) {
            const FinalMatch next = bestMatch(i + 1);

            if (next.offset < kMinMatch4Offset || (next.length + 1) >= 4 ||
                (next.offset == repMatchOffset && followsLiteral)) {
                int curSize = (match.length == 1) ? (kSizeShortMatch + kShortOffsetBits) : (1 + 8);
                if (next.offset == repMatchOffset)  // always follows the literal at i
                    curSize += kSizeLargeMatch + 2 + gamma2Size(next.length);
                else
                    curSize += offsetVarLenSize(next.length, next.offset, 1) +
                               matchVarLenSize(next.length, next.offset);

                int reducedSize;
                if (next.offset == repMatchOffset && followsLiteral)
                    reducedSize = kSizeLargeMatch + 2 + gamma2Size(next.length + 1);
                else
                    reducedSize = offsetVarLenSize(next.length + 1, next.offset, followsLiteral) +
                                  matchVarLenSize(next.length + 1, next.offset);

                if (reducedSize < curSize || (!followsLiteral && lastMatchLen >= kLcpMax)) {
                    match.length = next.length + 1;
                    match.offset = next.offset;
                    bestMatch(i + 1).length = 0;
                    bestMatch(i + 1).offset = 0;
                    didReduce = true;
                    continue;
                }
            }
        }

        if (match.length >= 2) {
            // Large matches always beat literals, don't bother evaluating them.
            if (match.length < kLcpMax) {
                int nextIndex = i + match.length;
                bool nextFollowsLiteral = false;

                while (nextIndex < endOffset && bestMatch(nextIndex).length < 2) {
                    ++nextIndex;
                    nextFollowsLiteral = true;
                }

                if (nextIndex < endOffset && bestMatch(nextIndex).length >= 2) {
                    bool cannotEncode = false;
                    const FinalMatch next = bestMatch(nextIndex);

                    // Try to turn this match into a rep-match of the next one.
                    if (repMatchOffset && repMatchOffset != match.offset && next.offset &&
                        match.offset != next.offset && nextFollowsLiteral &&
                        i >= next.offset && (i + match.length) <= endOffset &&
                        (next.offset < kMinMatch3Offset || match.length >= 3) &&
                        (next.offset < kMinMatch4Offset || match.length >= 4)) {
                        const uint8_t* at = window + i;
                        const int maxLen = matchLen(at, at - next.offset, match.length);

                        if (maxLen >= match.length) {
                            match.offset = next.offset;
                            didReduce = true;
                        } else if (maxLen >= 2 &&
                                   ((followsLiteral && repMatchOffset == next.offset) ||
                                    ((next.offset < kMinMatch3Offset || maxLen >= 3) &&
                                     (next.offset < kMinMatch4Offset || maxLen >= 4)))) {
                            int before = offsetVarLenSize(match.length, match.offset, followsLiteral) +
                                         matchVarLenSize(match.length, match.offset) +
                                         offsetVarLenSize(next.length, next.offset, 1) +
                                         matchVarLenSize(next.length, next.offset);

                            int after = offsetVarLenSize(maxLen, next.offset, followsLiteral);
                            if (followsLiteral && repMatchOffset == next.offset)
                                after += gamma2Size(maxLen);
                            else
                                after += matchVarLenSize(maxLen, next.offset);
                            after += kSizeLargeMatch + 2 + gamma2Size(next.length);

                            for (int j = maxLen; j < match.length; ++j)
                                after += (window[i + j] == 0 || match1(i + j)) ? (kSizeShortMatch + kShortOffsetBits)
                                                                              : (1 + 8);

                            if (after < before) {
                                // Shorter rep-match plus literals still wins.
                                const int origLen = match.length;
                                match.offset = next.offset;
                                match.length = maxLen;

                                for (int j = maxLen; j < origLen; ++j) {
                                    bestMatch(i + j).offset = match1(i + j);
                                    bestMatch(i + j).length =
                                        (window[i + j] && match1(i + j) == 0) ? 0 : 1;
                                }

                                didReduce = true;
                                continue;
                            }
                        }
                    }

                    int curSize;
                    if (match.offset == repMatchOffset && followsLiteral)
                        curSize = kSizeLargeMatch + 2 + gamma2Size(match.length);
                    else
                        curSize = offsetVarLenSize(match.length, match.offset, followsLiteral) +
                                  matchVarLenSize(match.length, match.offset);

                    int nextSize;
                    if (next.offset == match.offset && nextFollowsLiteral && next.length >= 2)
                        nextSize = kSizeLargeMatch + 2 + gamma2Size(next.length);
                    else
                        nextSize = offsetVarLenSize(next.length, next.offset, nextFollowsLiteral) +
                                   matchVarLenSize(next.length, next.offset);

                    const int originalCombined = curSize + nextSize;

                    // Cost of coding this match as literals instead.
                    int reducedSize = 0;
                    for (int j = 0; j < match.length; ++j)
                        reducedSize +=
                            (window[i + j] == 0 || match1(i + j)) ? (kSizeShortMatch + kShortOffsetBits) : (1 + 8);

                    if (next.offset == repMatchOffset && next.length >= 2) {
                        reducedSize += kSizeLargeMatch + 2 + gamma2Size(next.length);
                    } else if ((next.length < 3 && next.offset >= kMinMatch3Offset) ||
                               (next.length < 4 && next.offset >= kMinMatch4Offset)) {
                        cannotEncode = true;  // only encodable as a rep-match
                    } else {
                        reducedSize += offsetVarLenSize(next.length, next.offset, 1) +
                                       matchVarLenSize(next.length, next.offset);
                    }

                    if (originalCombined > reducedSize && !cannotEncode) {
                        const int len = match.length;
                        for (int j = 0; j < len; ++j) {
                            bestMatch(i + j).offset = match1(i + j);
                            bestMatch(i + j).length =
                                (window[i + j] && match1(i + j) == 0) ? 0 : 1;
                        }
                        didReduce = true;
                        continue;
                    }
                }
            }

            // Join adjacent large matches when that shortens the stream.
            const int tail = i + match.length;
            if (tail < endOffset && match.offset > 0 && bestMatch(tail).offset > 0 &&
                bestMatch(tail).length >= 2 &&
                (match.length + bestMatch(tail).length) <= kMaxVarLen && tail >= match.offset &&
                tail >= bestMatch(tail).offset && (tail + bestMatch(tail).length) <= endOffset &&
                std::memcmp(window + tail - match.offset, window + tail - bestMatch(tail).offset,
                            bestMatch(tail).length) == 0) {
                const int len = match.length;
                const FinalMatch second = bestMatch(tail);
                int nextIndex = tail + second.length;
                bool nextFollowsLiteral = false;
                bool cannotEncode = false;

                while (nextIndex < endOffset && bestMatch(nextIndex).length < 2) {
                    ++nextIndex;
                    nextFollowsLiteral = true;
                }

                int curSize;
                if (match.offset == repMatchOffset && followsLiteral)
                    curSize = kSizeLargeMatch + 2 + gamma2Size(len);
                else
                    curSize = offsetVarLenSize(len, match.offset, followsLiteral) +
                              matchVarLenSize(len, match.offset);
                curSize += offsetVarLenSize(second.length, second.offset, 0) +
                           matchVarLenSize(second.length, second.offset);

                if (nextIndex < endOffset && bestMatch(nextIndex).length >= 2) {
                    const FinalMatch next = bestMatch(nextIndex);
                    if (next.offset == second.offset && nextFollowsLiteral)
                        curSize += kSizeLargeMatch + 2 + gamma2Size(next.length);
                    else
                        curSize += offsetVarLenSize(next.length, next.offset, nextFollowsLiteral) +
                                   matchVarLenSize(next.length, next.offset);
                }

                int reducedSize;
                if (match.offset == repMatchOffset && followsLiteral)
                    reducedSize = kSizeLargeMatch + 2 + gamma2Size(len + second.length);
                else
                    reducedSize = offsetVarLenSize(len + second.length, match.offset, followsLiteral) +
                                  matchVarLenSize(len + second.length, match.offset);

                if (nextIndex < endOffset && bestMatch(nextIndex).length >= 2) {
                    const FinalMatch next = bestMatch(nextIndex);
                    if (next.offset == match.offset && nextFollowsLiteral) {
                        reducedSize += kSizeLargeMatch + 2 + gamma2Size(next.length);
                    } else {
                        reducedSize += offsetVarLenSize(next.length, next.offset, nextFollowsLiteral) +
                                       matchVarLenSize(next.length, next.offset);
                        if ((next.offset >= kMinMatch3Offset && next.length < 3) ||
                            (next.offset >= kMinMatch4Offset && next.length < 4))
                            cannotEncode = true;
                    }
                }

                if (curSize >= reducedSize && !cannotEncode) {
                    match.length += second.length;
                    bestMatch(tail).length = 0;
                    bestMatch(tail).offset = 0;
                    didReduce = true;
                    continue;
                }
            }

            repMatchOffset = match.offset;
            followsLiteral = false;
            lastMatchLen = match.length;
            i += match.length;
        } else {
            // Short offset (1 byte match) or literal
            ++i;
            followsLiteral = true;
            lastMatchLen = 0;
        }
    }

    return didReduce;
}

// -------------------------------------------------------------- block writer

bool Compressor::writeBlock(const uint8_t* window, int startOffset, int endOffset,
                            BitWriter& writer, int& followsLiteral, int& repMatchOffset,
                            int blockFlags) {
    int rep = repMatchOffset;
    int curFollowsLiteral = followsLiteral;

    if (blockFlags & 1) {
        writer.writeByte(window[startOffset]);
        curFollowsLiteral = 1;
    }

    for (int i = startOffset + (blockFlags & 1); i < endOffset;) {
        const FinalMatch& match = bestMatch(i);

        if (match.length >= 2) {
            const int len = match.length;
            const int offset = match.offset;

            if (offset < kMinOffset || offset > max_offset_) return false;

            if (offset == rep && curFollowsLiteral) {
                // Rep-match: gamma2(2) selects "same offset as last time".
                writer.writeBits(kCodeLargeMatch, kSizeLargeMatch);
                writer.writeBits(0, 2);
                writer.writeGamma2(len);
                curFollowsLiteral = 0;
            } else if (len <= 3 && offset < 128) {
                // 7 bits offset + 1 bit length
                writer.writeBits(kCode7BitMatch, kSize7BitMatch);
                writer.writeByte(static_cast<uint8_t>(((offset & 0x7f) << 1) | (len - 2)));
                curFollowsLiteral = 0;
                rep = offset;
            } else if (offset & 1) {
                // This offset cannot be encoded, emit literals instead.
                for (int l = 0; l < len; ++l) {
                    writer.writeBits(0, 1);
                    writer.writeByte(window[i + l]);
                }
                curFollowsLiteral = 1;
            } else {
                // Even offset: gamma2 high bits, raw bits, LSB byte (in halfwords).
                // Followed by the length as a gamma2 value
                writer.writeBits(kCodeLargeMatch, kSizeLargeMatch);
                writer.writeGamma2(farOffsetGamma(offset, curFollowsLiteral & 1));
                writer.writeBits((offset >> 9) & ((1 << kFarRawBits) - 1), kFarRawBits);
                writer.writeByte(static_cast<uint8_t>((offset >> 1) & 0xff));

                if (offset < 128 || offset >= kMinMatch4Offset)
                    writer.writeGamma2(len - 2);
                else if (offset < kMinMatch3Offset)
                    writer.writeGamma2(len);
                else
                    writer.writeGamma2(len - 1);

                curFollowsLiteral = 0;
                rep = offset;
            }

            i += len;
        } else if (match.length == 1) {
            // Short offset, in halfwords
            if (match.offset != 0 && !shortOffsetEncodable(match.offset)) return false;
            writer.writeBits(kCodeShortMatch, kSizeShortMatch);
            writer.writeBits(match.offset >> 1, kShortOffsetBits);
            ++i;
            curFollowsLiteral = 1;
        } else {
            writer.writeBits(0, 1);  // literal
            writer.writeByte(window[i]);
            ++i;
            curFollowsLiteral = 1;
        }

        if (writer.failed()) return false;
    }

    if (blockFlags & 2) {
        // End of data: 7-bit offset command with a zero offset.
        writer.writeBits(kCode7BitMatch, kSize7BitMatch);
        writer.writeByte(0x00);
    }

    if (writer.failed()) return false;

    repMatchOffset = rep;
    followsLiteral = curFollowsLiteral;
    return true;
}

// ----------------------------------------------------- match set supplements

void Compressor::supplementMatches(const uint8_t* window, int prevBlockSize, int inDataSize,
                                   int endOffset, int /*blockFlags*/) {
    std::fill(first_offset_for_byte_.begin(), first_offset_for_byte_.end(), -1);
    std::fill_n(next_offset_for_pos_.begin(), inDataSize, -1);

    const auto pair = [&](int pos) {
        return static_cast<unsigned>(window[pos]) | (static_cast<unsigned>(window[pos + 1]) << 8);
    };

    for (int pos = prevBlockSize; pos < endOffset - 1; ++pos) {
        next_offset_for_pos_[pos - prevBlockSize] = first_offset_for_byte_[pair(pos)];
        first_offset_for_byte_[pair(pos)] = pos;
    }

    // Add 2- and 3-byte matches that the suffix array pass did not keep.
    for (int pos = prevBlockSize + 1; pos < endOffset - 1; ++pos) {
        Match* match = matchesAt(pos - prevBlockSize);
        uint16_t* depth = depthsAt(pos - prevBlockSize);
        int m = 0, inserted = 0;

        while (m < 15 && match[m].length) ++m;

        for (int matchPos = next_offset_for_pos_[pos - prevBlockSize]; m < 15 && matchPos >= 0;
             matchPos = next_offset_for_pos_[matchPos - prevBlockSize]) {
            const int offset = pos - matchPos;
            if (offset > max_offset_) break;

            bool exists = false;
            for (int e = 0; e < m; ++e) {
                if (static_cast<int>(match[e].offset) == offset ||
                    static_cast<int>(match[e].offset) - (depth[e] & 0x3fff) == offset) {
                    exists = true;
                    break;
                }
            }
            if (exists || !offsetEncodable(offset)) continue;

            match[m].length =
                (pos < (endOffset - 2) && window[matchPos + 2] == window[pos + 2]) ? 3 : 2;
            match[m].offset = static_cast<uint32_t>(offset);
            depth[m] = 0x4000;
            ++m;
            if (++inserted >= 6) break;
        }
    }
}

void Compressor::supplementMatchesFurther(const uint8_t* window, int prevBlockSize, int endOffset) {
    std::fill(offset_cache_.begin(), offset_cache_.end(), -1);

    for (int pos = prevBlockSize + 1; pos < endOffset - 1; ++pos) {
        Match* match = matchesAt(pos - prevBlockSize);
        if (match[0].length >= 8) continue;

        uint16_t* depth = depthsAt(pos - prevBlockSize);
        int m = 0, inserted = 0;
        int maxForwardPos = std::min(pos + 2 + 1 + 5, endOffset - 2);

        while (m < 46 && match[m].length) {
            offset_cache_[match[m].offset & 2047] = pos;
            offset_cache_[(match[m].offset - (depth[m] & 0x3fff)) & 2047] = pos;
            ++m;
        }

        const auto lenAt = [&](int matchPos) {
            int len = 2;
            while (len < 16 && (pos + len) < endOffset && window[matchPos + len] == window[pos + len])
                ++len;
            return len;
        };

        for (int matchPos = next_offset_for_pos_[pos - prevBlockSize]; m < 46 && matchPos >= 0;
             matchPos = next_offset_for_pos_[matchPos - prevBlockSize]) {
            const int offset = pos - matchPos;
            if (offset > max_offset_) break;

            bool exists = false;
            if (offset_cache_[offset & 2047] == pos) {
                for (int e = 0; e < m; ++e) {
                    if (static_cast<int>(match[e].offset) == offset ||
                        static_cast<int>(match[e].offset) - (depth[e] & 0x3fff) == offset) {
                        exists = true;
                        if (depth[e] == 0x4000) {
                            const int len = lenAt(matchPos);
                            if (len > static_cast<int>(match[e].length))
                                match[e].length = static_cast<uint32_t>(len);
                        }
                        break;
                    }
                }
            }
            if (exists) continue;

            int forwardPos = pos + 2 + 1;
            if (forwardPos < offset) continue;

            bool gotMatch = false;
            for (; forwardPos < maxForwardPos; ++forwardPos) {
                if (std::memcmp(window + forwardPos, window + forwardPos - offset, 2) == 0) {
                    gotMatch = true;
                    break;
                }
            }
            if (!gotMatch || !offsetEncodable(offset)) continue;

            match[m].length = static_cast<uint32_t>(lenAt(matchPos));
            match[m].offset = static_cast<uint32_t>(offset);
            depth[m] = 0;
            ++m;

            insertForwardMatch(window, pos, offset, prevBlockSize, endOffset, 8);

            if (++inserted >= 18 || (inserted >= 15 && m >= 38)) break;
        }
    }
}

// -------------------------------------------------------------- block driver

bool Compressor::shrinkBlock(std::span<const uint8_t> window, int prevBlockSize, int inDataSize,
                             BitWriter& writer, int& followsLiteral, int& repMatchOffset,
                             int blockFlags) {
    const int endOffset = prevBlockSize + inDataSize;
    const uint8_t* data = window.data();
    startOffset_ = prevBlockSize;

    buildSuffixArray(window.first(endOffset));
    if (prevBlockSize) skipMatches(0, prevBlockSize);
    findAllMatches(prevBlockSize, endOffset, blockFlags);
    findShortMatches(window, prevBlockSize, endOffset);

    std::fill(best_match_.begin(), best_match_.end(), FinalMatch{0, 0});

    // Run lengths, used to shortcut rep-match length computation.
    for (int i = 0; i < endOffset;) {
        const int runStart = i;
        const uint8_t c = data[i];
        do {
            ++i;
        } while (i < endOffset && data[i] == c);
        for (int j = runStart; j < i; ++j) rle_len_[j] = i - j;
    }

    if ((blockFlags & 3) == 3)
        supplementMatches(data, prevBlockSize, inDataSize, endOffset, blockFlags);

    optimizeForward(data, prevBlockSize, endOffset, /*insertForwardReps=*/true, repMatchOffset,
                    blockFlags);

    if ((blockFlags & 3) == 3 && arrivals_ == kArrivalsMax)
        supplementMatchesFurther(data, prevBlockSize, endOffset);

    optimizeForward(data, prevBlockSize, endOffset, /*insertForwardReps=*/false, repMatchOffset,
                    blockFlags);

    for (int pass = 0; pass < 20; ++pass)
        if (!reduceCommands(data, prevBlockSize, endOffset, repMatchOffset, blockFlags)) break;

    return writeBlock(data, prevBlockSize, endOffset, writer, followsLiteral, repMatchOffset,
                      blockFlags);
}

// ------------------------------------------------------------- public entry

std::vector<uint8_t> compress(std::span<const uint8_t> input, std::size_t maxWindowSize) {
    std::vector<uint8_t> out(maxCompressedSize(input.size()));
    if (input.empty()) {
        out.clear();
        return out;
    }

    const auto inputSize = static_cast<int>(input.size());
    const int blockSize = (inputSize < kBlockSize) ? std::max(1024, inputSize) : kBlockSize;

    // A single-block input gets the widest search; larger inputs trade it away.
    int maxArrivals = kArrivalsSmall;
    if (std::min(inputSize, blockSize) >= inputSize)
        maxArrivals = (inputSize <= 262144) ? kArrivalsMax : kArrivalsNormal;

    Compressor compressor(blockSize, blockSize * 2, maxArrivals,
                          maxWindowSize ? static_cast<int>(maxWindowSize) : kMaxOffset);

    BitWriter writer(out.data(), static_cast<int>(out.size()));
    int prevBlockSize = 0, followsLiteral = 0, repMatchOffset = 0, blockFlags = 1;
    int originalSize = 0;

    while (originalSize < inputSize) {
        int inDataSize = std::min(inputSize - originalSize, blockSize);
        if ((originalSize + inDataSize) >= inputSize) blockFlags |= 2;

        if (!compressor.shrinkBlock(input.subspan(originalSize - prevBlockSize), prevBlockSize,
                                    inDataSize, writer, followsLiteral, repMatchOffset, blockFlags))
            throw std::runtime_error("apultra: compression failed");

        blockFlags &= ~1;
        originalSize += inDataSize;
        prevBlockSize = inDataSize;
    }

    out.resize(static_cast<std::size_t>(writer.pos()));
    return out;
}

}  // namespace apultra

// ================================================== command line front end

#ifndef APULTRA_NO_MAIN

namespace {

std::vector<std::uint8_t> readFile(const char* path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error(std::string("cannot open ") + path);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    std::size_t window = 0;
    std::vector<std::string> files;

    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "-w" && i + 1 < args.size())
            window = std::strtoul(args[++i].c_str(), nullptr, 10);
        else if (args[i].starts_with("-w"))
            window = std::strtoul(args[i].c_str() + 2, nullptr, 10);
        else
            files.push_back(args[i]);
    }

    if (files.size() != 2) {
        std::fprintf(stderr, "usage: %s [-w window] <infile> <outfile>\n", argv[0]);
        return 1;
    }

    try {
        const auto input = readFile(files[0].c_str());
        const auto output = apultra::compress(input, window);

        std::ofstream out(files[1], std::ios::binary);
        if (!out) throw std::runtime_error("cannot create " + files[1]);
        out.write(reinterpret_cast<const char*>(output.data()),
                  static_cast<std::streamsize>(output.size()));
        if (!out) throw std::runtime_error("write failed");

        std::printf("%zu -> %zu bytes\n", input.size(), output.size());
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}

#endif  // APULTRA_NO_MAIN
