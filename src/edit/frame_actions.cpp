#include "frame_actions.h"

#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/timeline.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace fla {

namespace frameactions {

namespace {

struct Token
{
    enum class Kind
    {
        Identifier,
        Number,
        String,
        Punct,
    };
    Kind kind = Kind::Punct;
    std::string text;
    /// A line break (or a comment holding one) comes before it: ActionScript
    /// can end a statement there without a semicolon.
    bool newlineBefore = false;
};

bool identifierStart(char c)
{
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$';
}

bool identifierPart(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$';
}

/// The script as tokens: comments dropped, strings with their quotes and
/// escapes taken off. Anything it does not know is a one-character punct.
std::vector<Token> tokenize(const std::string& script)
{
    std::vector<Token> tokens;
    bool newline = false;
    size_t i = 0;
    const size_t n = script.size();
    while (i < n)
    {
        const char c = script[i];
        if (c == '\n' || c == '\r')
        {
            newline = true;
            ++i;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c)))
        {
            ++i;
            continue;
        }
        if (c == '/' && i + 1 < n && script[i + 1] == '/')
        {
            while (i < n && script[i] != '\n' && script[i] != '\r')
                ++i;
            continue;
        }
        if (c == '/' && i + 1 < n && script[i + 1] == '*')
        {
            const size_t end = script.find("*/", i + 2);
            const size_t stop = end == std::string::npos ? n : end + 2;
            if (script.find_first_of("\r\n", i) < stop)
                newline = true;
            i = stop;
            continue;
        }
        Token token;
        token.newlineBefore = newline;
        newline = false;
        if (c == '"' || c == '\'')
        {
            token.kind = Token::Kind::String;
            ++i;
            while (i < n && script[i] != c && script[i] != '\n' && script[i] != '\r')
            {
                if (script[i] == '\\' && i + 1 < n)
                {
                    const char e = script[i + 1];
                    token.text += e == 'n' ? '\n' : e == 't' ? '\t' : e == 'r' ? '\r' : e;
                    i += 2;
                    continue;
                }
                token.text += script[i++];
            }
            if (i < n && script[i] == c)
                ++i;
        }
        else if (identifierStart(c))
        {
            token.kind = Token::Kind::Identifier;
            while (i < n && identifierPart(script[i]))
                token.text += script[i++];
        }
        else if (std::isdigit(static_cast<unsigned char>(c)))
        {
            token.kind = Token::Kind::Number;
            while (i < n && (identifierPart(script[i]) || script[i] == '.'))
                token.text += script[i++];
        }
        else
        {
            token.kind = Token::Kind::Punct;
            token.text = std::string(1, c);
            ++i;
        }
        tokens.push_back(std::move(token));
    }
    return tokens;
}

bool isPunct(const Token& token, char c)
{
    return token.kind == Token::Kind::Punct && token.text.size() == 1 && token.text[0] == c;
}

bool isIdentifier(const Token& token, const char* name)
{
    return token.kind == Token::Kind::Identifier && token.text == name;
}

/// A keyword that makes what follows it run only sometimes, or more than
/// once.
bool isControlKeyword(const Token& token)
{
    static const char* const keywords[] = {"if", "else", "while", "for", "do", "switch", "with", "case", "default",
        "return", "try", "catch", "finally"};
    if (token.kind != Token::Kind::Identifier)
        return false;
    for (const char* keyword : keywords)
    {
        if (token.text == keyword)
            return true;
    }
    return false;
}

/// Whether a statement can start at tokens[i]: the first token, or after a
/// semicolon or a closing brace, or on a new line after something that ends
/// an expression.
bool statementStart(const std::vector<Token>& tokens, size_t i)
{
    if (i == 0)
        return true;
    const Token& previous = tokens[i - 1];
    if (isPunct(previous, ';') || isPunct(previous, '}'))
        return true;
    if (!tokens[i].newlineBefore)
        return false;
    return previous.kind != Token::Kind::Punct || isPunct(previous, ')') || isPunct(previous, ']');
}

