// Paints a contact sheet of plant sprites, to judge the painter without
// opening a window: `petrihd-sprites sheet.png`.
#include <cstdio>
#include <vector>

#include "plant_sprites.hpp"
#include "png.hpp"

int main(int argc, char** argv) {
    using namespace art::sprites;
    const int width = 480, height = 960;
    std::vector<std::uint8_t> rgb(std::size_t(width) * height * 3);
    for (std::size_t i = 0; i < rgb.size(); i += 3) {
        rgb[i] = 128, rgb[i + 1] = 138, rgb[i + 2] = 78;
    }
    int x = 4, shelf = 4, tallest = 0;
    const auto place = [&](const Spec& spec) {
        const Image image = paint(spec);
        if (x + image.width + 4 > width) {
            x = 4;
            shelf += tallest + 6;
            tallest = 0;
        }
        for (int j = 0; j < image.height && shelf + j < height; ++j) {
            for (int i = 0; i < image.width; ++i) {
                const auto* p = &image.rgba[(std::size_t(j) * image.width + i) * 4];
                if (p[3]) { // solid or glowing
                    auto* out = &rgb[(std::size_t(shelf + j) * width + x + i) * 3];
                    out[0] = p[0], out[1] = p[1], out[2] = p[2];
                }
            }
        }
        x += image.width + 4;
        tallest = image.height > tallest ? image.height : tallest;
    };
    const auto row = [&] {
        x = 4;
        shelf += tallest + 6;
        tallest = 0;
    };
    // Crowns: each form at three sizes, then bare, autumn, and small ones.
    for (int form = 0; form < 4; ++form) {
        for (int v = 0; v < 3; ++v) {
            place({Kind::crown, 0, std::uint16_t(22 + v * 14), std::uint8_t(form == 3 ? 112 : 80),
                   std::uint8_t(form == 3 ? 144 : 112), std::uint8_t(form == 1 ? 80 : 48), 0, 0, std::uint8_t(v),
                   std::uint8_t(form)});
        }
        row();
    }
    for (int view = 0; view < views; ++view) {
        place({Kind::crown, 0, 40, 112, 98, 86, bare, 0, 0, 0, std::uint8_t(view)});
    }
    // A snag's top, from the same three pitches.
    for (int view = 0; view < views; ++view) {
        place({Kind::crown, 0, 16, 104, 92, 82, std::uint8_t(bare | dead), 0, 1, 0, std::uint8_t(view)});
    }
    row();
    place({Kind::crown, 0, 30, 208, 112, 48, 0, 0, 1});
    place({Kind::crown, 0, 30, 208, 176, 48, 0, 0, 1, 3});
    place({Kind::crown, 0, 30, 176, 48, 48, 0, 0, 1, 1});
    place({Kind::crown, 0, 12, 80, 112, 48, 0, 0, 1});
    place({Kind::crown, 0, 8, 80, 112, 48, 0, 0, 1, 2});
    place({Kind::trunk, 60, 5, 0, 0, 0, pale, 0, 0});
    row();
    for (int v = 0; v < 5; ++v) {
        place({Kind::trunk, std::uint16_t(30 + v * 18), std::uint16_t(2 + v * 2), 0, 0, 0, std::uint8_t(v == 4 ? bare : 0), 0, 0});
    }
    for (int v = 0; v < 6; ++v) {
        place({Kind::bough, std::uint16_t(30 - v * 3), std::uint16_t(34 - v * 6), 48, 104, 70,
               std::uint8_t(v == 5 ? tip : 0), 0, std::uint8_t(v)});
    }
    row();
    for (int v = 0; v < 8; ++v) {
        place({Kind::bush, std::uint16_t(10 + v * 3), std::uint16_t(6 + v * 2), std::uint8_t(64 + v * 4), 104, 52, 0, 0,
               std::uint8_t(v)});
    }
    place({Kind::bush, 24, 14, 112, 96, 80, bare, 0, 0});
    for (int v = 0; v < 5; ++v) {
        place({Kind::cone_bush, std::uint16_t(14 + v * 5), std::uint16_t(5 + v * 2), 60, 106, 62, 0, 0, std::uint8_t(v)});
    }
    row();
    for (int v = 0; v < 10; ++v) {
        place({Kind::grass, std::uint16_t(4 + v * 3), std::uint16_t(2 + v / 2), std::uint8_t(150 - v * 9),
               std::uint8_t(158 - v * 5), std::uint8_t(64 + v),
               std::uint8_t(v % 3 == 0 ? flowering : 0), std::uint8_t(v % 6), std::uint8_t(v)});
    }
    place({Kind::grass, 14, 4, 176, 158, 104, bare, 0, 0});
    for (int v = 0; v < 6; ++v) {
        place({Kind::reed, std::uint16_t(10 + v * 4), std::uint16_t(2 + v / 2), 112, 136, 60, 0, 0, std::uint8_t(v)});
    }
    row();
    // Water plants: under the water, and floating on it.
    for (int v = 0; v < 6; ++v) {
        place({Kind::waterweed, std::uint16_t(5 + v * 3), std::uint16_t(2 + v / 2), 80, 120, 64, 0, 0, std::uint8_t(v)});
    }
    for (int v = 0; v < 6; ++v) {
        place({Kind::pad, 0, std::uint16_t(3 + v / 2), 80, 136, 64, std::uint8_t(v % 2 ? flowering : 0), std::uint8_t(v),
               std::uint8_t(v)});
    }
    row();
    // Fire: a flame's four frames at two sizes, and smoke as it thins.
    for (int v = 0; v < 8; ++v) {
        place({Kind::flame, std::uint16_t(v < 4 ? 14 : 28), 0, 0, 0, 0, 0, 0, std::uint8_t(v % 4)});
    }
    for (int v = 0; v < 6; ++v) {
        place({Kind::smoke, 0, std::uint16_t(4 + v * 2), 0, 0, 0, 0, 0, std::uint8_t(v / 2)});
    }
    const char* path = argc > 1 ? argv[1] : "sprites.png";
    if (!phd::write_png(path, width, height, rgb)) {
        std::fprintf(stderr, "could not write %s\n", path);
        return 1;
    }
    std::printf("wrote %s\n", path);
    return 0;
}
