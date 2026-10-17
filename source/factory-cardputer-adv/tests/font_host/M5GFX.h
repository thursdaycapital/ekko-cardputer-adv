#pragma once
// Host drawing surface only. Font declarations, UTF-8 decoding, text measuring,
// layout and raster decoding are compiled from the actual vendor source.
#include <lgfx/v1/lgfx_fonts.hpp>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>

namespace lgfx { inline namespace v1 {
class LGFXBase {
public:
    explicit LGFXBase(int width = 64, int height = 32)
        : width_(width), height_(height), pixels(width * height, 0), clipW_(width), clipH_(height) {}
    struct ColorConverter {
        uint32_t convert(uint32_t value) const { return value; }
    } converter_;
    ColorConverter* getColorConverter() { return &converter_; }
    void startWrite() {}
    void endWrite() {}
    void setRawColor(uint32_t color) { color_ = color; }
    void setColor(uint32_t color) { color_ = color; }
    void getClipRect(int32_t* x, int32_t* y, int32_t* w, int32_t* h) {
        *x = clipX_; *y = clipY_; *w = clipW_; *h = clipH_;
    }
    void setClipRect(int32_t x, int32_t y, int32_t w, int32_t h) {
        assert(w >= 0 && h >= 0);
        clipX_ = x; clipY_ = y; clipW_ = w; clipH_ = h;
    }
    void writeFillRect(int32_t x, int32_t y, int32_t w, int32_t h) {
        // Detect unsigned row/height underflow before the sink clips it away.
        assert(w >= 0 && h >= 0 && w < 2048 && h < 2048);
        for (int py = std::max({0, y, clipY_}); py < std::min({height_, y + h, clipY_ + clipH_}); ++py)
            for (int px = std::max({0, x, clipX_}); px < std::min({width_, x + w, clipX_ + clipW_}); ++px)
                pixels[py * width_ + px] = color_ != 0;
    }
    void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
        setRawColor(color); writeFillRect(x, y, w, h);
    }
    void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
        fillRect(x, y, w, 1, color); fillRect(x, y + h - 1, w, 1, color);
        fillRect(x, y, 1, h, color); fillRect(x + w - 1, y, 1, h, color);
    }
    void setFont(const IFont* font) { _font = font; font->getDefaultMetric(&_font_metrics); }
    void setTextSize(float scale) { _text_style.size_x = _text_style.size_y = scale; }
    int32_t textWidth(const char* text, const IFont* font = nullptr);
    int32_t text_width(const char* text, const IFont* font, FontMetrics* metrics);
    uint16_t decodeUTF8(uint8_t byte);
    size_t draw_string(const char* text, int32_t x, int32_t y, textdatum_t datum, const IFont* font = nullptr);
    void clear() { std::fill(pixels.begin(), pixels.end(), 0); }
    int width() const { return width_; }
    int height() const { return height_; }
    int width_, height_;
    std::vector<uint8_t> pixels;
    int32_t clipX_ = 0, clipY_ = 0, clipW_, clipH_;
    uint32_t color_ = 0;
    const IFont* _font = nullptr;
    FontMetrics _font_metrics{};
    TextStyle _text_style;
    enum utf8_decode_state_t : uint8_t { utf8_state0 = 0, utf8_state1 = 1, utf8_state2 = 2 };
    utf8_decode_state_t _decoderState = utf8_state0;
    uint16_t _unicode_buffer = 0;
};
} }