/// Whether a statement ends after tokens[i - 1]: the end, a semicolon, a
/// closing brace or a new line.
bool statementEnd(const std::vector<Token>& tokens, size_t i)
{
    return i >= tokens.size() || isPunct(tokens[i], ';') || isPunct(tokens[i], '}') || tokens[i].newlineBefore;
}

/// A frame number written as a whole number, 1 or more, or -1.
int frameNumber(const std::string& text)
{
    if (text.empty() || !std::all_of(text.begin(), text.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) || c == '.'; }))
        return -1;
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end != text.c_str() + text.size() || value < 1.0 || value > 1.0e7 || value != static_cast<double>(static_cast<int>(value)))
        return -1;
    return static_cast<int>(value);
}

} // namespace

std::vector<Action> parse(const std::string& script)
{
    std::vector<Action> actions;
    const std::vector<Token> tokens = tokenize(script);
    int braces = 0;
    int parens = 0;
    int brackets = 0;
    // In an if, else, loop and the like at the top level: until the
    // statement or block it governs ends.
    bool governed = false;
    for (size_t i = 0; i < tokens.size(); ++i)
    {
        const Token& token = tokens[i];
        if (token.kind == Token::Kind::Punct)
        {
            const char c = token.text[0];
            if (c == '{')
                ++braces;
            else if (c == '}')
            {
                braces = std::max(0, braces - 1);
                if (braces == 0 && parens == 0 && brackets == 0)
                    governed = false;
            }
            else if (c == '(')
                ++parens;
            else if (c == ')')
                parens = std::max(0, parens - 1);
            else if (c == '[')
                ++brackets;
            else if (c == ']')
                brackets = std::max(0, brackets - 1);
            else if (c == ';' && braces == 0 && parens == 0 && brackets == 0)
                governed = false;
            continue;
        }
        if (braces != 0 || parens != 0 || brackets != 0 || governed)
            continue;
        if (isControlKeyword(token))
        {
            governed = true;
            continue;
        }
        if (token.kind != Token::Kind::Identifier || !statementStart(tokens, i))
            continue;
        // The call's name, bare or after "this.".
        size_t name = i;
        if (isIdentifier(token, "this"))
        {
            if (i + 2 >= tokens.size() || !isPunct(tokens[i + 1], '.') || tokens[i + 2].kind != Token::Kind::Identifier)
                continue;
            name = i + 2;
        }
        if (name + 1 >= tokens.size() || !isPunct(tokens[name + 1], '('))
            continue;
        // The arguments, split at the top-level commas, up to the matching
        // parenthesis.
        std::vector<std::vector<const Token*>> arguments;
        size_t close = name + 2;
        int depth = 0;
        bool closed = false;
        for (; close < tokens.size(); ++close)
        {
            const Token& t = tokens[close];
            if (isPunct(t, '(') || isPunct(t, '[') || isPunct(t, '{'))
                ++depth;
            else if (depth == 0 && isPunct(t, ')'))
            {
                closed = true;
                break;
            }
            else if (isPunct(t, ')') || isPunct(t, ']') || isPunct(t, '}'))
                --depth;
            if (depth == 0 && isPunct(t, ','))
            {
                arguments.emplace_back();
                continue;
            }
            if (arguments.empty())
                arguments.emplace_back();
            arguments.back().push_back(&t);
        }
        if (!closed)
            break;
        // The parentheses are passed over here, so the loop's own counting
        // does not see them.
        const size_t after = close + 1;
        i = close;
        if (!statementEnd(tokens, after))
            continue;
        const std::string& called = tokens[name].text;
        Action action;
        if (called == "stop" || called == "play" || called == "nextFrame" || called == "prevFrame")
        {
            if (!arguments.empty())
                continue;
            action.call = called == "stop" ? Call::Stop : called == "play" ? Call::Play
                : called == "nextFrame" ? Call::NextFrame : Call::PrevFrame;
        }
        else if (called == "gotoAndPlay" || called == "gotoAndStop")
        {
            // One argument: a scene as well is not followed.
            if (arguments.size() != 1 || arguments[0].size() != 1)
                continue;
            const Token& target = *arguments[0][0];
            if (target.kind == Token::Kind::Number)
            {
                const int number = frameNumber(target.text);
                if (number < 1)
                    continue;
                action.frame = number - 1;
            }
            else if (target.kind == Token::Kind::String && !target.text.empty())
                action.label = target.text;
            else
                continue;
            action.call = called == "gotoAndPlay" ? Call::GotoAndPlay : Call::GotoAndStop;
        }
        else
        {
            continue;
        }
        actions.push_back(std::move(action));
    }
    return actions;
}

