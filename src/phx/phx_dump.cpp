#include "phx_document.h"

#include "container.h"
#include "wire.h"

#include <cstdio>
#include <sstream>
#include <algorithm>
#include <cstdint>

namespace phx {

namespace {

bool printable(const uint8_t* p, size_t size)
{
    for (size_t i = 0; i < size; ++i)
    {
        if (p[i] < 9 || (p[i] > 13 && p[i] < 32))
            return false;
    }
    return true;
}

/// Whether \a bytes read as a whole message, as protoc --decode_raw guesses.
bool looksLikeMessage(const uint8_t* p, size_t size)
{
    if (size == 0)
        return false;
    Reader r(p, size);
    int fields = 0;
    while (r.next())
        ++fields;
    return !r.failed() && fields > 0;
}

void dumpMessage(const uint8_t* p, size_t size, int depth, std::ostringstream& out)
{
    const std::string indent(static_cast<size_t>(depth) * 2, ' ');
    Reader r(p, size);
    while (r.next())
    {
        out << indent << r.field() << ": ";
        switch (r.type())
        {
        case WireType::Varint:
            out << r.uint();
            if (r.uint() & 1)
                out << " (zigzag " << r.sint() << ")";
            out << "\n";
            break;
        case WireType::Fixed64:
        {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%.17g (0x%016llx)", r.real(), static_cast<unsigned long long>(r.fixed64()));
            out << buffer << "\n";
            break;
        }
        case WireType::Fixed32:
        {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "0x%08x", r.fixed32());
            out << buffer << "\n";
            break;
        }
        case WireType::Bytes:
            if (printable(r.bytes(), r.size()) && r.size() > 0)
            {
                std::string text(reinterpret_cast<const char*>(r.bytes()), std::min<size_t>(r.size(), 200));
                for (char& c : text)
                {
                    if (c == '\n' || c == '\r')
                        c = ' ';
                }
                out << '"' << text << (r.size() > 200 ? "...\"" : "\"") << "\n";
            }
            else if (looksLikeMessage(r.bytes(), r.size()) && depth < 40)
            {
                out << "{\n";
                dumpMessage(r.bytes(), r.size(), depth + 1, out);
                out << indent << "}\n";
            }
            else
            {
                out << "<" << r.size() << " bytes>\n";
            }
            break;
        }
    }
    if (r.failed())
        out << indent << "<damaged from here>\n";
}

} // namespace

std::string dump(const std::string& path)
{
    std::ostringstream out;
    ContainerReader file;
    std::string error;
    if (!file.open(path, error))
        return error + "\n";
    out << "Phoenix document, format " << file.majorVersion() << "." << file.minorVersion()
        << (file.recovered() ? " (index rebuilt)" : "") << "\n";
    for (const ChunkInfo& chunk : file.chunks())
    {
        out << "\n[" << typeName(chunk.type) << "] id " << chunk.id << ", at " << chunk.offset << ", "
            << chunk.storedLength << " bytes stored, " << chunk.length << " bytes"
            << ((chunk.flags & kCritical) ? ", critical" : "")
            << ((chunk.flags & kCompressionMask) == kDeflate ? ", deflated" : "");
        if (chunk.mediaId)
            out << ", media " << chunk.mediaId;
        if (!chunk.symbolName.empty())
            out << ", symbol \"" << chunk.symbolName << "\"";
        out << "\n";
        std::string content;
        if (!file.content(chunk, content, error))
        {
            out << "  " << error << "\n";
            continue;
        }
        const uint8_t* p = reinterpret_cast<const uint8_t*>(content.data());
        if (chunk.type == kMedia)
        {
            // A length-prefixed header, then the bytes.
            uint64_t headerSize = 0;
            size_t at = 0;
            for (int shift = 0; at < content.size() && shift < 64; shift += 7)
            {
                const uint8_t byte = p[at++];
                headerSize |= static_cast<uint64_t>(byte & 0x7F) << shift;
                if (!(byte & 0x80))
                    break;
            }
            if (at + headerSize <= content.size())
            {
                dumpMessage(p + at, static_cast<size_t>(headerSize), 1, out);
                out << "  <" << (content.size() - at - headerSize) << " bytes of data>\n";
            }
            continue;
        }
        if (chunk.type == kThumbnail)
        {
            out << "  <PNG, " << content.size() << " bytes>\n";
            continue;
        }
        dumpMessage(p, content.size(), 1, out);
    }
    return out.str();
}

} // namespace phx
