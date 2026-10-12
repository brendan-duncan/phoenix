#include "test_util.h"

#include "../src/data/static_text.h"
#include "../src/edit/command_stack.h"
#include "../src/edit/text_style.h"

TEST(text_settings_by_name_on_runs_paragraphs_and_fields)
{
    using fla::textstyle::Scope;
    CHECK(fla::textstyle::scopeOf("bold") == Scope::Character);
    CHECK(fla::textstyle::scopeOf("indent") == Scope::Paragraph);
    CHECK(fla::textstyle::scopeOf("maxCharacters") == Scope::Field);

    fla::StaticText text(nullptr);
    text.runs.resize(2);
    text.runs[0].text = "Hello ";
    text.runs[0].size = 12.0;
    text.runs[1].text = "world";
    text.runs[1].size = 20.0;

    fla::TextRun& run = text.runs[0];
    fla::textstyle::set(run, "fillColor", "#ff800040");
    CHECK(run.fillColor[0] == 255 && run.fillColor[1] == 128 && run.fillColor[3] == 64);
    CHECK(fla::textstyle::get(run, "fillColor") == "#ff800040");
    fla::textstyle::set(run, "alignment", "justify");
    CHECK(run.alignment == fla::TextRun::Alignment::Justify && fla::textstyle::get(run, "alignment") == "justify");
    fla::textstyle::set(run, "bold", "true");
    CHECK(fla::textstyle::get(run, "bold") == "true");
    fla::textstyle::set(run, "bold", "false");
    CHECK(run.extraAttributes.empty());

    // A field is selectable unless it says not.
    fla::textstyle::set(text, "isSelectable", "false");
    CHECK(fla::textstyle::get(text, "isSelectable") == "false");
    fla::textstyle::set(text, "isSelectable", "true");
    CHECK(text.extraAttributes.empty());
    fla::textstyle::set(text, "textType", "input");
    CHECK(text.textType == fla::StaticText::TextType::Input);

    // Whole fields restyled undoably, measured.
    fla::CommandStack stack;
    stack.push(fla::textstyle::restyle({&text}, "size", "30", "Size",
        [](fla::StaticText& measured) { measured.height = measured.runs[0].size * 2; }));
    CHECK(text.runs[0].size == 30.0 && text.runs[1].size == 30.0 && text.height == 60.0);
    stack.undo();
    CHECK(text.runs[0].size == 12.0 && text.runs[1].size == 20.0 && text.height == 0.0);
}
