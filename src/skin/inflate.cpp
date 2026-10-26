// Small deflate decoder (same approach as puff.c from zlib): decodes RFC 1951 bit by bit.
// Speed isn't a goal: skin files are small and decode in a few milliseconds.
#include "skin/zip.h"

namespace {

class BitReader {
public:
    BitReader(const uint8_t* p, size_t n) : p_(p), n_(n) {}

    int Bits(int count) {  // low bit first; returns -1 past the end
        int v = 0;
        for (int i = 0; i < count; ++i) {
            if (pos_ >= n_ * 8) return -1;
            v |= ((p_[pos_ >> 3] >> (pos_ & 7)) & 1) << i;
            ++pos_;
        }
        return v;
    }
    void AlignByte() { pos_ = (pos_ + 7) & ~size_t(7); }
    size_t BytePos() const { return pos_ >> 3; }
    void SkipBytes(size_t n) { pos_ += n * 8; }
    bool HasBytes(size_t n) const { return BytePos() + n <= n_; }
    const uint8_t* Ptr() const { return p_ + BytePos(); }

private:
    const uint8_t* p_;
    size_t n_;
    size_t pos_ = 0;
};

// Canonical Huffman table: count[len] is the number of symbols per code length, symbol is sorted by code
struct Huffman {
    short count[16];
    short symbol[288];

    bool Build(const uint8_t* lengths, int n) {
        for (short& c : count) c = 0;
        for (int i = 0; i < n; ++i) count[lengths[i]]++;
        count[0] = 0;
        short offs[16];
        offs[1] = 0;
        for (int len = 1; len < 15; ++len) offs[len + 1] = offs[len] + count[len];
        for (int i = 0; i < n; ++i)
            if (lengths[i]) symbol[offs[lengths[i]]++] = (short)i;
        return true;
    }

    int Decode(BitReader& br) const {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; ++len) {
            int bit = br.Bits(1);
            if (bit < 0) return -1;
            code |= bit;
            int c = count[len];
            if (code - c < first) return symbol[index + (code - first)];
            index += c;
            first = (first + c) << 1;
            code <<= 1;
        }
        return -1;
    }
};

const short kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                            35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const short kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                             3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const short kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                             193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
                             6145, 8193, 12289, 16385, 24577};
const short kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                              6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

class Inflater {
public:
    Inflater(const uint8_t* in, size_t inLen, uint8_t* out, size_t outLen)
        : br_(in, inLen), out_(out), outLen_(outLen) {}

    bool Run() {
        for (;;) {
            int last = br_.Bits(1), type = br_.Bits(2);
            if (last < 0 || type < 0) return false;
            bool ok = type == 0 ? Stored() : type == 1 ? Fixed() : type == 2 ? Dynamic() : false;
            if (!ok) return false;
            if (last) return outPos_ == outLen_;
        }
    }

private:
    bool Stored() {
        br_.AlignByte();
        if (!br_.HasBytes(4)) return false;
        const uint8_t* p = br_.Ptr();
        unsigned len = p[0] | (p[1] << 8);
        br_.SkipBytes(4);
        if (!br_.HasBytes(len) || outPos_ + len > outLen_) return false;
        memcpy(out_ + outPos_, br_.Ptr(), len);
        br_.SkipBytes(len);
        outPos_ += len;
        return true;
    }

    bool Fixed() {
        uint8_t lengths[320];
        int i = 0;
        for (; i < 144; ++i) lengths[i] = 8;
        for (; i < 256; ++i) lengths[i] = 9;
        for (; i < 280; ++i) lengths[i] = 7;
        for (; i < 288; ++i) lengths[i] = 8;
        Huffman lit, dist;
        lit.Build(lengths, 288);
        for (i = 0; i < 30; ++i) lengths[i] = 5;
        dist.Build(lengths, 30);
        return Codes(lit, dist);
    }

    bool Dynamic() {
        static const uint8_t kOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        int nlen = br_.Bits(5) + 257, ndist = br_.Bits(5) + 1, ncode = br_.Bits(4) + 4;
        if (nlen > 286 || ndist > 30 || ncode < 4) return false;

        uint8_t lengths[320] = {};
        for (int i = 0; i < ncode; ++i) {
            int v = br_.Bits(3);
            if (v < 0) return false;
            lengths[kOrder[i]] = (uint8_t)v;
        }
        Huffman codeLen;
        codeLen.Build(lengths, 19);

        // the code lengths are Huffman coded too; 16/17/18 mean repeats
        int index = 0;
        while (index < nlen + ndist) {
            int sym = codeLen.Decode(br_);
            if (sym < 0) return false;
            if (sym < 16) {
                lengths[index++] = (uint8_t)sym;
                continue;
            }
            uint8_t value = 0;
            int repeat;
            if (sym == 16) {
                if (index == 0) return false;
                value = lengths[index - 1];
                repeat = 3 + br_.Bits(2);
            } else if (sym == 17) {
                repeat = 3 + br_.Bits(3);
            } else {
                repeat = 11 + br_.Bits(7);
            }
            if (index + repeat > nlen + ndist) return false;
            while (repeat--) lengths[index++] = value;
        }

        Huffman lit, dist;
        lit.Build(lengths, nlen);
        dist.Build(lengths + nlen, ndist);
        return Codes(lit, dist);
    }

    bool Codes(const Huffman& lit, const Huffman& dist) {
        for (;;) {
            int sym = lit.Decode(br_);
            if (sym < 0) return false;
            if (sym < 256) {
                if (outPos_ >= outLen_) return false;
                out_[outPos_++] = (uint8_t)sym;
            } else if (sym == 256) {
                return true;
            } else {
                sym -= 257;
                if (sym >= 29) return false;
                int len = kLenBase[sym] + br_.Bits(kLenExtra[sym]);
                int dsym = dist.Decode(br_);
                if (dsym < 0 || dsym >= 30) return false;
                size_t d = kDistBase[dsym] + br_.Bits(kDistExtra[dsym]);
                if (d > outPos_ || outPos_ + len > outLen_) return false;
                for (int i = 0; i < len; ++i, ++outPos_) out_[outPos_] = out_[outPos_ - d];
            }
        }
    }

    BitReader br_;
    uint8_t* out_;
    size_t outLen_;
    size_t outPos_ = 0;
};

}  // namespace

bool Inflate(const uint8_t* in, size_t inLen, uint8_t* out, size_t outLen) {
    return Inflater(in, inLen, out, outLen).Run();
}
