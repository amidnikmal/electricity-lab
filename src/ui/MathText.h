#pragma once

#include <string>
#include <vector>

// Textbook-style math rendering for ImGui. The mini-markup supports:
//   ^{...} / ^x   superscript          _{...} / _x   subscript
//   \frac{a}{b}   stacked fraction     {...}         grouping
//   \tau \pi \omega \Delta \mu \Omega  greek (UTF-8)
//   \cdot \approx \to \le \ge \infty -  proper symbols (· ≈ → ≤ ≥ ∞ −)
// Parsing is pure logic (testable); drawing lives in renderMathText.
namespace current_lab::ui {

struct MathNode {
    enum class Kind { Row, Text, Sup, Sub, Frac };
    Kind kind = Kind::Text;
    std::string text;                // Kind::Text
    std::vector<MathNode> children;  // Row: sequence; Sup/Sub: [arg]; Frac: [num, den]
};

MathNode parseMath(const std::string& source);

// Draws the formula at the current ImGui cursor and advances it.
void renderMathText(const char* source);

} // namespace current_lab::ui
