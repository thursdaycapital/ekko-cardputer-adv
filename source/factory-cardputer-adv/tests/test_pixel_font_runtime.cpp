#include "EkkoPixelFont12.h"
#include "FactoryUi.h"
#include <cassert>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "actual_ui_formats.h"
extern const uint8_t lgfx_efont_cn_12[];

int main(int argc, char** argv) {
    assert(argc == 4);
    lgfx::FontMetrics metrics{};
    ekko_pixel_font::kFont.getDefaultMetric(&metrics);
    assert(metrics.height == 12 && metrics.y_advance == 12 && metrics.baseline == 10);
    assert(metrics.y_offset == -10 && metrics.x_advance == 12);
    lgfx::LGFXBase frame;
    lgfx::TextStyle style;
    const lgfx::U8g2font originalEfont(lgfx_efont_cn_12);
    std::ifstream records(argv[1]);
    unsigned codepoint, advance, width, height;
    int xoff, yoff;
    size_t checked = 0, pixelsCompared = 0, supplementsChecked = 0;
    bool supplement;
    while (records >> codepoint >> advance >> width >> height >> xoff >> yoff >> supplement) {
        std::vector<uint64_t> rows(height);
        for (auto& row : rows) records >> std::hex >> row >> std::dec;
        assert(records && width <= 64);
        ekko_pixel_font::kFont.getDefaultMetric(&metrics);
        assert(ekko_pixel_font::kFont.updateFontMetric(&metrics, codepoint));
        assert(metrics.x_advance == static_cast<int>(advance));
        frame.clear();
        frame.setClipRect(0, 0, frame.width(), 12);
        int32_t filled = 0;
        const auto actualAdvance = ekko_pixel_font::kFont.drawChar(
            &frame, 8, 10, codepoint, &style, &metrics, filled);
        assert(actualAdvance == advance);
        int32_t cx, cy, cw, ch;
        frame.getClipRect(&cx, &cy, &cw, &ch);
        assert(cx == 0 && cy == 0 && cw == frame.width() && ch == 12);
        std::vector<uint8_t> expected(frame.pixels.size(), 0);
        const int bits = ((width + 7) / 8) * 8;
        for (unsigned row = 0; row < height; ++row) {
            for (unsigned column = 0; column < width; ++column) {
                const int x = 8 + xoff + static_cast<int>(column);
                const int y = 10 - yoff - static_cast<int>(height) + static_cast<int>(row);
                if (x >= 0 && x < frame.width() && y >= 0 && y < 12)
                    expected[y * frame.width() + x] = (rows[row] >> (bits - column - 1)) & 1;
            }
        }
        if (frame.pixels != expected) {
            std::cerr << "BDF raster mismatch U+" << std::hex << codepoint << std::dec << '\n';
            return 2;
        }
        if (supplement) {
            lgfx::LGFXBase sourceFrame;
            sourceFrame.setClipRect(0, 0, sourceFrame.width(), 12);
            lgfx::FontMetrics sourceMetrics{};
            originalEfont.getDefaultMetric(&sourceMetrics);
            assert(originalEfont.updateFontMetric(&sourceMetrics, codepoint));
            assert(sourceMetrics.x_advance == static_cast<int>(advance));
            filled = 0;
            originalEfont.drawChar(&sourceFrame, 8, 10, codepoint, &style, &sourceMetrics, filled);
            if (sourceFrame.pixels != expected) {
                std::cerr << "Original factory efont mismatch U+" << std::hex << codepoint << std::dec << '\n';
                return 3;
            }
            ++supplementsChecked;
        }
        frame.clear();
        frame.setClipRect(10, 4, 9, 5);
        filled = 0;
        ekko_pixel_font::kFont.drawChar(&frame, 8, 10, codepoint, &style, &metrics, filled);
        frame.getClipRect(&cx, &cy, &cw, &ch);
        assert(cx == 10 && cy == 4 && cw == 9 && ch == 5);
        auto clippedExpected = expected;
        for (int y = 0; y < frame.height(); ++y)
            for (int x = 0; x < frame.width(); ++x)
                if (x < 10 || x >= 19 || y < 4 || y >= 9) clippedExpected[y * frame.width() + x] = 0;
        assert(frame.pixels == clippedExpected);
        frame.clear();
        frame.setClipRect(0, 20, frame.width(), 8);
        filled = 0;
        ekko_pixel_font::kFont.drawChar(&frame, 8, 10, codepoint, &style, &metrics, filled);
        frame.getClipRect(&cx, &cy, &cw, &ch);
        assert(cx == 0 && cy == 20 && cw == frame.width() && ch == 8);
        assert(std::none_of(frame.pixels.begin(), frame.pixels.end(), [](uint8_t pixel) { return pixel; }));
        assert(metrics.height == 12 && metrics.y_offset == -10);
        ++checked;
        pixelsCompared += 3 * expected.size();
    }
    assert(checked == ekko_pixel_font::kGlyphCount);
    std::cout << "GLYPHS\t" << checked << '\t' << pixelsCompared << '\t' << supplementsChecked << '\n';

    frame.setFont(&ekko_pixel_font::kFont);
    assert(frame.textWidth("ABC 123") == 42);
    assert(frame.textWidth("无线扫描") == 48);
    frame.setTextSize(1);
    const auto timeSample = gpsTimeSample();
    const auto speedSample = gpsSpeedSample();
    assert(frame.textWidth(timeSample.c_str()) <= 192);
    assert(frame.textWidth(speedSample.c_str()) <= 120);
    std::cout << "FORMATTED\tworldTime\t" << frame.textWidth(timeSample.c_str()) << '\n';
    std::cout << "FORMATTED\tspeed\t" << frame.textWidth(speedSample.c_str()) << '\n';
    frame.setTextSize(1.5);
    const auto coordinateSample = gpsCoordinateSample();
    assert(frame.textWidth(coordinateSample.c_str()) <= 192);
    std::cout << "FORMATTED\tcoordinateAt1_5x\t" << frame.textWidth(coordinateSample.c_str()) << '\n';
    frame.setTextSize(1);
    std::ifstream labels(argv[2]);
    std::string text;
    size_t index = 0;
    while (std::getline(labels, text)) {
        const int labelWidth = frame.textWidth(text.c_str());
        std::cout << "LABEL\t" << index++ << '\t' << labelWidth << '\n';
    }
    lgfx::LGFXBase menu(204, 16 * 14);
    menu.setFont(&ekko_pixel_font::kFont);
    int y = 0;
    for (const auto& label : factory_ui::kAppLabels) {
        const int labelWidth = menu.textWidth(label.title);
        assert(labelWidth <= menu.width());
        menu.draw_string(label.title, 102, y, lgfx::textdatum_t::top_center);
        std::cout << "MENU\t" << label.id << '\t' << labelWidth << '\n';
        y += 16;
    }
    // Exercise the actual production UTF-8 truncator against vendor measuring.
    for (const auto& label : factory_ui::kAppLabels) {
        for (int available = 0; available <= 80; ++available) {
            const auto fitted = factory_ui::fitText(frame, label.title, available);
            assert(frame.textWidth(fitted.c_str()) <= available);
        }
    }
    std::ofstream image(argv[3], std::ios::binary);
    image << "P5\n" << menu.width() << ' ' << menu.height() << "\n255\n";
    for (const uint8_t pixel : menu.pixels) image.put(pixel ? char(255) : char(0));
    assert(image.good());
}
