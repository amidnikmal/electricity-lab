#pragma once
//
// LabelLayout — раскладка текстовых надписей без перекрытий (чистая, тестируемая).
//
// Надписи на холсте (значения компонентов, показания узлов) задаются мировыми
// позициями и в плотных схемах налезают друг на друга. Здесь — оценка экранного
// размера надписи и разведение боксов по вертикали так, чтобы они не пересекались.
// Используется и в рендере (PrimitiveRenderer), и в жёстком тесте (test_label_layout).

#include <string>
#include <vector>
#include <algorithm>

namespace current_lab::render {

struct LabelBox {
    float x = 0, y = 0;   // левый-верхний угол (экранные пиксели)
    float w = 0, h = 0;   // размер
    int   id = 0;         // индекс исходной надписи (сохраняется при сортировке)
};

// Консервативная оценка ширины строки для дефолтного шрифта ImGui при данном
// fontSize. Считаем КОДОВЫЕ ТОЧКИ UTF-8 (кириллица = 2 байта, но 1 глиф).
// Коэффициент 0.62 чуть завышен относительно реального среднего — так бокс не
// уже текста, и развод по нему гарантирует отсутствие перекрытий и для рендера.
inline float estimateLabelWidth(const std::string& text, float fontSize) {
    int glyphs = 0;
    for (unsigned char c : text)
        if ((c & 0xC0) != 0x80) ++glyphs; // не байт-продолжение UTF-8
    return glyphs * 0.62f * fontSize;
}

inline float labelLineHeight(float fontSize) {
    return fontSize * 1.15f;
}

inline bool boxesOverlap(const LabelBox& a, const LabelBox& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w &&
           a.y < b.y + b.h && b.y < a.y + a.h;
}

inline bool anyOverlap(const std::vector<LabelBox>& boxes) {
    for (size_t i = 0; i < boxes.size(); ++i)
        for (size_t j = i + 1; j < boxes.size(); ++j)
            if (boxesOverlap(boxes[i], boxes[j])) return true;
    return false;
}

// Разводит перекрывающиеся надписи СДВИГОМ ВНИЗ (x сохраняется). Жадно: сортируем
// по (y,x); каждый следующий бокс опускаем ниже всех уже размещённых, с которыми
// он пересекается, пока пересечений не останется. Сдвиг только вниз → сходится.
inline void declutterVertical(std::vector<LabelBox>& boxes, float gap = 2.0f) {
    if (boxes.size() < 2) return;
    std::stable_sort(boxes.begin(), boxes.end(), [](const LabelBox& a, const LabelBox& b) {
        if (a.y != b.y) return a.y < b.y;
        return a.x < b.x;
    });
    for (size_t i = 1; i < boxes.size(); ++i) {
        bool moved = true;
        int guard = 0;
        while (moved && guard++ < 10000) {
            moved = false;
            for (size_t j = 0; j < i; ++j) {
                if (boxesOverlap(boxes[i], boxes[j])) {
                    boxes[i].y = boxes[j].y + boxes[j].h + gap;
                    moved = true;
                }
            }
        }
    }
}

} // namespace current_lab::render
