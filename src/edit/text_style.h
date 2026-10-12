#pragma once

#include "command.h"

#include <functional>
#include <string>
#include <vector>

namespace fla {

class StaticText;
struct TextRun;

/// Text settings by name, so the Properties panel, the text tool and the
/// Selection tool share one way to read and change them. A key names a
/// character setting (on the characters it covers), a paragraph setting
/// (on whole paragraphs) or a field setting (on the text field itself).
/// Values are strings, as the file writes them. Free of Qt.
///
/// Character: face, size, fillColor (#rrggbbaa), bold, italic,
/// letterSpacing, autoKern, characterPosition (normal, superscript,
/// subscript), url, target.
/// Paragraph: alignment (left, center, right, justify), lineSpacing,
/// indent, leftMargin, rightMargin.
/// Field: fontRenderingMode, instanceName, lineType, isSelectable,
/// renderAsHTML, border, variableName, maxCharacters, orientation,
/// scrollable, textType (static, dynamic, input).
namespace textstyle {

enum class Scope
{
    Character,
    Paragraph,
    Field
};

Scope scopeOf(const std::string& key);

/// A run's or a field's setting, as a string ("" when not set).
std::string get(const TextRun& run, const std::string& key);
std::string get(const StaticText& text, const std::string& key);

void set(TextRun& run, const std::string& key, const std::string& value);
void set(StaticText& text, const std::string& key, const std::string& value);

/// The text with \a key set everywhere it applies: every run for a
/// character or paragraph setting, the field for a field setting.
void setAll(StaticText& text, const std::string& key, const std::string& value);

/// Restyles whole text fields, as one undo step; \a measure fits each
/// box to its restyled text.
CommandPtr restyle(const std::vector<StaticText*>& texts, const std::string& key, const std::string& value,
    const std::string& name, const std::function<void(StaticText&)>& measure = {});

} // namespace textstyle

} // namespace fla
