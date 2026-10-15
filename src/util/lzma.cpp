#include "lzma.h"

#include <algorithm>

namespace lzma {

namespace {

constexpr int kProbBits = 11;
constexpr uint16_t kProbInit = 1 << (kProbBits - 1);
constexpr int kStates = 12;
constexpr int kPosBitsMax = 4;
constexpr int kMatchMinLength = 2;
constexpr int kEndPosModelIndex = 14;
constexpr int kFullDistances = 1 << (kEndPosModelIndex >> 1);
constexpr int kAlignBits = 4;
constexpr int kLenStates = 4;

struct Damaged
{
};

class RangeDecoder
{
public:
    RangeDecoder(const uint8_t* data, size_t length)
        : _data(data)
        , _length(length)
    {
        if (next() != 0)
            throw Damaged();
        for (int i = 0; i < 4; ++i)
            _code = (_code << 8) | next();
        if (_code == _range)
            throw Damaged();
    }

    bool finishedOk() const { return _code == 0; }

    uint32_t direct(int bits)
    {
        uint32_t result = 0;
        while (bits-- > 0)
        {
            _range >>= 1;
            _code -= _range;
            const uint32_t t = 0u - (_code >> 31);
            _code += _range & t;
            if (_code == _range)
                throw Damaged();
            normalize();
            result = (result << 1) + (t + 1);
        }
        return result;
    }

    int bit(uint16_t& prob)
    {
        const uint32_t bound = (_range >> kProbBits) * prob;
        int symbol;
        if (_code < bound)
        {
            prob = static_cast<uint16_t>(prob + (((1 << kProbBits) - prob) >> 5));
            _range = bound;
            symbol = 0;
        }
        else
        {
            prob = static_cast<uint16_t>(prob - (prob >> 5));
            _code -= bound;
            _range -= bound;
            symbol = 1;
        }
        normalize();
        return symbol;
    }

private:
    uint8_t next()
    {
        // Past the end the stream reads as zeros, as the reference does; a
        // decoder that needs them has run off damaged data, which the size
        // check catches.
        return _at < _length ? _data[_at++] : (++_over, 0);
    }

    void normalize()
    {
        if (_range < (1u << 24))
        {
            _range <<= 8;
            _code = (_code << 8) | next();
            if (_over > 8)
                throw Damaged();
        }
    }

    const uint8_t* _data;
    size_t _length;
    size_t _at = 0;
    size_t _over = 0;
    uint32_t _range = 0xFFFFFFFFu;
    uint32_t _code = 0;
};

int bitTree(RangeDecoder& rc, uint16_t* probs, int bits)
{
    int m = 1;
    for (int i = 0; i < bits; ++i)
        m = (m << 1) + rc.bit(probs[m]);
    return m - (1 << bits);
}

int reverseBitTree(RangeDecoder& rc, uint16_t* probs, int bits)
{
    int m = 1;
    int symbol = 0;
    for (int i = 0; i < bits; ++i)
    {
        const int b = rc.bit(probs[m]);
        m = (m << 1) + b;
        symbol |= b << i;
    }
    return symbol;
}

struct LengthDecoder
{
    uint16_t choice = kProbInit;
    uint16_t choice2 = kProbInit;
    uint16_t low[1 << kPosBitsMax][1 << 3];
    uint16_t mid[1 << kPosBitsMax][1 << 3];
    uint16_t high[1 << 8];

    LengthDecoder()
    {
        std::fill(&low[0][0], &low[0][0] + sizeof(low) / sizeof(uint16_t), kProbInit);
        std::fill(&mid[0][0], &mid[0][0] + sizeof(mid) / sizeof(uint16_t), kProbInit);
        std::fill(high, high + (1 << 8), kProbInit);
    }

