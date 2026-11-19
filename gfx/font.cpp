#include "font.hpp"

#include <cctype>
#include <cstdint>
#include <vector>

namespace gfx {
namespace {

struct Glyph {
    char c;
    const char* rows; // seven rows of five, separated by spaces; '#' is ink
};

const Glyph glyphs[] = {
    {'0', ".###. #...# #..## #.#.# ##..# #...# .###."}, {'1', "..#.. .##.. ..#.. ..#.. ..#.. ..#.. .###."},
    {'2', ".###. #...# ....# ...#. ..#.. .#... #####"}, {'3', "##### ...#. ..#.. ...#. ....# #...# .###."},
    {'4', "...#. ..##. .#.#. #..#. ##### ...#. ...#."}, {'5', "##### #.... ####. ....# ....# #...# .###."},
    {'6', "..##. .#... #.... ####. #...# #...# .###."}, {'7', "##### ....# ...#. ..#.. .#... .#... .#..."},
    {'8', ".###. #...# #...# .###. #...# #...# .###."}, {'9', ".###. #...# #...# .#### ....# ...#. .##.."},
    {'A', ".###. #...# #...# ##### #...# #...# #...#"}, {'B', "####. #...# #...# ####. #...# #...# ####."},
    {'C', ".###. #...# #.... #.... #.... #...# .###."}, {'D', "####. #...# #...# #...# #...# #...# ####."},
    {'E', "##### #.... #.... ####. #.... #.... #####"}, {'F', "##### #.... #.... ####. #.... #.... #...."},
    {'G', ".###. #...# #.... #.### #...# #...# .####"}, {'H', "#...# #...# #...# ##### #...# #...# #...#"},
    {'I', ".###. ..#.. ..#.. ..#.. ..#.. ..#.. .###."}, {'J', "..### ...#. ...#. ...#. ...#. #..#. .##.."},
    {'K', "#...# #..#. #.#.. ##... #.#.. #..#. #...#"}, {'L', "#.... #.... #.... #.... #.... #.... #####"},
    {'M', "#...# ##.## #.#.# #.#.# #...# #...# #...#"}, {'N', "#...# #...# ##..# #.#.# #..## #...# #...#"},
    {'O', ".###. #...# #...# #...# #...# #...# .###."}, {'P', "####. #...# #...# ####. #.... #.... #...."},
    {'Q', ".###. #...# #...# #...# #.#.# #..#. .##.#"}, {'R', "####. #...# #...# ####. #.#.. #..#. #...#"},
    {'S', ".#### #.... #.... .###. ....# ....# ####."}, {'T', "##### ..#.. ..#.. ..#.. ..#.. ..#.. ..#.."},
    {'U', "#...# #...# #...# #...# #...# #...# .###."}, {'V', "#...# #...# #...# #...# #...# .#.#. ..#.."},
    {'W', "#...# #...# #...# #.#.# #.#.# #.#.# .#.#."}, {'X', "#...# #...# .#.#. ..#.. .#.#. #...# #...#"},
    {'Y', "#...# #...# .#.#. ..#.. ..#.. ..#.. ..#.."}, {'Z', "##### ....# ...#. ..#.. .#... #.... #####"},
    {'.', "..... ..... ..... ..... ..... .##.. .##.."}, {',', "..... ..... ..... ..... .##.. ..#.. .#..."},
    {':', "..... .##.. .##.. ..... .##.. .##.. ....."}, {'-', "..... ..... ..... .###. ..... ..... ....."},
    {'+', "..... ..#.. ..#.. ##### ..#.. ..#.. ....."}, {'=', "..... ..... ##### ..... ##### ..... ....."},
    {'/', "....# ....# ...#. ..#.. .#... #.... #...."}, {'%', "##..# ##..# ...#. ..#.. .#... #..## #..##"},
    {'(', "...#. ..#.. .#... .#... .#... ..#.. ...#."}, {')', ".#... ..#.. ...#. ...#. ...#. ..#.. .#..."},
    {'>', ".#... ..#.. ...#. ....# ...#. ..#.. .#..."}, {'<', "...#. ..#.. .#... #.... .#... ..#.. ...#."},
    {'|', ".#.#. .#.#. .#.#. .#.#. .#.#. .#.#. .#.#."}, {'*', "..... #...# .#.#. ..#.. .#.#. #...# ....."},
    {'~', "..... ..... .#... #.#.# ...#. ..... ....."}, {'_', "..... ..... ..... ..... ..... ..... #####"},
    {'\'', "..#.. ..#.. ..... ..... ..... ..... ....."}, {'!', "..#.. ..#.. ..#.. ..#.. ..#.. ..... ..#.."},
    {'?', ".###. #...# ....# ...#. ..#.. ..... ..#.."}, {'#', ".#.#. ##### .#.#. .#.#. .#.#. ##### .#.#."},
};

constexpr int atlas_size = 128, cell_w = 6, cell_h = 8, columns = atlas_size / cell_w;
constexpr int solid = 127; // a fully inked cell for rectangles

int slot(char c) {
    const unsigned char u = static_cast<unsigned char>(std::toupper(static_cast<unsigned char>(c)));
    return u < 32 || u > 127 ? '?' - 32 : u - 32;
}

} // namespace

bool Font::init() {
    std::vector<std::uint8_t> rgba(atlas_size * atlas_size * 4, 0);
    const auto ink = [&](int index, int x, int y) {
        const int px = (index % columns) * cell_w + x, py = (index / columns) * cell_h + y;
        auto* p = &rgba[(py * atlas_size + px) * 4];
        p[0] = p[1] = p[2] = p[3] = 255;
    };
    for (const auto& glyph : glyphs) {
        int x = 0, y = 0;
        for (const char* r = glyph.rows; *r; ++r) {
            if (*r == ' ') {
                ++y;
                x = 0;
                continue;
            }
            if (*r == '#') {
                ink(glyph.c - 32, x, y);
            }
            ++x;
        }
    }
    for (int y = 0; y < cell_h; ++y) {
        for (int x = 0; x < cell_w; ++x) {
            ink(solid - 32, x, y);
        }
    }
    texture_ = make_texture(atlas_size, atlas_size, gl::RGBA, rgba.data());
    return texture_.id != 0;
}

float Font::text(Batch& batch, float x, float y, const std::string& text, int scale, Color color) const {
    for (const char c : text) {
        const int index = slot(c);
        const float u = float((index % columns) * cell_w) / atlas_size;
        const float v = float((index / columns) * cell_h) / atlas_size;
        if (c != ' ') {
            batch.quad(texture_, x, y, x + glyph_width * scale, y + glyph_height * scale, u, v,
                       u + float(glyph_width) / atlas_size, v + float(glyph_height) / atlas_size, color);
        }
        x += advance * scale;
    }
    return x;
}

void Font::rect(Batch& batch, float x0, float y0, float x1, float y1, Color color) const {
    const int index = solid - 32;
    const float u = (float((index % columns) * cell_w) + 3.0f) / atlas_size;
    const float v = (float((index / columns) * cell_h) + 4.0f) / atlas_size;
    batch.quad(texture_, x0, y0, x1, y1, u, v, u, v, color);
}

} // namespace gfx
