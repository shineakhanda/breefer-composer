#include "io/Inflate.h"

#include <array>
#include <cstdint>

namespace bc::io {
namespace {

struct Huffman {
    std::array<uint16_t, 16> counts{};     // codes per length
    std::array<uint16_t, 320> symbols{};   // symbols ordered by code
};

class BitReader {
public:
    BitReader(const uint8_t* data, size_t size, size_t pos)
        : m_data(data), m_size(size), m_pos(pos) {}

    bool bits(int n, uint32_t& out)
    {
        while (m_count < n) {
            if (m_pos >= m_size) return false;
            m_buffer |= static_cast<uint32_t>(m_data[m_pos++]) << m_count;
            m_count += 8;
        }
        out = m_buffer & ((n == 32) ? 0xffffffffu : ((1u << n) - 1));
        m_buffer >>= n;
        m_count -= n;
        return true;
    }

    void alignToByte()
    {
        m_buffer = 0;
        m_count = 0;
    }

    size_t pos() const { return m_pos; }
    void setPos(size_t p) { m_pos = p; }
    const uint8_t* data() const { return m_data; }
    size_t size() const { return m_size; }

    // Canonical Huffman decode, one bit at a time (simple and fast enough).
    bool decode(const Huffman& h, int& symbol)
    {
        int code = 0;
        int first = 0;
        int index = 0;
        for (int len = 1; len < 16; ++len) {
            uint32_t bit;
            if (!bits(1, bit)) return false;
            code |= static_cast<int>(bit);
            const int count = h.counts[len];
            if (code - count < first) {
                symbol = h.symbols[index + (code - first)];
                return true;
            }
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        return false;
    }

private:
    const uint8_t* m_data;
    size_t m_size;
    size_t m_pos;
    uint32_t m_buffer = 0;
    int m_count = 0;
};

bool build(Huffman& h, const uint8_t* lengths, int n)
{
    h.counts.fill(0);
    for (int i = 0; i < n; ++i) h.counts[lengths[i]]++;
    h.counts[0] = 0;
    std::array<uint16_t, 16> offsets{};
    for (int len = 1; len < 16; ++len) offsets[len] = offsets[len - 1] + h.counts[len - 1];
    for (int i = 0; i < n; ++i)
        if (lengths[i]) h.symbols[offsets[lengths[i]]++] = static_cast<uint16_t>(i);
    return true;
}

const uint16_t kLengthBase[] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint16_t kLengthExtra[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistBase[] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                              257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
                              8193, 12289, 16385, 24577};
const uint16_t kDistExtra[] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                               7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool inflateBlock(BitReader& in, QByteArray& out, const Huffman& lit, const Huffman& dist)
{
    for (;;) {
        int sym;
        if (!in.decode(lit, sym)) return false;
        if (sym < 256) {
            out.append(static_cast<char>(sym));
            continue;
        }
        if (sym == 256) return true;
        sym -= 257;
        if (sym >= 29) return false;
        uint32_t extra;
        if (!in.bits(kLengthExtra[sym], extra)) return false;
        const int length = kLengthBase[sym] + static_cast<int>(extra);
        int dsym;
        if (!in.decode(dist, dsym) || dsym >= 30) return false;
        if (!in.bits(kDistExtra[dsym], extra)) return false;
        const int distance = kDistBase[dsym] + static_cast<int>(extra);
        if (distance > out.size()) return false;
        const qsizetype start = out.size() - distance;
        for (int i = 0; i < length; ++i) out.append(out.at(start + i));
    }
}

} // namespace

bool inflateRaw(const QByteArray& input, int offset, QByteArray& output, QString* error)
{
    auto fail = [&](const char* msg) {
        if (error) *error = QString::fromLatin1(msg);
        return false;
    };
    BitReader in(reinterpret_cast<const uint8_t*>(input.constData()), static_cast<size_t>(input.size()),
                 static_cast<size_t>(offset));
    output.clear();
    output.reserve(input.size() * 6);

    Huffman fixedLit, fixedDist;
    {
        uint8_t lengths[288];
        for (int i = 0; i < 144; ++i) lengths[i] = 8;
        for (int i = 144; i < 256; ++i) lengths[i] = 9;
        for (int i = 256; i < 280; ++i) lengths[i] = 7;
        for (int i = 280; i < 288; ++i) lengths[i] = 8;
        build(fixedLit, lengths, 288);
        uint8_t d[30];
        for (auto& v : d) v = 5;
        build(fixedDist, d, 30);
    }

    uint32_t last = 0;
    do {
        uint32_t type;
        if (!in.bits(1, last) || !in.bits(2, type)) return fail("Truncated DEFLATE stream");
        if (type == 0) {
            in.alignToByte();
            const size_t p = in.pos();
            if (p + 4 > in.size()) return fail("Truncated stored block");
            const uint8_t* d = in.data() + p;
            const uint16_t len = static_cast<uint16_t>(d[0] | (d[1] << 8));
            const uint16_t nlen = static_cast<uint16_t>(d[2] | (d[3] << 8));
            if (len != static_cast<uint16_t>(~nlen)) return fail("Corrupt stored block");
            if (p + 4 + len > in.size()) return fail("Truncated stored block");
            output.append(reinterpret_cast<const char*>(d + 4), len);
            in.setPos(p + 4 + len);
        } else if (type == 1) {
            if (!inflateBlock(in, output, fixedLit, fixedDist)) return fail("Corrupt fixed-Huffman block");
        } else if (type == 2) {
            uint32_t hlit, hdist, hclen;
            if (!in.bits(5, hlit) || !in.bits(5, hdist) || !in.bits(4, hclen)) return fail("Truncated header");
            hlit += 257;
            hdist += 1;
            hclen += 4;
            static const int order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            uint8_t codeLengths[19] = {0};
            for (uint32_t i = 0; i < hclen; ++i) {
                uint32_t v;
                if (!in.bits(3, v)) return fail("Truncated code lengths");
                codeLengths[order[i]] = static_cast<uint8_t>(v);
            }
            Huffman lenCode;
            build(lenCode, codeLengths, 19);
            uint8_t lengths[320] = {0};
            uint32_t n = 0;
            while (n < hlit + hdist) {
                int sym;
                if (!in.decode(lenCode, sym)) return fail("Corrupt code lengths");
                if (sym < 16) {
                    lengths[n++] = static_cast<uint8_t>(sym);
                    continue;
                }
                uint32_t repeat = 0;
                uint8_t value = 0;
                uint32_t extra;
                if (sym == 16) {
                    if (n == 0 || !in.bits(2, extra)) return fail("Bad repeat");
                    value = lengths[n - 1];
                    repeat = 3 + extra;
                } else if (sym == 17) {
                    if (!in.bits(3, extra)) return fail("Bad repeat");
                    repeat = 3 + extra;
                } else {
                    if (!in.bits(7, extra)) return fail("Bad repeat");
                    repeat = 11 + extra;
                }
                if (n + repeat > hlit + hdist) return fail("Too many code lengths");
                while (repeat--) lengths[n++] = value;
            }
            Huffman lit, dist;
            build(lit, lengths, static_cast<int>(hlit));
            build(dist, lengths + hlit, static_cast<int>(hdist));
            if (!inflateBlock(in, output, lit, dist)) return fail("Corrupt dynamic-Huffman block");
        } else {
            return fail("Invalid DEFLATE block type");
        }
    } while (!last);
    return true;
}

bool gunzip(const QByteArray& input, QByteArray& output, QString* error)
{
    const auto* d = reinterpret_cast<const uint8_t*>(input.constData());
    const int n = input.size();
    if (n < 18 || d[0] != 0x1f || d[1] != 0x8b || d[2] != 8) {
        if (error) *error = QStringLiteral("Not a gzip file");
        return false;
    }
    const uint8_t flags = d[3];
    int pos = 10;
    if (flags & 0x04) {   // FEXTRA
        if (pos + 2 > n) return false;
        pos += 2 + (d[pos] | (d[pos + 1] << 8));
    }
    if (flags & 0x08) while (pos < n && d[pos++]) {}   // FNAME
    if (flags & 0x10) while (pos < n && d[pos++]) {}   // FCOMMENT
    if (flags & 0x02) pos += 2;                          // FHCRC
    if (pos >= n) {
        if (error) *error = QStringLiteral("Truncated gzip header");
        return false;
    }
    return inflateRaw(input, pos, output, error);
}

} // namespace bc::io
