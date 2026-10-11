#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

/// A Microsoft compound file (OLE2, MS-CFB): the container of a Flash CS4 or
/// earlier .fla, as of Word's old .doc. Reads the whole file and gives its
/// streams by name. Free of Qt.
class CompoundFile
{
public:
    /// Whether \a data starts with the compound file signature.
    static bool isCompoundFile(const std::vector<uint8_t>& data);

    /// Reads \a data; false with errorString() set when it is not a
    /// compound file or is damaged.
    bool read(const std::vector<uint8_t>& data);

    /// Every stream, by its name. Streams inside storages are named with
    /// their storage's path ("Storage/Stream").
    const std::map<std::string, std::vector<uint8_t>>& streams() const { return _streams; }

    const std::string& errorString() const { return _error; }

private:
    std::map<std::string, std::vector<uint8_t>> _streams;
    std::string _error;
};
