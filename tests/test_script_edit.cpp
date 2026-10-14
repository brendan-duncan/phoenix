#include "test_util.h"

#include "../src/data/action_script.h"
#include "../src/data/frame.h"
#include "../src/data/symbol_instance.h"
#include "../src/edit/command_stack.h"
#include "../src/edit/script_edit.h"

#include <memory>

TEST(frame_script_typing_merges_into_one_undo_step)
{
    fla::Frame frame(nullptr);
    fla::CommandStack stack;
    // Each pause in typing is a command; one session is one step.
    stack.push(std::make_unique<fla::scripts::SetFrameScriptCommand>(&frame, "st", 1));
    stack.push(std::make_unique<fla::scripts::SetFrameScriptCommand>(&frame, "stop();", 1));
    CHECK(frame.actionScript && frame.actionScript->code == "stop();");
    // Another session is another step.
    stack.push(std::make_unique<fla::scripts::SetFrameScriptCommand>(&frame, "stop();\nplay();", 2));
    stack.undo();
    CHECK(frame.actionScript && frame.actionScript->code == "stop();");
    stack.undo();
    CHECK(frame.actionScript == nullptr);
    stack.redo();
    CHECK(frame.actionScript && frame.actionScript->code == "stop();");
    // Emptied, the script goes.
    stack.push(std::make_unique<fla::scripts::SetFrameScriptCommand>(&frame, "", 3));
    CHECK(frame.actionScript == nullptr);
    stack.undo();
    CHECK(frame.actionScript && frame.actionScript->code == "stop();");
}

TEST(instance_script_is_set_kept_with_other_children_and_taken_off)
{
    fla::SymbolInstance instance(nullptr);
    instance.extraChildren = {"<filters><GlowFilter/></filters>"};
    CHECK(fla::scripts::instanceScript(instance).empty());

    fla::CommandStack stack;
    const std::string code = "on (release) {\n    if (a < b && c) gotoAndPlay(2);\n}";
    stack.push(std::make_unique<fla::scripts::SetInstanceScriptCommand>(&instance, code));
    CHECK(instance.extraChildren.size() == 2 && instance.extraChildren[0] == "<filters><GlowFilter/></filters>");
    CHECK(fla::scripts::instanceScript(instance) == code);

    stack.push(std::make_unique<fla::scripts::SetInstanceScriptCommand>(&instance, "on (press) {}"));
    CHECK(instance.extraChildren.size() == 2 && fla::scripts::instanceScript(instance) == "on (press) {}");

    stack.push(std::make_unique<fla::scripts::SetInstanceScriptCommand>(&instance, ""));
    CHECK(instance.extraChildren.size() == 1 && fla::scripts::instanceScript(instance).empty());
    stack.undo();
    stack.undo();
    CHECK(fla::scripts::instanceScript(instance) == code);
}
