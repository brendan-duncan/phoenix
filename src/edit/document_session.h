#pragma once

#include <string>
#include <vector>

namespace fla {

/// The documents open when Phoenix quits, to open again on the next start
/// (Preferences > Reopen documents from last time). Free of Qt.
namespace session {

/// A document in a tab: its file (empty when it has never been saved) and
/// whether it is the tab shown in its window.
struct OpenDocument
{
    std::string path;
    bool active = false;
};

/// The files to open again, in order: the tabs of each window in turn,
/// every file once, those never saved left out, and the document shown in
/// the first window last, so it is the one shown again.
std::vector<std::string> reopenList(const std::vector<std::vector<OpenDocument>>& windows);

} // namespace session

} // namespace fla
