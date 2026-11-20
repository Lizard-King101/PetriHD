// Paints a contact sheet of plant sprites, to judge the painter without
// opening a window: `petrihd-sprites sheet.png`.
#include <cstdio>
#include <vector>

#include "plant_sprites.hpp"
#include "png.hpp"

int main(int argc, char** argv) {
    using namespace art::sprites;
    const int width = 640, height = 560;
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
                if (p[3]) {
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
    // The pieces of trees: crowns, trunks, and a conifer's boughs from the hem to the tip.
    for (int v = 0; v < 4; ++v) {
        place({Kind::crown, 0, std::uint16_t(18 + v * 12), 70, 124, 52, 0, 0, std::uint8_t(v)});
    }
    place({Kind::crown, 0, 40, 112, 98, 86, bare, 0, 0});
    place({Kind::crown, 0, 30, 180, 120, 50, 0, 0, 1});
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
        place({Kind::grass, std::uint16_t(4 + v * 3), std::uint16_t(2 + v / 2), std::uint8_t(v * 2), 0, 0,
               std::uint8_t(v % 3 == 0 ? flowering : 0), std::uint8_t(v % 6), std::uint8_t(v)});
    }
    place({Kind::grass, 14, 4, 0, 0, 0, bare, 0, 0});
    for (int v = 0; v < 6; ++v) {
        place({Kind::reed, std::uint16_t(10 + v * 4), std::uint16_t(2 + v / 2), 6, 0, 0, 0, 0, std::uint8_t(v)});
    }
    const char* path = argc > 1 ? argv[1] : "sprites.png";
    if (!phd::write_png(path, width, height, rgb)) {
        std::fprintf(stderr, "could not write %s\n", path);
        return 1;
    }
    std::printf("wrote %s\n", path);
    return 0;
}
