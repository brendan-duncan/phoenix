#include "session.h"

#include "../data/document.h"
#include "../data/fla_document.h"
#include "../edit/command_stack.h"

#include <algorithm>

namespace jsfl {

Session::Session(Host& host, std::string undoName)
    : _host(host)
    , _undoName(std::move(undoName))
{
}

Session::~Session()
{
    finish();
}

void Session::finish()
{
    if (_finished)
        return;
    _finished = true;
    closeSteps();
    _host.refresh();
}

bool Session::isOpen(const fla::FLADocument* document) const
{
    if (!document)
        return false;
    const std::vector<fla::FLADocument*> open = _host.documents();
    return std::find(open.begin(), open.end(), document) != open.end() && document->document;
}

fla::Document& Session::data(fla::FLADocument* document) const
{
    if (!isOpen(document))
        throw Error("This document has been closed.");
    return *document->document;
}

void Session::beforeSwitch()
{
    closeSteps();
}

void Session::forget(const fla::FLADocument* document)
{
    _documents.erase(document);
}

void Session::closeSteps()
{
    for (auto& [document, state] : _documents)
    {
        if (!state.stepOpen)
            continue;
        state.stepOpen = false;
        auto* open = const_cast<fla::FLADocument*>(document);
        if (!isOpen(open))
            continue;
        fla::CommandStack& history = _host.history(open);
        if (history.isInMacro())
        {
            history.endMacro();
            history.breakMergeChain();
        }
    }
}

Session::DocumentState& Session::state(fla::FLADocument* document)
{
    return _documents[document];
}

void Session::checkWritable(fla::FLADocument* document)
{
    data(document);
    if (_host.readOnly(document))
        throw Error("This document is read-only: it came from a newer Phoenix. Save a copy to change it.");
}

void Session::push(fla::FLADocument* document, fla::CommandPtr command, bool structural)
{
    checkWritable(document);
    DocumentState& here = state(document);
    fla::CommandStack& history = _host.history(document);
    if (!here.stepOpen)
    {
        history.beginMacro(_undoName);
        here.stepOpen = true;
    }
    history.push(std::move(command));
    if (structural)
        here.reachableValid = false;
    _host.changed(document);
}

bool Session::alive(fla::FLADocument* document, const fla::DOMElement* object)
{
    if (!object || !isOpen(document))
        return false;
    DocumentState& here = state(document);
    if (!here.reachableValid)
    {
        here.reachable = document->document->reachable();
        here.reachableValid = true;
    }
    return here.reachable.count(object) > 0;
}

void Session::invalidate(fla::FLADocument* document)
{
    state(document).reachableValid = false;
    _host.changed(document);
}

std::vector<std::string>& Session::librarySelection(fla::FLADocument* document)
{
    return state(document).librarySelection;
}

std::vector<FrameRange>& Session::frameSelection(fla::FLADocument* document, const fla::Timeline* timeline)
{
    DocumentState& here = state(document);
    auto found = here.frames.find(timeline);
    if (found == here.frames.end())
    {
        std::vector<FrameRange> initial;
        if (timeline == _host.editedTimeline(document))
            initial = _host.selectedFrames(document);
        found = here.frames.emplace(timeline, initial).first;
    }
    return found->second;
}

std::vector<int>& Session::layerSelection(fla::FLADocument* document, const fla::Timeline* timeline)
{
    DocumentState& here = state(document);
    auto found = here.layers.find(timeline);
    if (found == here.layers.end())
    {
        std::vector<int> initial;
        if (timeline == _host.editedTimeline(document))
            initial.push_back(_host.currentLayer(document));
        found = here.layers.emplace(timeline, initial).first;
    }
    return found->second;
}

} // namespace jsfl
