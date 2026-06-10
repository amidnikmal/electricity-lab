#include "ui/MathText.h"
#include "imgui.h"

#include <algorithm>
#include <cstring>
#include <cfloat>
#include <utility>

namespace current_lab::ui {

namespace {

// --- measuring & drawing -------------------------------------------------------

struct DrawCtx {
    ImDrawList* dl = nullptr;
    ImFont* font = nullptr;
    ImU32 color = 0xFFFFFFFFu;
};

ImVec2 measure(const MathNode& node, float size);

ImVec2 measureRow(const MathNode& row, float size) {
    float width = 0.0f, height = size;
    for (const auto& child : row.children) {
        ImVec2 extent = measure(child, size);
        width += extent.x;
        height = std::max(height, extent.y);
    }
    return ImVec2(width, height);
}

ImVec2 measure(const MathNode& node, float size) {
    switch (node.kind) {
        case MathNode::Kind::Text: {
            ImFont* font = ImGui::GetFont();
            return font->CalcTextSizeA(size, FLT_MAX, 0.0f, node.text.c_str());
        }
        case MathNode::Kind::Row:
            return measureRow(node, size);
        case MathNode::Kind::Sup:
        case MathNode::Kind::Sub: {
            ImVec2 inner = node.children.empty() ? ImVec2(0, 0)
                                                 : measure(node.children[0], size * 0.68f);
            return ImVec2(inner.x, size * 1.15f);
        }
        case MathNode::Kind::Frac: {
            ImVec2 num = node.children.size() > 0 ? measure(node.children[0], size * 0.85f)
                                                  : ImVec2(0, 0);
            ImVec2 den = node.children.size() > 1 ? measure(node.children[1], size * 0.85f)
                                                  : ImVec2(0, 0);
            return ImVec2(std::max(num.x, den.x) + 6.0f, num.y + den.y + 5.0f);
        }
    }
    return ImVec2(0, 0);
}

// Draws the node with its vertical center at centerY; returns the advance.
float draw(const DrawCtx& ctx, const MathNode& node, float x, float centerY, float size);

float drawRow(const DrawCtx& ctx, const MathNode& row, float x, float centerY, float size) {
    float advance = 0.0f;
    for (const auto& child : row.children)
        advance += draw(ctx, child, x + advance, centerY, size);
    return advance;
}

float draw(const DrawCtx& ctx, const MathNode& node, float x, float centerY, float size) {
    switch (node.kind) {
        case MathNode::Kind::Text: {
            ImVec2 extent = ctx.font->CalcTextSizeA(size, FLT_MAX, 0.0f, node.text.c_str());
            ctx.dl->AddText(ctx.font, size, ImVec2(x, centerY - size * 0.5f), ctx.color,
                            node.text.c_str());
            return extent.x;
        }
        case MathNode::Kind::Row:
            return drawRow(ctx, node, x, centerY, size);
        case MathNode::Kind::Sup: {
            if (node.children.empty()) return 0.0f;
            float small = size * 0.68f;
            return draw(ctx, node.children[0], x, centerY - size * 0.38f, small);
        }
        case MathNode::Kind::Sub: {
            if (node.children.empty()) return 0.0f;
            float small = size * 0.68f;
            return draw(ctx, node.children[0], x, centerY + size * 0.32f, small);
        }
        case MathNode::Kind::Frac: {
            float small = size * 0.85f;
            ImVec2 num = node.children.size() > 0 ? measure(node.children[0], small) : ImVec2(0, 0);
            ImVec2 den = node.children.size() > 1 ? measure(node.children[1], small) : ImVec2(0, 0);
            float width = std::max(num.x, den.x) + 6.0f;
            if (node.children.size() > 0)
                draw(ctx, node.children[0], x + (width - num.x) * 0.5f,
                     centerY - num.y * 0.5f - 2.5f, small);
            if (node.children.size() > 1)
                draw(ctx, node.children[1], x + (width - den.x) * 0.5f,
                     centerY + den.y * 0.5f + 2.5f, small);
            ctx.dl->AddLine(ImVec2(x + 1.0f, centerY), ImVec2(x + width - 1.0f, centerY),
                            ctx.color, 1.0f);
            return width;
        }
    }
    return 0.0f;
}

} // namespace

void renderMathText(const char* source) {
    MathNode tree = parseMath(source ? source : "");

    DrawCtx ctx;
    ctx.dl = ImGui::GetWindowDrawList();
    ctx.font = ImGui::GetFont();
    ctx.color = ImGui::GetColorU32(ImGuiCol_Text);

    float size = ImGui::GetFontSize() * 1.15f;
    ImVec2 extent = measure(tree, size);
    extent.y = std::max(extent.y, size) + 4.0f;

    ImVec2 origin = ImGui::GetCursorScreenPos();
    draw(ctx, tree, origin.x + 2.0f, origin.y + extent.y * 0.5f, size);
    ImGui::Dummy(ImVec2(extent.x + 4.0f, extent.y));
}

} // namespace current_lab::ui
