// petrihd: the windowed world: the terrain and its weather, pan and zoom by
// mouse or touch, the control bar, the cell inspector and the budget HUD.
//
//   petrihd [--seed N] [--size WxH] [--load FILE] [--shot FILE.png]
//           [--ticks N]                (run N ticks before showing)
//           [--frames N --speed 0-3]   (run N frames, print timings, quit)
//           [--zoom Z] [--at X,Y]      (start zoom, 1-256, and focus cell)
//           [--flat] [--relief K]      (no tilted view; display cells per metre)
//           [--pixel N]                (screen pixels per art pixel, default 2)
//
// Keys: space pause, 1-4 speed, H HUD, S save, F12 screenshot, Esc quit.
// T locks the camera top-down, G turns the tilted view off and on, O puts
// the test tree and creature at the focus.
// Long-press or right-click a cell to inspect it.

#include <SDL.h>

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "batch.hpp"
#include "file.hpp"
#include "font.hpp"
#include "gl.hpp"
#include "png.hpp"
#include "pixel_target.hpp"
#include "pointer.hpp"
#include "world.hpp"
#include "tilt_view.hpp"
#include "world_view.hpp"

namespace {

constexpr int bar_height = 32;
constexpr double ticks_per_second = 10.0;
// Pi time is guessed as PC time x 8 until the Pi has been measured.
constexpr double pi_factor = 8.0;
// 10 ticks/s using at most half of three cores: 150 core-ms per tick.
constexpr double pi_tick_budget_ms = 150.0;
constexpr double autosave_seconds = 300.0;

const gfx::Color white{255, 255, 255, 255}, dim{170, 180, 170, 255}, panel{16, 20, 18, 220},
    accent{120, 210, 140, 255}, warn{240, 150, 90, 255};

struct Speed {
    const char* label;
    double multiplier; // 0 = paused, < 0 = as fast as the frame allows
};
const Speed speeds[] = {{"|", 0}, {">", 1}, {">>", 4}, {">>>", -1}};

double now_seconds() { return double(SDL_GetPerformanceCounter()) / double(SDL_GetPerformanceFrequency()); }

std::size_t resident_bytes() {
#ifdef __linux__
    if (FILE* f = std::fopen("/proc/self/statm", "r")) {
        unsigned long pages = 0, resident = 0;
        const int n = std::fscanf(f, "%lu %lu", &pages, &resident);
        std::fclose(f);
        if (n == 2) {
            return std::size_t(resident) * 4096;
        }
    }
#endif
    return 0;
}

std::string format(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
std::string format(const char* fmt, ...) {
    char buffer[256];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    return buffer;
}

std::string clock_label(std::uint64_t tick) {
    const auto day = tick / phd::World::ticks_per_day;
    const char* seasons[] = {"SPRING", "SUMMER", "AUTUMN", "WINTER"};
    const auto season = seasons[(day % phd::World::days_per_year) / (phd::World::days_per_year / 4)];
    const int minutes = int((tick % phd::World::ticks_per_day) * 24 * 60 / phd::World::ticks_per_day);
    return format("DAY %" PRIu64 "  %s  %02d:%02d", day + 1, season, minutes / 60, minutes % 60);
}

using gfx::Camera;

constexpr float panel_width = 260;
constexpr double long_press_seconds = 0.45;
constexpr double bake_budget_seconds = 0.003;

const char* substrate_name(phd::Substrate s) {
    static const char* names[] = {"LOAM", "SAND", "CLAY", "PEAT", "ROCK", "GRAVEL"};
    return names[int(s)];
}

// One plant in a cell, for the inspector: what it is, how tall, how old,
// and two of its genes.
std::string plant_line(const phd::World& world, std::size_t cell, int slot) {
    const phd::Plant* p = world.occupant(cell, slot);
    if (!p || (p->slot == phd::whole_cell && slot > 0)) {
        return slot == 0 ? "NO PLANTS" : "";
    }
    const char* kind = p->slot == phd::whole_cell ? "TREE" : phd::woody(p->genome) ? "SHRUB"
                       : p->genome.gene[phd::gene_flood] > 160                       ? "REED"
                       : p->genome.gene[phd::gene_lifespan] < 48                     ? "HERB"
                                                                                     : "GRASS";
    return format("%s %.1fM %dD DRY%d COLD%d", kind, p->height / 10.0, p->age / phd::passes_per_day,
                  p->genome.gene[phd::gene_drought], p->genome.gene[phd::gene_cold]);
}

const char* water_name(phd::Water kind) {
    switch (kind) {
    case phd::Water::ocean: return "SEA";
    case phd::Water::lake: return "LAKE";
    case phd::Water::river: return "RIVER";
    default: return "NONE";
    }
}

bool screenshot(const std::string& path, int width, int height) {
    std::vector<std::uint8_t> rgba(std::size_t(width) * height * 4), rgb(std::size_t(width) * height * 3);
    gl::PixelStorei(gl::PACK_ALIGNMENT, 1);
    gl::ReadPixels(0, 0, width, height, gl::RGBA, gl::UNSIGNED_BYTE, rgba.data());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto* s = &rgba[(std::size_t(height - 1 - y) * width + x) * 4];
            auto* d = &rgb[(std::size_t(y) * width + x) * 3];
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
        }
    }
    return phd::write_png(path, width, height, rgb);
}