TimelineActions::TimelineActions(const fla::Timeline& timeline)
{
    for (const Layer* layer : timeline.layers)
    {
        if (!layer)
            continue;
        for (const Frame* frame : layer->frames)
        {
            if (!frame)
                continue;
            _length = std::max(_length, frame->index + std::max(frame->duration, 1));
            // A guide layer is not part of the movie, nor are its scripts.
            if (layer->layerType == Layer::Type::Guide || layer->layerType == Layer::Type::Folder)
                continue;
            if (!frame->name.empty() && frame->labelType != "comment" && !_labels.count(frame->name))
                _labels[frame->name] = frame->index;
            if (frame->actionScript && !frame->actionScript->code.empty())
            {
                std::vector<Action> found = parse(frame->actionScript->code);
                if (!found.empty())
                {
                    std::vector<Action>& at = _actions[frame->index];
                    at.insert(at.end(), found.begin(), found.end());
                }
            }
        }
    }
}

int TimelineActions::labelFrame(const std::string& label) const
{
    const auto found = _labels.find(label);
    return found == _labels.end() ? -1 : found->second;
}

State TimelineActions::enter(int frame) const
{
    State state;
    state.frame = std::max(0, std::min(frame, _length - 1));
    state.playing = true;
    // A goto lands on a frame whose own actions run in turn; a chain that
    // goes round is cut short.
    for (int hop = 0; hop < 8; ++hop)
    {
        const auto found = _actions.find(state.frame);
        if (found == _actions.end())
            break;
        int target = -1;
        for (const Action& action : found->second)
        {
            switch (action.call)
            {
            case Call::Stop:
                state.playing = false;
                break;
            case Call::Play:
                state.playing = true;
                break;
            case Call::GotoAndPlay:
            case Call::GotoAndStop:
            {
                const int to = action.label.empty() ? action.frame : labelFrame(action.label);
                if (to < 0)
                    break;
                target = std::min(to, _length - 1);
                state.playing = action.call == Call::GotoAndPlay;
                break;
            }
            case Call::NextFrame:
                target = std::min(state.frame + 1, _length - 1);
                state.playing = false;
                break;
            case Call::PrevFrame:
                target = std::max(state.frame - 1, 0);
                state.playing = false;
                break;
            }
        }
        if (target < 0 || target == state.frame)
            break;
        state.frame = target;
    }
    return state;
}

TimelineActions::Sequence TimelineActions::sequence() const
{
    Sequence sequence;
    // Once entered while playing, where a frame leads depends on the frame
    // alone: a frame seen playing before starts the repeat.
    std::map<int, int> seen;
    State state = enter(0);
    while (true)
    {
        sequence.frames.push_back(state.frame);
        if (!state.playing)
        {
            sequence.loopFrom = -1;
            break;
        }
        seen[state.frame] = static_cast<int>(sequence.frames.size()) - 1;
        state = enter(state.frame + 1 >= _length ? 0 : state.frame + 1);
        if (state.playing)
        {
            const auto again = seen.find(state.frame);
            if (again != seen.end())
            {
                sequence.loopFrom = again->second;
                break;
            }
        }
    }
    return sequence;
}

int TimelineActions::Sequence::frameAt(int elapsed) const
{
    if (frames.empty())
        return 0;
    elapsed = std::max(0, elapsed);
    const int count = static_cast<int>(frames.size());
    if (elapsed < count)
        return frames[static_cast<size_t>(elapsed)];
    if (loopFrom < 0 || loopFrom >= count)
        return frames.back();
    return frames[static_cast<size_t>(loopFrom + (elapsed - loopFrom) % (count - loopFrom))];
}

} // namespace frameactions

} // namespace fla
