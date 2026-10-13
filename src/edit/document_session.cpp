#include "document_session.h"

#include <algorithm>

namespace fla {

namespace session {

std::vector<std::string> reopenList(const std::vector<std::vector<OpenDocument>>& windows)
{
    std::vector<std::string> list;
    std::string last;
    const auto add = [&list](const std::string& path) {
        if (!path.empty() && std::find(list.begin(), list.end(), path) == list.end())
            list.push_back(path);
    };
    for (size_t w = 0; w < windows.size(); ++w)
    {
        for (const OpenDocument& document : windows[w])
        {
            if (w == 0 && document.active)
                last = document.path;
            else
                add(document.path);
        }
    }
    if (!last.empty())
    {
        list.erase(std::remove(list.begin(), list.end(), last), list.end());
        list.push_back(last);
    }
    return list;
}

} // namespace session

} // namespace fla
