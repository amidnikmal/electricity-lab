#include "ui/MathText.h"

#include <cctype>
#include <string>
#include <utility>

namespace current_lab::ui {

namespace {

struct SymbolEntry {
    const char* command;
    const char* utf8;
};

// UTF-8 replacements for the supported commands.
const SymbolEntry kSymbols[] = {
    {"tau", "\xCF\x84"},      // τ
    {"pi", "\xCF\x80"},       // π
    {"omega", "\xCF\x89"},    // ω
    {"Delta", "\xCE\x94"},    // Δ
    {"mu", "\xC2\xB5"},       // µ
    {"Omega", "\xCE\xA9"},    // Ω
    {"cdot", "\xC2\xB7"},     // ·
    {"approx", "\xE2\x89\x88"}, // ≈
    {"to", "\xE2\x86\x92"},   // →
    {"le", "\xE2\x89\xA4"},   // ≤
    {"ge", "\xE2\x89\xA5"},   // ≥
    {"infty", "\xE2\x88\x9E"}, // ∞
};

struct Parser {
    const std::string& src;
    size_t pos = 0;

    bool done() const { return pos >= src.size(); }
    char peek() const { return done() ? '\0' : src[pos]; }

    MathNode parseRow(char stopAt) {
        MathNode row;
        row.kind = MathNode::Kind::Row;
        std::string text;

        auto flushText = [&]() {
            if (text.empty()) return;
            MathNode node;
            node.kind = MathNode::Kind::Text;
            node.text = std::move(text);
            text.clear();
            row.children.push_back(std::move(node));
        };

        while (!done() && peek() != stopAt) {
            char ch = src[pos];
            if (ch == '\\') {
                ++pos;
                std::string command;
                while (!done() && std::isalpha(static_cast<unsigned char>(peek())))
                    command += src[pos++];
                if (command == "frac") {
                    flushText();
                    MathNode frac;
                    frac.kind = MathNode::Kind::Frac;
                    frac.children.push_back(parseGroup());
                    frac.children.push_back(parseGroup());
                    row.children.push_back(std::move(frac));
                } else {
                    bool replaced = false;
                    for (const auto& symbol : kSymbols) {
                        if (command == symbol.command) {
                            text += symbol.utf8;
                            replaced = true;
                            break;
                        }
                    }
                    if (!replaced) text += command; // unknown command: literal
                }
            } else if (ch == '^' || ch == '_') {
                flushText();
                ++pos;
                MathNode script;
                script.kind = ch == '^' ? MathNode::Kind::Sup : MathNode::Kind::Sub;
                script.children.push_back(parseGroup());
                row.children.push_back(std::move(script));
            } else if (ch == '{') {
                flushText();
                row.children.push_back(parseGroup());
            } else if (ch == '-') {
                text += "\xE2\x88\x92"; // proper minus sign −
                ++pos;
            } else {
                text += ch;
                ++pos;
            }
        }
        if (!done() && peek() == stopAt) ++pos; // consume the brace
        flushText();
        return row;
    }

    // A group is either {row}, a single \command, or a single character.
    MathNode parseGroup() {
        if (peek() == '{') {
            ++pos;
            return parseRow('}');
        }
        MathNode node;
        node.kind = MathNode::Kind::Text;
        if (done()) return node;
        if (peek() == '\\') {
            ++pos;
            std::string command;
            while (!done() && std::isalpha(static_cast<unsigned char>(peek())))
                command += src[pos++];
            for (const auto& symbol : kSymbols) {
                if (command == symbol.command) {
                    node.text = symbol.utf8;
                    return node;
                }
            }
            node.text = command;
            return node;
        }
        node.text = std::string(1, src[pos++]);
        return node;
    }
};

} // namespace

MathNode parseMath(const std::string& source) {
    Parser parser{source};
    return parser.parseRow('\0');
}

} // namespace current_lab::ui
