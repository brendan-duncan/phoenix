#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fla {

/// PDF files read as their objects: the cross-reference table or stream
/// (with the tables of earlier saves it points back to), objects packed in
/// object streams, and stream data through its filters (Flate with its
/// predictors, LZW, ASCII hex and base-85, run length). A file whose table
/// is missing or damaged is read by finding its objects where they lie.
/// What Illustrator files are read through: their PDF page, or the private
/// data Illustrator keeps beside it. Free of Qt.
namespace pdf {

struct Object
{
    enum class Type
    {
        Null,
        Boolean,
        Number,
        String,
        Name,
        Array,
        Dictionary,
        Reference,
        Stream
    };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    /// A string's bytes, or a name without its slash.
    std::string text;
    std::vector<Object> items;
    /// A dictionary's (or a stream's) entries, keys without their slash.
    std::vector<std::pair<std::string, Object>> entries;
    int reference = 0;
    int generation = 0;
    /// A stream's data as stored, before its filters.
    std::vector<uint8_t> raw;

    bool is(Type t) const { return type == t; }
    bool isDictionary() const { return type == Type::Dictionary || type == Type::Stream; }
    /// The entry \a key of a dictionary or stream; null when it has none.
    const Object* get(const std::string& key) const;
    void set(const std::string& key, Object value);
    void remove(const std::string& key);
};

class File
{
public:
    /// Reads \a bytes; false with \a error set when they are not a PDF
    /// file Phoenix can read (encrypted, for one).
    bool open(std::vector<uint8_t> bytes, std::string& error);

    /// Object \a number; null when the file has none.
    const Object* object(int number);
    /// \a value, followed through references to what they name.
    const Object& resolve(const Object& value);
    /// A value's number, after resolving; \a fallback for anything else.
    double number(const Object* value, double fallback = 0.0);

    /// A stream's data through its filters. Image codecs (DCT, JPX, CCITT,
    /// JBIG2) are left on: \a remaining gets the filter still to apply,
    /// empty when none.
    std::vector<uint8_t> decoded(const Object& stream, std::string* remaining = nullptr);

    /// The pages, in order, each with what it inherits from the page tree
    /// (Resources, MediaBox, CropBox, Rotate) set on it.
    std::vector<Object> pages();

    const Object& trailer() const { return _trailer; }

    /// An object as PDF writes it, a token apart, references as "N G R";
    /// a stream as its dictionary, its /Filter what \a remainingFilter says.
    static std::string text(const Object& object, const std::string* remainingFilter = nullptr);

private:
    struct Entry
    {
        int type = 0; // 1 at an offset, 2 in an object stream
        size_t offset = 0;
        int stream = 0;
        int index = 0;
    };
    bool readTables(std::string& error);
    bool readTable(size_t offset, int depth);
    void rebuild();
    bool parseAt(size_t offset, Object& out);
    bool fromObjectStream(int stream, int index, int number, Object& out);

    std::vector<uint8_t> _bytes;
    std::map<int, Entry> _entries;
    std::map<int, std::unique_ptr<Object>> _cache;
    std::map<int, bool> _loading;
    std::map<int, std::vector<std::pair<int, size_t>>> _streamTables;
    std::map<int, std::vector<uint8_t>> _streamData;
    Object _trailer;
    Object _null;
};

} // namespace pdf

} // namespace fla