SDL_GLContext create_context(SDL_Window*& window, int width, int height) {
    // GLES 2 first, as on the Pi; desktop GL 2.1 where there is no GLES.
    const int profiles[][3] = {{SDL_GL_CONTEXT_PROFILE_ES, 2, 0}, {0, 2, 1}};
    for (const auto& profile : profiles) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, profile[0]);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, profile[1]);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, profile[2]);
        window = SDL_CreateWindow("Petri HD", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height,
                                  SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI);
        if (!window) {
            continue;
        }
        if (SDL_GLContext context = SDL_GL_CreateContext(window)) {
            return context;
        }
        SDL_DestroyWindow(window);
        window = nullptr;
    }
    return nullptr;
}

} // namespace

int main(int argc, char** argv) {
    phd::WorldConfig config;
    int width = 800, height = 480;
    std::string load, shot, save_path = "petrihd.phdw";
    bool quit_after_shot = false;
    int bench_frames = 0, start_speed = 1;
    long long skip_ticks = 0;
    float start_zoom = 2, relief = 10.0f / float(phd::cell_decimetres), at_x = -1, at_y = -1;
    bool tilt_enabled = true;
    int pixel = 2;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool more = i + 1 < argc;
        if (arg == "--seed" && more) {
            config.seed = std::uint32_t(std::strtoul(argv[++i], nullptr, 0));
        } else if (arg == "--size" && more && std::sscanf(argv[++i], "%dx%d", &width, &height) == 2) {
        } else if (arg == "--load" && more) {
            load = argv[++i];
            save_path = load;
        } else if (arg == "--zoom" && more) {
            start_zoom = std::clamp(float(std::atof(argv[++i])), 1.0f, 256.0f);
        } else if (arg == "--at" && more && std::sscanf(argv[++i], "%f,%f", &at_x, &at_y) == 2) {
        } else if (arg == "--pixel" && more) {
            pixel = std::clamp(std::atoi(argv[++i]), 1, 8);
        } else if (arg == "--flat") {
            tilt_enabled = false;
        } else if (arg == "--relief" && more) {
            relief = float(std::atof(argv[++i]));
        } else if (arg == "--ticks" && more) {
            skip_ticks = std::atoll(argv[++i]);
        } else if (arg == "--frames" && more) {
            bench_frames = std::atoi(argv[++i]);
        } else if (arg == "--speed" && more) {
            start_speed = std::clamp(std::atoi(argv[++i]), 0, 3);
        } else if (arg == "--shot" && more) {
            shot = argv[++i];
            quit_after_shot = true;
        } else {
            std::fprintf(stderr, "usage: %s [--seed N] [--size WxH] [--load FILE] [--shot FILE.png] "
                                 "[--frames N --speed 0-3]\n", argv[0]);
            return 2;
        }
    }

    // Touches arrive as finger events only, and the mouse as mouse events only.
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0); // the world draws into its own target, which has one
    SDL_Window* window = nullptr;
    SDL_GLContext context = create_context(window, width, height);
    if (!context) {
        std::fprintf(stderr, "no GLES 2 or GL 2.1 context: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetSwapInterval(bench_frames ? 0 : 1);
    if (const char* missing = gl::load()) {
        std::fprintf(stderr, "missing %s\n", missing);
        return 1;
    }
    const std::string gl_version = reinterpret_cast<const char*>(gl::GetString(gl::VERSION));

    gfx::Batch batch;
    gfx::Font font;
    if (!batch.init() || !font.init()) {
        return 1;
    }

    phd::World world(config);
    if (!load.empty()) {
        std::vector<std::uint8_t> data;
        if (!phd::read_file(load, data) || !world.load(data.data(), data.size())) {
            std::fprintf(stderr, "could not load %s\n", load.c_str());
            return 1;
        }
    }
    for (long long t = 0; t < skip_ticks; ++t) {
        world.step();
    }
    gfx::WorldView view;
    if (!view.init(world)) {
        return 1;
    }

    gfx::TiltView tilt(relief);
    if (!tilt.init(world)) {
        return 1;
    }

    const int view_height = height - bar_height;
    // The world is drawn at 1/pixel of the window's size (its logical size,
    // so a scaled desktop and the Pi's panel give the same pixel grid), then
    // scaled up by whole pixels.
    const int art_w = width / pixel, art_h = (view_height + pixel - 1) / pixel;
    gfx::PixelTarget target;
    if (!target.init(art_w, art_h)) {
        return 1;
    }
    Camera camera;
    camera.cx = at_x >= 0 ? at_x : float(world.width()) / 2;
    camera.cy = at_y >= 0 ? at_y : float(world.height()) / 2;
    const auto max_zoom = [&] { return tilt_enabled ? 256.0f : 32.0f; };
    camera.zoom = camera.target_zoom = std::min(start_zoom, max_zoom());
    camera.focus_z = tilt.ground(camera.cx, camera.cy);
    const auto clamp_camera = [&] { camera.clamp(float(width), float(view_height), world.width(), world.height()); };
    clamp_camera();
    const auto zoom_at = [&](float zoom, float sx, float sy) {
        camera.zoom_about(zoom, sx, sy, float(width), float(view_height));
        clamp_camera();
    };
    tilt.place_test_scene(world, camera.cx, camera.cy);

    int speed = start_speed;
    long long tilt_frame = 0;
    bool hud = true, running = true;
    double accumulator = 0, tick_ms = 0, frame_ms = 0, fps = 60, last_frame = now_seconds();
    double last_save = last_frame;
    std::string status;
    double status_until = 0;
    const auto save = [&] {
        const bool ok = phd::write_file_atomic(save_path, world.save());
        status = ok ? "SAVED " + save_path : "SAVE FAILED";
        status_until = now_seconds() + 2;
        last_save = now_seconds();
    };

    // Pointers: one drags, two pinch.
    std::map<SDL_FingerID, ui::Pointer> held;
    int inspected = -1;          // the cell in the inspector, or -1
    double press_time = 0;       // when the single pointer went down
    float press_x = 0, press_y = 0;
    bool press_moved = false, press_fired = false;
    const auto cell_at = [&](float px, float py) {
        float gx, gy;
        camera.to_ground(px, py, float(width), float(view_height), gx, gy);
        const int cx = int(std::floor(gx)), cy = int(std::floor(gy));
        return cx < 0 || cy < 0 || cx >= world.width() || cy >= world.height() ? -1 : cy * world.width() + cx;
    };
    std::vector<double> frame_samples;
    // Where zooming is centred: the pointer that last scrolled or pinched.
    float zoom_anchor_x = float(width) / 2, zoom_anchor_y = float(view_height) / 2;
    float pinch_zoom = 0;
    float pinch_start = 0;
    bool dragging = false;
    float drag_x = 0, drag_y = 0;
    const auto button_rect = [&](int index, float& x0, float& x1) {
        x0 = 4.0f + float(index) * 44.0f;
        x1 = x0 + 40.0f;
    };

    while (running) {
        const double frame_start = now_seconds();
        int window_w = width, window_h = height;
        SDL_GetWindowSize(window, &window_w, &window_h);
        const float input_x = float(width) / float(std::max(1, window_w));
        const float input_y = float(height) / float(std::max(1, window_h));
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_RIGHT &&
                       float(event.button.y) * input_y < float(view_height)) {
                inspected = cell_at(float(event.button.x) * input_x, float(event.button.y) * input_y);
            } else if (event.type == SDL_KEYDOWN) {
                switch (event.key.keysym.sym) {
                case SDLK_ESCAPE: running = false; break;
                case SDLK_SPACE: speed = speed == 0 ? 1 : 0; break;
                case SDLK_1: speed = 0; break;
                case SDLK_2: speed = 1; break;
                case SDLK_3: speed = 2; break;
                case SDLK_4: speed = 3; break;
                case SDLK_h: hud = !hud; break;
                case SDLK_t: camera.top_down = !camera.top_down; break;
                case SDLK_g:
                    tilt_enabled = !tilt_enabled;
                    camera.target_zoom = std::min(camera.target_zoom, max_zoom());
                    break;
                case SDLK_o: tilt.place_test_scene(world, camera.cx, camera.cy); break;
                case SDLK_s: save(); break;
                case SDLK_F12: shot = format("petrihd-%" PRIu64 ".png", world.tick()); break;
                default: break;
                }
            }
            const auto p = ui::pointer_from(event, width, height, input_x, input_y);
            if (p.type == ui::Pointer::wheel && p.y < view_height) {
                // Proportional to how far the wheel turned: a notch is about
                // x1.4, so two notches double the zoom. Smooth-scrolling wheels
                // and touchpads send fractions of a notch.
                const float notches = std::clamp(p.scroll, -3.0f, 3.0f);
                camera.target_zoom = std::clamp(camera.target_zoom * std::pow(2.0f, notches * 0.5f), 1.0f, max_zoom());
                zoom_anchor_x = p.x;
                zoom_anchor_y = p.y;
            } else if (p.type == ui::Pointer::down) {
                if (p.y >= view_height) {
                    for (int i = 0; i < 4; ++i) {
                        float x0, x1;
                        button_rect(i, x0, x1);
                        if (p.x >= x0 && p.x < x1) {
                            speed = i;
                        }
                    }
                    continue;
                }
                if (inspected >= 0 && p.x >= float(width) - panel_width) {
                    inspected = -1; // tapping the inspector closes it
                    continue;
                }
                held[p.id] = p;
                dragging = held.size() == 1;
                press_time = held.size() == 1 ? now_seconds() : 0;
                press_x = p.x;
                press_y = p.y;
                press_moved = false;
                press_fired = false;
                drag_x = p.x;
                drag_y = p.y;
                if (held.size() == 2) {
                    const auto a = held.begin()->second, b = std::next(held.begin())->second;
                    pinch_start = std::hypot(a.x - b.x, a.y - b.y);
                    pinch_zoom = camera.target_zoom;
                }
            } else if (p.type == ui::Pointer::move && held.count(p.id)) {
                held[p.id] = p;
                if (std::hypot(p.x - press_x, p.y - press_y) > 8) {
                    press_moved = true;
                }
                if (held.size() == 1 && dragging) {
                    camera.cx -= (p.x - drag_x) / camera.zoom;
                    camera.cy -= (p.y - drag_y) / (camera.zoom * camera.sin_pitch());
                    drag_x = p.x;
                    drag_y = p.y;
                    clamp_camera();
                } else if (held.size() == 2 && pinch_start > 0) {
                    const auto a = held.begin()->second, b = std::next(held.begin())->second;
                    // The zoom follows the fingers' spread, about their midpoint.
                    const float distance = std::hypot(a.x - b.x, a.y - b.y);
                    camera.target_zoom = std::clamp(pinch_zoom * distance / pinch_start, 1.0f, max_zoom());
                    zoom_anchor_x = (a.x + b.x) / 2;
                    zoom_anchor_y = (a.y + b.y) / 2;
                }
            } else if (p.type == ui::Pointer::up) {
                if (held.size() == 1 && held.count(p.id) && !press_moved && !press_fired) {
                    inspected = -1; // a short tap on the world closes the inspector
                }
                held.erase(p.id);
                dragging = false;
                if (held.size() == 1) {
                    drag_x = held.begin()->second.x;
                    drag_y = held.begin()->second.y;
                    dragging = true;
                }
            }
        }

        // A long press without moving inspects the cell under it.
        if (held.size() == 1 && press_time > 0 && !press_moved && !press_fired &&
            now_seconds() - press_time > long_press_seconds) {
            inspected = cell_at(press_x, press_y);
            press_fired = true;
        }

        // Smooth zoom toward the target, about the pointer that asked for it.
        if (camera.zoom != camera.target_zoom) {
            const float sx = zoom_anchor_x, sy = std::min(zoom_anchor_y, float(view_height));
            float next = camera.zoom * std::pow(camera.target_zoom / camera.zoom, 0.25f);
            if (std::fabs(next / camera.target_zoom - 1) < 0.002f) {
                next = camera.target_zoom;
            }
            zoom_at(next, sx, sy);
        }

        // Tilting turns about the ground at the focus, not sea level. When the
        // pivot's height follows the ground, it slides along the line of sight
        // too, so the picture doesn't move: panning over hills then tracks the
        // finger in every direction.
        {
            const float rise = (tilt.ground(camera.cx, camera.cy) - camera.focus_z) * 0.2f;
            camera.focus_z += rise;
            if (camera.sin_pitch() > 0.01f) {
                camera.cy += rise * camera.cos_pitch() / camera.sin_pitch();
            }
        }

        // Simulation.
        const double dt = std::min(0.25, frame_start - last_frame);
        last_frame = frame_start;
        int ticks = 0;
        if (speeds[speed].multiplier > 0) {
            accumulator += dt * ticks_per_second * speeds[speed].multiplier;
            ticks = int(accumulator);
            accumulator -= ticks;
        } else {
            accumulator = 0;
        }
        const double sim_start = now_seconds();
        const bool flat_out = speeds[speed].multiplier < 0;
        int done = 0;
        while (flat_out ? now_seconds() - sim_start < 0.010 : done < ticks) {
            world.step();
            ++done;
        }
        if (done) {
            const double ms = (now_seconds() - sim_start) * 1000.0 / done;
            tick_ms = tick_ms == 0 ? ms : tick_ms * 0.9 + ms * 0.1;
        }
        view.refresh(world, camera, width, view_height, bake_budget_seconds, tilt_enabled && camera.tilted());
        if (frame_start - last_save > autosave_seconds) {
            save();
        }

        // Drawing.
        int drawable_w = width, drawable_h = height;
        SDL_GL_GetDrawableSize(window, &drawable_w, &drawable_h);
        const bool tilted = tilt_enabled && camera.tilted();
        const Camera art_camera = camera.for_target(float(pixel));
        target.begin();
        if (tilted) {
            if (++tilt_frame % 15 == 0) {
                tilt.refresh(world, 2);
            }
            tilt.draw(world, art_camera, view.terrain(), font, art_w, art_h, art_h, frame_start);
        } else {
            batch.begin(art_w, art_h);
            view.draw(batch, font, world, art_camera, art_w, art_h, frame_start);
            batch.end();
        }
        const int world_draws = batch.draw_calls() + (tilted ? tilt.draw_calls() : 0);
        target.end(drawable_w, drawable_h);
        gl::ClearColor(0.04f, 0.05f, 0.05f, 1);
        gl::Clear(gl::COLOR_BUFFER_BIT);
        batch.begin(width, height);
        // The framebuffer's rows run bottom to top.
        batch.quad(target.texture(), 0, 0, float(art_w * pixel), float(art_h * pixel), 0, 1, 1, 0, white);
        if (inspected >= 0) {
            // Outline the cell, projected through the camera at its ground height.
            const float x = float(inspected % world.width()), y = float(inspected / world.width());
            const gfx::Color mark{255, 240, 120, 230};
            float px[4], py[4];
            const float corner_x[4] = {x, x + 1, x + 1, x}, corner_y[4] = {y, y, y + 1, y + 1};
            for (int k = 0; k < 4; ++k) {
                const float z = tilted ? tilt.ground(corner_x[k], corner_y[k]) : camera.focus_z;
                camera.to_screen(corner_x[k], corner_y[k], z, float(width), float(view_height), px[k], py[k]);
            }
            font.rect(batch, px[0] - 1, std::min(py[0], py[1]) - 1, px[1] + 1, std::min(py[0], py[1]) + 1, mark);
            font.rect(batch, px[3] - 1, std::max(py[2], py[3]) - 1, px[2] + 1, std::max(py[2], py[3]) + 1, mark);
            font.rect(batch, px[0] - 1, py[0], px[0] + 1, py[3], mark);
            font.rect(batch, px[1] - 1, py[1], px[1] + 1, py[2], mark);
        }

        // The cell inspector.
        if (inspected >= 0) {
            const auto i = std::size_t(inspected);
            const float px = float(width) - panel_width;
            const int t = world.temperature(i);
            const std::string lines[] = {
                phd::biome_name(world.biome(i)),
                format("CELL %d, %d", inspected % world.width(), inspected / world.width()),
                format("HEIGHT %.1f M  %s", world.elevation(i) / 10.0, substrate_name(world.substrate(i))),
                format("WATER %s %.1f M", water_name(world.water(i)), world.water_depth_dm(i) / 10.0),
                format("TEMP %s%d.%d C", t < 0 ? "-" : "", std::abs(t) / 10, std::abs(t) % 10),
                format("SOIL WATER %d", world.soil_water(i)),
                format("MOISTURE %d", world.moisture(i)),
                format("SNOW %d", world.snow(i)),
                format("FLOW %u", world.flow(i)),
                format("NUTRIENTS %d  LITTER %d", world.nutrients(i), world.litter(i)),
                plant_line(world, i, 0),
                plant_line(world, i, 1),
                plant_line(world, i, 2),
                plant_line(world, i, 3),
            };
            font.rect(batch, px, 0, float(width), float(view_height), panel);
            int row = 0;
            for (const auto& line : lines) {
                font.text(batch, px + 10, 12 + float(row) * 20, line, 2, row == 0 ? accent : white);
                ++row;
            }
            font.text(batch, px + 10, float(view_height) - 22, "TAP HERE TO CLOSE", 2, dim);
        }

        // The control bar.
        font.rect(batch, 0, float(view_height), float(width), float(height), {12, 15, 14, 255});
        for (int i = 0; i < 4; ++i) {
            float bx0, bx1;
            button_rect(i, bx0, bx1);
            font.rect(batch, bx0, float(view_height) + 4, bx1, float(height) - 4,
                      i == speed ? gfx::Color{40, 80, 55, 255} : gfx::Color{30, 36, 33, 255});
            const float tw = font.width(speeds[i].label, 2);
            font.text(batch, (bx0 + bx1 - tw) / 2, float(view_height) + 9, speeds[i].label, 2,
                      i == speed ? accent : dim);
        }
        font.text(batch, 188, float(view_height) + 9, clock_label(world.tick()), 2, white);
        const auto right = format("SEED %u", world.seed());
        font.text(batch, float(width) - font.width(right, 2) - 8, float(view_height) + 9, right, 2, dim);

        // The budget HUD.
        if (hud) {
            const double pi_ms = tick_ms * pi_factor;
            const std::string lines[] = {
                format("TICK %.3f MS  PI~%.2f/%.0f MS", tick_ms, pi_ms, pi_tick_budget_ms),
                format("FRAME %.2f MS  %.0f FPS  ZOOM %.0f  PITCH %.0f%s", frame_ms, fps, camera.zoom,
                       camera.pitch(), camera.top_down ? " LOCK" : tilt_enabled ? "" : " FLAT"),
                format("DRAWS %d  TRIS %d  BAKE %.2f MS  %dX%d Z%d", world_draws + batch.draw_calls(),
                       tilted ? tilt.triangles() : 0, view.bake_ms(), art_w, art_h, target.depth_bits()),
                format("SIM %.2f MB  RSS %.1f MB", world.memory_bytes() / 1048576.0, resident_bytes() / 1048576.0),
                format("FRONTS %zu  WIND %d  TREES %d  %s", world.fronts().size(), world.wind_speed(),
                       tilted ? tilt.trees() : 0, gl::is_es() ? "GLES" : "GL"),
            };
            font.rect(batch, 4, 4, 4 + 6 * 2 * 44 + 8, 4 + 5 * 18 + 6, panel);
            for (int i = 0; i < 5; ++i) {
                font.text(batch, 10, 10 + float(i) * 18, lines[i], 2, i == 0 && pi_ms > pi_tick_budget_ms ? warn : white);
            }
        }
        if (now_seconds() < status_until) {
            font.text(batch, float(width) - font.width(status, 2) - 10, 10, status, 2, accent);
        }
        batch.end();

        if (!shot.empty()) {
            const bool ok = screenshot(shot, drawable_w, drawable_h);
            std::printf("%s %s\n", ok ? "wrote" : "could not write", shot.c_str());
            running = running && !quit_after_shot;
            shot.clear();
        }
        frame_ms = (now_seconds() - frame_start) * 1000.0;
        if (bench_frames) {
            frame_samples.push_back(frame_ms);
            if (int(frame_samples.size()) >= bench_frames) {
                std::vector<double> sorted = frame_samples;
                std::sort(sorted.begin(), sorted.end());
                double sum = 0;
                for (const double v : sorted) {
                    sum += v;
                }
                std::printf("frames=%zu frame_ms_mean=%.2f frame_ms_p99=%.2f frame_ms_max=%.2f tick_ms=%.3f bake_ms=%.2f "
                            "ticks=%" PRIu64 "\n",
                            sorted.size(), sum / double(sorted.size()), sorted[sorted.size() * 99 / 100], sorted.back(),
                            tick_ms, view.bake_ms(), world.tick());
                running = false;
            }
        }
        SDL_GL_SwapWindow(window);
        const double interval = now_seconds() - frame_start;
        if (interval > 0) {
            fps = fps * 0.95 + (1.0 / interval) * 0.05;
        }
    }

    std::printf("%s  tick=%" PRIu64 " fingerprint=%016" PRIx64 "\n", gl_version.c_str(), world.tick(),
                world.fingerprint());
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
