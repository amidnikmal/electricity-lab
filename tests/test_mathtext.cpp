#include <gtest/gtest.h>
#include "ui/MathText.h"

using current_lab::ui::MathNode;
using current_lab::ui::parseMath;

TEST(MathText, PlainTextStaysPlain) {
    auto tree = parseMath("P = U I");
    ASSERT_EQ(tree.kind, MathNode::Kind::Row);
    ASSERT_EQ(tree.children.size(), 1u);
    EXPECT_EQ(tree.children[0].kind, MathNode::Kind::Text);
    EXPECT_EQ(tree.children[0].text, "P = U I");
}

TEST(MathText, FractionHasNumeratorAndDenominator) {
    auto tree = parseMath("I = \\frac{V}{R}");
    ASSERT_GE(tree.children.size(), 2u);
    const MathNode& frac = tree.children.back();
    ASSERT_EQ(frac.kind, MathNode::Kind::Frac);
    ASSERT_EQ(frac.children.size(), 2u);
    EXPECT_EQ(frac.children[0].children[0].text, "V");
    EXPECT_EQ(frac.children[1].children[0].text, "R");
}

TEST(MathText, SuperscriptAndSubscriptParse) {
    auto tree = parseMath("V_C e^{-t}");
    bool hasSub = false, hasSup = false;
    for (const auto& child : tree.children) {
        hasSub = hasSub || child.kind == MathNode::Kind::Sub;
        hasSup = hasSup || child.kind == MathNode::Kind::Sup;
    }
    EXPECT_TRUE(hasSub);
    EXPECT_TRUE(hasSup);
}

TEST(MathText, GreekAndSymbolsBecomeUtf8) {
    auto tree = parseMath("\\tau = RC \\cdot 1");
    ASSERT_FALSE(tree.children.empty());
    const std::string& text = tree.children[0].text;
    EXPECT_NE(text.find("\xCF\x84"), std::string::npos); // τ
    EXPECT_NE(text.find("\xC2\xB7"), std::string::npos); // ·
    EXPECT_EQ(text.find("\\tau"), std::string::npos);
}

TEST(MathText, MinusBecomesProperMinusSign) {
    auto tree = parseMath("1 - 2");
    EXPECT_NE(tree.children[0].text.find("\xE2\x88\x92"), std::string::npos);
}

TEST(MathText, SingleCharAndCommandScripts) {
    auto tree = parseMath("x^2 + e^\\tau");
    int sups = 0;
    for (const auto& child : tree.children)
        if (child.kind == MathNode::Kind::Sup) ++sups;
    EXPECT_EQ(sups, 2);
}

TEST(MathText, UnbalancedBracesDoNotCrash) {
    auto a = parseMath("\\frac{V}{");
    auto b = parseMath("x^{");
    auto c = parseMath("}{}{");
    EXPECT_EQ(a.kind, MathNode::Kind::Row);
    EXPECT_EQ(b.kind, MathNode::Kind::Row);
    EXPECT_EQ(c.kind, MathNode::Kind::Row);
}

namespace {

std::string collectText(const MathNode& node) {
    std::string text = node.text;
    for (const auto& child : node.children)
        text += collectText(child);
    return text;
}

} // namespace

TEST(MathText, MinusInUnbracedScriptBecomesProperMinus) {
    auto tree = parseMath("x^-1");
    const MathNode* sup = nullptr;
    for (const auto& child : tree.children) {
        if (child.kind == MathNode::Kind::Sup) {
            sup = &child;
            break;
        }
    }

    ASSERT_NE(sup, nullptr);
    std::string text = collectText(*sup);
    EXPECT_NE(text.find("\xE2\x88\x92"), std::string::npos);
    EXPECT_EQ(text.find('-'), std::string::npos);
}

TEST(MathText, UnknownCommandKeepsBackslash) {
    auto tree = parseMath("\\alpha");
    std::string text = collectText(tree);
    EXPECT_NE(text.find("\\alpha"), std::string::npos);
}
