#pragma once

#include <string>

#include "batch.hpp"

namespace gfx {

// The built-in 5 x 7 bitmap font, drawn at integer scales. Lowercase letters
// draw as capitals. The atlas also holds a white texel for solid rectangles,
// so text and panels share one texture and one draw call.
class Font {
public:
    static constexpr int glyph_width = 5, glyph_height = 7, advance = 6;

    bool init();
    const Texture& texture() const { return texture_; }

    // Returns the x after the last character.
    float text(Batch& batch, float x, float y, const std::string& text, int scale, Color color) const;
    float width(const std::string& text, int scale) const { return float(text.size() * advance * scale); }
    void rect(Batch& batch, float x0, float y0, float x1, float y1, Color color) const;

private:
    Texture texture_;
};

} // namespace gfx
