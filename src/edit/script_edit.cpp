#include "script_edit.h"

#include "../data/action_script.h"
#include "../data/element.h"
#include "../data/frame.h"
#include "../third_party/tinyxml2/tinyxml2.h"

namespace fla {

namespace scripts {

namespace {

bool isScriptChild(const std::string& child)
{
    return child.find("<Actionscript") != std::string::npos;
}

} // namespace

std::string instanceScript(const Element& element)
{
    std::string script;
    for (const std::string& child : element.extraChildren)
    {
        if (!isScriptChild(child))
            continue;
        tinyxml2::XMLDocument xml;
        if (xml.Parse(child.c_str(), child.size()) != tinyxml2::XML_SUCCESS)
            continue;
        const tinyxml2::XMLElement* root = xml.FirstChildElement();
        for (const tinyxml2::XMLElement* part = root ? root->FirstChildElement("script") : nullptr; part;
             part = part->NextSiblingElement("script"))
        {
            if (const char* text = part->GetText())
            {
                if (!script.empty() && script.back() != '\n')
                    script += "\n";
                script += text;
            }
        }
    }
    return script;
}

std::vector<std::string> withInstanceScript(const std::vector<std::string>& extraChildren, const std::string& code)
{
    std::string child;
    if (!code.empty())
    {
        tinyxml2::XMLDocument xml;
        tinyxml2::XMLElement* root = xml.NewElement("Actionscript");
        tinyxml2::XMLElement* script = xml.NewElement("script");
        script->SetText(code.c_str());
        root->InsertEndChild(script);
        xml.InsertEndChild(root);
        tinyxml2::XMLPrinter printer(nullptr, true);
        xml.Print(&printer);
        child = printer.CStr();
    }
    std::vector<std::string> out;
    bool placed = false;
    for (const std::string& existing : extraChildren)
    {
        if (!isScriptChild(existing))
        {
            out.push_back(existing);
            continue;
        }
        // The new script where the old one was; any further ones go.
        if (!placed && !child.empty())
            out.push_back(child);
        placed = true;
    }
    if (!placed && !child.empty())
        out.push_back(child);
    return out;
}

SetFrameScriptCommand::SetFrameScriptCommand(Frame* frame, const std::string& code, int session)
    : _frame(frame)
    , _before(frame && frame->actionScript ? frame->actionScript->code : std::string())
    , _after(code)
    , _session(session)
{}

void SetFrameScriptCommand::apply(const std::string& code)
{
    if (code.empty())
    {
        delete _frame->actionScript;
        _frame->actionScript = nullptr;
        return;
    }
    if (!_frame->actionScript)
        _frame->actionScript = new ActionScript(_frame);
    _frame->actionScript->code = code;
}

void SetFrameScriptCommand::redo()
{
    apply(_after);
}

void SetFrameScriptCommand::undo()
{
    apply(_before);
}

bool SetFrameScriptCommand::mergeWith(const Command* other)
{
    const auto* next = dynamic_cast<const SetFrameScriptCommand*>(other);
    if (!next || next->_frame != _frame || next->_session != _session)
        return false;
    _after = next->_after;
    return true;
}

SetInstanceScriptCommand::SetInstanceScriptCommand(Element* element, const std::string& code, int session)
    : _element(element)
    , _before(element->extraChildren)
    , _after(withInstanceScript(element->extraChildren, code))
    , _session(session)
{}

void SetInstanceScriptCommand::redo()
{
    _element->extraChildren = _after;
}

void SetInstanceScriptCommand::undo()
{
    _element->extraChildren = _before;
}

bool SetInstanceScriptCommand::mergeWith(const Command* other)
{
    const auto* next = dynamic_cast<const SetInstanceScriptCommand*>(other);
    if (!next || next->_element != _element || next->_session != _session)
        return false;
    _after = next->_after;
    return true;
}

} // namespace scripts

} // namespace fla