    int decode(RangeDecoder& rc, int posState)
    {
        if (rc.bit(choice) == 0)
            return bitTree(rc, low[posState], 3);
        if (rc.bit(choice2) == 0)
            return 8 + bitTree(rc, mid[posState], 3);
        return 16 + bitTree(rc, high, 8);
    }
};

} // namespace

bool decode(const uint8_t properties[5], const uint8_t* data, size_t length, size_t size, std::vector<uint8_t>& out)
{
    int d = properties[0];
    if (d >= 9 * 5 * 5)
        return false;
    const int lc = d % 9;
    d /= 9;
    const int lp = d % 5;
    const int pb = d / 5;
    uint32_t dictionary = static_cast<uint32_t>(properties[1]) | (static_cast<uint32_t>(properties[2]) << 8) |
        (static_cast<uint32_t>(properties[3]) << 16) | (static_cast<uint32_t>(properties[4]) << 24);
    dictionary = std::max<uint32_t>(dictionary, 1u << 12);
    out.clear();
    out.reserve(size);
    try
    {
        RangeDecoder rc(data, length);
        std::vector<uint16_t> literals(static_cast<size_t>(0x300) << (lc + lp), kProbInit);
        uint16_t isMatch[kStates << kPosBitsMax];
        uint16_t isRep[kStates], isRepG0[kStates], isRepG1[kStates], isRepG2[kStates];
        uint16_t isRep0Long[kStates << kPosBitsMax];
        uint16_t posSlot[kLenStates][1 << 6];
        uint16_t posDecoders[1 + kFullDistances - kEndPosModelIndex];
        uint16_t align[1 << kAlignBits];
        for (uint16_t* table : {isMatch, isRep0Long})
            std::fill(table, table + (kStates << kPosBitsMax), kProbInit);
        for (uint16_t* table : {isRep, isRepG0, isRepG1, isRepG2})
            std::fill(table, table + kStates, kProbInit);
        std::fill(&posSlot[0][0], &posSlot[0][0] + kLenStates * (1 << 6), kProbInit);
        std::fill(posDecoders, posDecoders + sizeof(posDecoders) / sizeof(uint16_t), kProbInit);
        std::fill(align, align + (1 << kAlignBits), kProbInit);
        LengthDecoder lengths;
        LengthDecoder repLengths;

        uint32_t rep0 = 0, rep1 = 0, rep2 = 0, rep3 = 0;
        int state = 0;
        const uint32_t pbMask = (1u << pb) - 1;
        const uint32_t lpMask = (1u << lp) - 1;
        while (out.size() < size)
        {
            const uint32_t position = static_cast<uint32_t>(out.size());
            const int posState = static_cast<int>(position & pbMask);
            if (rc.bit(isMatch[(state << kPosBitsMax) + posState]) == 0)
            {
                // A literal, coded against the byte before it and, after a
                // match, the byte at rep0.
                const unsigned previous = out.empty() ? 0 : out.back();
                uint16_t* probs =
                    &literals[static_cast<size_t>(0x300) * (((position & lpMask) << lc) + (previous >> (8 - lc)))];
                unsigned symbol = 1;
                if (state >= 7)
                {
                    if (rep0 >= out.size())
                        return false;
                    unsigned matchByte = out[out.size() - rep0 - 1];
                    do
                    {
                        const unsigned matchBit = (matchByte >> 7) & 1;
                        matchByte <<= 1;
                        const int b = rc.bit(probs[((1 + matchBit) << 8) + symbol]);
                        symbol = (symbol << 1) | static_cast<unsigned>(b);
                        if (matchBit != static_cast<unsigned>(b))
                            break;
                    } while (symbol < 0x100);
                }
                while (symbol < 0x100)
                    symbol = (symbol << 1) | static_cast<unsigned>(rc.bit(probs[symbol]));
                out.push_back(static_cast<uint8_t>(symbol - 0x100));
                state = state < 4 ? 0 : state < 10 ? state - 3 : state - 6;
                continue;
            }
            int len;
            if (rc.bit(isRep[state]) != 0)
            {
                if (out.empty())
                    return false;
                if (rc.bit(isRepG0[state]) == 0)
                {
                    if (rc.bit(isRep0Long[(state << kPosBitsMax) + posState]) == 0)
                    {
                        // A single byte from rep0.
                        state = state < 7 ? 9 : 11;
                        if (rep0 >= out.size())
                            return false;
                        out.push_back(out[out.size() - rep0 - 1]);
                        continue;
                    }
                }
                else
                {
                    uint32_t distance;
                    if (rc.bit(isRepG1[state]) == 0)
                    {
                        distance = rep1;
                    }
                    else
                    {
                        if (rc.bit(isRepG2[state]) == 0)
                        {
                            distance = rep2;
                        }
                        else
                        {
                            distance = rep3;
                            rep3 = rep2;
                        }
                        rep2 = rep1;
                    }
                    rep1 = rep0;
                    rep0 = distance;
                }
                len = repLengths.decode(rc, posState);
                state = state < 7 ? 8 : 11;
            }
            else
            {
                rep3 = rep2;
                rep2 = rep1;
                rep1 = rep0;
                len = lengths.decode(rc, posState);
                state = state < 7 ? 7 : 10;
                // The distance: a slot, then bits under a tree or direct.
                const int lenState = std::min(len, kLenStates - 1);
                const int slot = bitTree(rc, posSlot[lenState], 6);
                if (slot < 4)
                {
                    rep0 = static_cast<uint32_t>(slot);
                }
                else
                {
                    const int bits = (slot >> 1) - 1;
                    uint32_t distance = (2u | (static_cast<uint32_t>(slot) & 1u)) << bits;
                    if (slot < kEndPosModelIndex)
                    {
                        distance += static_cast<uint32_t>(
                            reverseBitTree(rc, posDecoders + distance - static_cast<uint32_t>(slot), bits));
                    }
                    else
                    {
                        distance += rc.direct(bits - kAlignBits) << kAlignBits;
                        distance += static_cast<uint32_t>(reverseBitTree(rc, align, kAlignBits));
                    }
                    rep0 = distance;
                }
                if (rep0 == 0xFFFFFFFFu)
                    return rc.finishedOk() && out.size() == size; // the end marker
                if (rep0 >= out.size() || rep0 >= dictionary)
                    return false;
            }
            len += kMatchMinLength;
            const size_t from = out.size() - rep0 - 1;
            for (int i = 0; i < len && out.size() < size; ++i)
                out.push_back(out[from + static_cast<size_t>(i)]);
        }
        return true;
    }
    catch (const Damaged&)
    {
        return false;
    }
}

} // namespace lzma
