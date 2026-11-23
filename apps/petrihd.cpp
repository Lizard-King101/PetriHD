// petrihd: the windowed world: the terrain and its weather, pan and zoom by
// mouse or touch, the control bar, the cell inspector and the budget HUD.
//
//   petrihd [--seed N] [--size WxH] [--load FILE] [--shot FILE.png]
//           [--ticks N]                (run N ticks before showing)
//           [--frames N --speed 0-3]   (run N frames, print timings, quit)
//           [--zoom Z] [--at X,Y]      (start zoom, 1-256, and focus cell)
//           [--flat] [--relief K]      (no tilted view; display cells per metre)
//           [--performance desktop|low] [--pixel N]
//           [--fullscreen]             (use the current desktop resolution; F11 toggles)
//           [--shoreline-study]        (open the spring shoreline art study)
//           [--dev-map]                (a small hand-laid world)
//           [--brains MODE]            (evolving, frozen, shuffled or neutral)
//           [--founders FILE]          (founder brains bred by petrihd-nursery)
//           [--classic]                (start with the five designed founders, not fifty random bodies)
//           [--bodies SEED]            (which fifty random bodies; by default the world's seed)
//           [--phase SECONDS]          (how far into the study's animation a shot is taken)
//
// Keys: space pause, 1-4 speed, H HUD, K kinds alive, X tree X-ray, S save, F12 screenshot, Esc quit.
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
#include <optional>
#include <string>
#include <vector>

#include "batch.hpp"
#include "file.hpp"
#include "font.hpp"
#include "gl.hpp"
#include "png.hpp"
#include "pixel_target.hpp"
#include "pointer.hpp"
#include "taxonomy.hpp"
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
                                  SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
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
    bool start_fullscreen = false;
    std::string load, shot;
    bool quit_after_shot = false;
    bool shoreline_study = false, dev_map = false;
    std::uint32_t inspect_id = 0; // an animal to open the inspector on, and follow
    int animals = -1; // founders to put into a new world; by default, some on the development map
    std::string founders; // a file of founder brains from the nursery; by default the hand-wired ones
    bool classic = false; // the five designed founders
    std::optional<std::uint32_t> body_seed;
    std::optional<phd::BrainMode> brains;
    double start_phase = 1.0; // seconds into the study's animation: the pose a shot is taken in
    int bench_frames = 0;
    std::optional<int> start_speed;
    std::optional<long long> skip_ticks;
    std::optional<float> start_zoom;
    float relief = 10.0f / float(phd::cell_decimetres), at_x = -1, at_y = -1;
    bool tilt_enabled = true, start_xray = false, start_top_down = false;
    bool low_performance = false;
    std::optional<int> requested_pixel;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool more = i + 1 < argc;
        if (arg == "--seed" && more) {
            config.seed = std::uint32_t(std::strtoul(argv[++i], nullptr, 0));
        } else if (arg == "--size" && more && std::sscanf(argv[++i], "%dx%d", &width, &height) == 2) {
        } else if (arg == "--fullscreen") {
            start_fullscreen = true;
        } else if (arg == "--performance" && more) {
            const std::string profile = argv[++i];
            if (profile != "desktop" && profile != "low") {
                std::fprintf(stderr, "unknown performance profile '%s' (expected desktop or low)\n", argv[i]);
                return 2;
            }
            low_performance = profile == "low";
        } else if (arg == "--load" && more) {
            load = argv[++i];
        } else if (arg == "--shoreline-study") {
            shoreline_study = true;
        } else if (arg == "--dev-map") {
            // A small hand-laid world for trying things in every kind of
            // place, opened close up with the test plant and animals.
            dev_map = shoreline_study = true;
            config.layout = phd::Layout::development;
            config.width = 128;
            config.height = 96;
        } else if (arg == "--zoom" && more) {
            start_zoom = std::clamp(float(std::atof(argv[++i])), 1.0f, 256.0f);
        } else if (arg == "--inspect" && more) {
            inspect_id = std::uint32_t(std::atoi(argv[++i]));
        } else if (arg == "--animals" && more) {
            animals = std::atoi(argv[++i]);
        } else if (arg == "--founders" && more) {
            founders = argv[++i];
            classic = true; // the file holds brains for the five
        } else if (arg == "--classic") {
            classic = true;
        } else if (arg == "--bodies" && more) {
            body_seed = std::uint32_t(std::strtoul(argv[++i], nullptr, 0));
        } else if (arg == "--brains" && more) {
            brains = phd::parse_brain_mode(argv[++i]);
            if (!brains) {
                std::fprintf(stderr, "unknown brain mode '%s' (expected evolving, frozen, shuffled or neutral)\n", argv[i]);
                return 2;
            }
        } else if (arg == "--at" && more && std::sscanf(argv[++i], "%f,%f", &at_x, &at_y) == 2) {
        } else if (arg == "--pixel" && more) {
            requested_pixel = std::clamp(std::atoi(argv[++i]), 1, 8);
        } else if (arg == "--tree-xray") {
            start_xray = true;
        } else if (arg == "--top-down") {
            start_top_down = true;
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
        } else if (arg == "--phase" && more) {
            start_phase = std::atof(argv[++i]);
        } else if (arg == "--shot" && more) {
            shot = argv[++i];
            quit_after_shot = true;
        } else {
            std::fprintf(stderr, "usage: %s [--seed N] [--size WxH] [--fullscreen] [--load FILE] [--shot FILE.png] "
                                 "[--performance desktop|low] [--pixel N] [--frames N --speed 0-3] "
                                 "[--shoreline-study] [--dev-map] [--brains MODE]\n", argv[0]);
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
    // Keep the requested logical grid on backends that report a smaller
    // window in desktop coordinates. Resizes and pointer input use the same units.
    int window_w, window_h;
    SDL_GetWindowSize(window, &window_w, &window_h);
    const float input_x = float(width) / std::max(1, window_w);
    const float input_y = float(height) / std::max(1, window_h);
    if (start_fullscreen && SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP) < 0) {
        std::fprintf(stderr, "fullscreen unavailable: %s\n", SDL_GetError());
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
    // What lives here is named from outside: families, genera, species and
    // subspecies, founded as they are met. The bodies a world starts from
    // are met first, so that their names do not depend on who is seen first.
    phd::Taxonomy taxa;
    constexpr int genera = 10, relatives = 5, family = 6;
    const auto bodies = phd::random_bodies(body_seed.value_or(world.config().seed), genera, relatives);
    if (classic) {
        for (int k = 0; k < phd::founder_kinds; ++k) taxa.classify(phd::founder_genome(phd::FounderKind(k)));
    } else {
        for (const auto& body : bodies) taxa.classify(body);
    }
    // Founders, for a world that has no animals yet.
    const bool wants_founders = world.animals().empty() && (animals > 0 || (animals < 0 && dev_map && !world.has_had_animals()));
    // The hunters among the fifty come later, a pair of each, with brains
    // from the hunting school: once, when the world reaches that year.
    const std::uint64_t hunters_arrive = std::uint64_t(phd::hunters_arrive_year) * phd::World::ticks_per_year;
    bool hunters_due = !classic && world.tick() < hunters_arrive && (wants_founders || !load.empty());
    gfx::CreatureActions actions;
    const auto step = [&] {
        world.step(&actions);
        if (!hunters_due || world.tick() != hunters_arrive) return;
        hunters_due = false;
        std::size_t next = 0;
        int arrived = 0;
        for (const auto& body : bodies) {
            if (phd::diet_of(body) != phd::Diet::carnivore) continue;
            const auto& minds = phd::schooled_hunters();
            arrived += world.settle(body, phd::hunters_each, minds[next++ % minds.size()]);
        }
        std::printf("year %d: %d hunters arrive\n", phd::hunters_arrive_year, arrived);
    };
    if (wants_founders && !classic) {
        // Fifty bodies nobody designed, a family of each that does not hunt, with the
        // reflexes wired by hand: those that eat some meat can strike a smaller body.
        for (const auto& body : bodies) {
            if (phd::diet_of(body) == phd::Diet::carnivore) continue;
            world.settle(body, family, phd::brain::founder(phd::diet_of(body) != phd::Diet::herbivore, true));
        }
    } else if (wants_founders) {
        auto founder_brains = phd::FounderBrains::hand_wired();
        std::vector<std::uint8_t> file;
        if (!founders.empty() && (!phd::read_file(founders, file) || !phd::FounderBrains::decode(file.data(), file.size(), founder_brains))) {
            std::fprintf(stderr, "could not read founder brains from %s\n", founders.c_str());
            return 1;
        }
        world.seed_animals(animals > 0 ? animals : 48, founder_brains);
    }
    if (brains) world.set_brain_mode(*brains);
    const long long initial_ticks = skip_ticks.value_or(shoreline_study && load.empty() ? phd::World::ticks_per_day / 2 : 0);
    for (long long t = 0; t < initial_ticks; ++t) {
        step();
    }
    gfx::WorldView view;
    if (!view.init(world)) {
        return 1;
    }

    actions.set_time(double(world.tick()));
    gfx::TiltView tilt(actions, relief);
    if (!tilt.init(world)) {
        return 1;
    }

    int view_height;
    const auto measure_window = [&] {
        SDL_GetWindowSize(window, &window_w, &window_h);
        width = std::max(1, int(std::lround(window_w * input_x)));
        height = std::max(1, int(std::lround(window_h * input_y)));
        view_height = std::max(1, height - bar_height);
    };
    measure_window();
    gfx::PixelTarget target;
    gfx::TerrainBatch terrain_batch;
    double bake_ms = 0;
    gl::GLint texture_limit, depth_limit;
    gl::GetIntegerv(gl::MAX_TEXTURE_SIZE, &texture_limit);
    gl::GetIntegerv(gl::MAX_RENDERBUFFER_SIZE, &depth_limit);
    const int target_limit = std::max(1, std::min(texture_limit, depth_limit));
    Camera camera;
    camera.tree_xray = start_xray;
    camera.top_down = start_top_down;
    if (dev_map && at_x < 0 && at_y < 0) {
        at_x = 62; // the level pad, with the wood to the west and the lake to the east
        at_y = 64;
    } else if (shoreline_study && at_x < 0 && at_y < 0) {
        at_x = 186;
        at_y = 73;
    }
    camera.cx = at_x >= 0 ? at_x : float(world.width()) / 2;
    camera.cy = at_y >= 0 ? at_y : float(world.height()) / 2;
    const auto max_zoom = [&] { return tilt_enabled ? 256.0f : 32.0f; };
    camera.zoom = camera.target_zoom = std::min(start_zoom.value_or(shoreline_study ? 64.0f : 2.0f), max_zoom());
    camera.focus_z = tilt.ground(camera.cx, camera.cy);
    const auto clamp_camera = [&] { camera.clamp(float(width), float(view_height), world.width(), world.height()); };
    clamp_camera();
    const auto zoom_at = [&](float zoom, float sx, float sy) {
        camera.zoom_about(zoom, sx, sy, float(width), float(view_height));
        clamp_camera();
    };
    // The study and the development map open with the test plant and animals
    // (O puts them at the focus of any world). On the development map they
    // start on its pad wherever the view opens, so a shot of any part of the
    // map finds them in the same places.
    if (shoreline_study) tilt.place_test_scene(world, dev_map ? 62.0f : camera.cx, dev_map ? 64.0f : camera.cy);

    int speed = start_speed.value_or(shoreline_study && !dev_map ? 0 : 1);
    long long tilt_frame = 0;
    bool hud = !shoreline_study, running = true;
    bool kinds_shown = !world.animals().empty(); // the list of what is alive: K
    struct KindRow { int living; std::string name; };
    std::vector<KindRow> kind_rows;
    int kinds_alive = 0;
    double kinds_counted = -10;
    double animation = start_phase; // seconds of sway and stride drawn so far: it stops while paused
    double accumulator = 0, tick_ms = 0, frame_ms = 0, fps = 60, last_frame = now_seconds();
    double last_save = last_frame;
    std::string status;
    double status_until = 0;
    const std::string save_path = load.empty() ? (shoreline_study ? "shoreline-study.phdw" : "petrihd.phdw") : load;
    const auto save = [&] {
        const bool ok = phd::write_file_atomic(save_path, world.save());
        status = ok ? "SAVED " + save_path : "SAVE FAILED";
        status_until = now_seconds() + 2;
        last_save = now_seconds();
    };

    // Pointers: one drags, two pinch.
    std::map<SDL_FingerID, ui::Pointer> held;
    int inspected = -1;          // the cell in the inspector, or -1
    std::uint32_t watched = inspect_id; // the animal in the inspector, or 0
    bool following = inspect_id != 0;   // whether the view keeps it in the middle
    const auto find_animal = [&](std::uint32_t id) -> const phd::Animal* {
        for (const phd::Animal& a : world.animals()) {
            if (a.id == id) {
                return &a;
            }
        }
        return nullptr;
    };
    // Where an animal is on the screen: its middle, as the close view draws it.
    const auto animal_on_screen = [&](const phd::Animal& a, float& sx, float& sy) {
        const float x = float(a.x) / 256.0f, y = float(a.y) / 256.0f;
        const gfx::creature::Body body = gfx::animal_body(a.genome, a.growth);
        float z = camera.focus_z;
        if (tilt_enabled && camera.tilted()) {
            z = tilt.animal_height(a);
        }
        camera.to_screen(x, y, z, float(width), float(view_height), sx, sy);
        return std::max(14.0f, (body.length + body.wing) * camera.zoom * 0.6f); // how near a tap must land
    };
    // Toward the middle of the screen from where it is drawn, which for a
    // flier or a fish is not over its ground.
    const auto follow_step = [&](const phd::Animal& a) {
        float sx, sy;
        animal_on_screen(a, sx, sy);
        // The middle of what the inspector's panel leaves clear.
        camera.cx += (sx - (float(width) - panel_width) / 2) / camera.zoom * 0.15f;
        camera.cy += (sy - float(view_height) / 2) / (camera.zoom * camera.sin_pitch()) * 0.15f;
        clamp_camera();
    };
    // Asked to inspect one from the start: open with it already in the middle.
    if (const phd::Animal* a = find_animal(watched)) {
        camera.cx = float(a->x) / 256.0f;
        camera.cy = float(a->y) / 256.0f;
        for (int k = 0; k < 60; ++k) {
            camera.focus_z = tilt.ground(camera.cx, camera.cy);
            follow_step(*a);
        }
    }
    const auto animal_at = [&](float px, float py) {
        std::uint32_t found = 0;
        float nearest = 1e9f;
        for (const phd::Animal& a : world.animals()) {
            float sx, sy;
            const float reach = animal_on_screen(a, sx, sy);
            const float d = std::hypot(sx - px, sy - py);
            if (d < reach && d < nearest) {
                nearest = d;
                found = a.id;
            }
        }
        return found;
    };
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
    int detail_ready_frame = 0;
    double bench_start = 0, detail_ready_ms = 0;
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

    const float xray_left = 180, xray_right = xray_left + font.width("TREE X-RAY", 2) + 12;
    while (running) {
        const double frame_start = now_seconds();
        if (bench_frames && frame_samples.empty()) bench_start = frame_start;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                const int old_width = width, old_height = view_height;
                measure_window();
                if (width != old_width || view_height != old_height) {
                    zoom_anchor_x *= float(width) / old_width;
                    zoom_anchor_y *= float(view_height) / old_height;
                    held.clear();
                    dragging = false;
                    press_time = 0;
                    clamp_camera();
                }
            } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_RIGHT &&
                       float(event.button.y) * input_y < float(view_height)) {
                inspected = cell_at(float(event.button.x) * input_x, float(event.button.y) * input_y);
            } else if (event.type == SDL_KEYDOWN) {
                switch (event.key.keysym.sym) {
                case SDLK_ESCAPE: running = false; break;
                case SDLK_F11:
                    if (!event.key.repeat) {
                        const bool fullscreen = SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN;
                        if (SDL_SetWindowFullscreen(window, fullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP) < 0) {
                            status = "FULLSCREEN UNAVAILABLE";
                            status_until = now_seconds() + 2;
                        }
                    }
                    break;
                case SDLK_SPACE: speed = speed == 0 ? 1 : 0; break;
                case SDLK_1: speed = 0; break;
                case SDLK_2: speed = 1; break;
                case SDLK_3: speed = 2; break;
                case SDLK_4: speed = 3; break;
                case SDLK_h: hud = !hud; break;
                case SDLK_k: kinds_shown = !kinds_shown; break;
                case SDLK_x: if (!event.key.repeat) camera.tree_xray = !camera.tree_xray; break;
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
                    if (p.x >= xray_left && p.x < xray_right) camera.tree_xray = !camera.tree_xray;
                    for (int i = 0; i < 4; ++i) {
                        float x0, x1;
                        button_rect(i, x0, x1);
                        if (p.x >= x0 && p.x < x1) {
                            speed = i;
                        }
                    }
                    continue;
                }
                if ((inspected >= 0 || watched) && p.x >= float(width) - panel_width) {
                    inspected = -1; // tapping the inspector closes it
                    watched = 0;
                    following = false;
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
                if (held.size() == 1 && dragging && press_moved) {
                    following = false; // dragging the view lets the animal go
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
                    // A short tap on an animal inspects it and follows it; on the
                    // one already inspected, lets it go or follows again;
                    // elsewhere, closes.
                    inspected = -1;
                    const std::uint32_t tapped = animal_at(p.x, p.y);
                    following = tapped && tapped == watched ? !following : tapped != 0;
                    watched = tapped;
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
            watched = 0;
            following = false;
            press_fired = true;
        }

        // The watched animal: gone if it has died; kept in the middle if followed.
        if (watched) {
            if (const phd::Animal* a = find_animal(watched)) {
                if (following) {
                    follow_step(*a);
                }
            } else {
                status = "IT DIED";
                status_until = now_seconds() + 3;
                watched = 0;
                following = false;
            }
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
        if (speed != 0) animation += dt;
        int ticks = 0;
        if (speeds[speed].multiplier > 0) {
            accumulator += dt * ticks_per_second * speeds[speed].multiplier;
            ticks = int(accumulator);
            accumulator -= ticks;
        } else if (speeds[speed].multiplier < 0) {
            accumulator = 0;
        }
        const double sim_start = now_seconds();
        const bool flat_out = speeds[speed].multiplier < 0;
        int done = 0;
        while (flat_out ? now_seconds() - sim_start < 0.010 : done < ticks) {
            step();
            ++done;
        }
        actions.set_time(double(world.tick()) + accumulator);
        if (done) {
            const double ms = (now_seconds() - sim_start) * 1000.0 / done;
            tick_ms = tick_ms == 0 ? ms : tick_ms * 0.9 + ms * 0.1;
        }
        const float live_opacity = tilt_enabled ? camera.live_opacity() : 0.0f;
        const bool live = live_opacity > 0;
        const bool capture = !shot.empty();
        if (frame_start - last_save > autosave_seconds) {
            save();
        }

        // Drawing.
        int drawable_w = width, drawable_h = height;
        SDL_GL_GetDrawableSize(window, &drawable_w, &drawable_h);
        // Allocate once per drawn size, after any queued resize events.
        // Round up so odd dimensions also have art under the last row and column.
        // Keep the low profile's world buffer near the Pi LCD's drawing cost as the window grows.
        const int profile_pixel = low_performance ? std::max({2, (width - 1) / 400 + 1, (view_height - 1) / 240 + 1}) : 2;
        const int pixel = std::max(requested_pixel.value_or(profile_pixel), (std::max(width, view_height) - 1) / target_limit + 1);
        const int art_w = (width - 1) / pixel + 1, art_h = (view_height - 1) / pixel + 1;
        if (target.width() != art_w || target.height() != art_h) {
            if (!target.init(art_w, art_h)) return 1;
            tilt.set_target_alpha(target.has_alpha());
        }
        const Camera art_camera = camera.for_target(float(pixel));
        // One publication boundary for the overview, its reduced resolutions,
        // the live fallback and the painted ground, including during the zoom fade.
        const double bake_start = now_seconds();
        const auto previous_uploads = view.uploaded_chunks();
        auto* next_batch = terrain_batch.begin(capture) ? &terrain_batch : nullptr;
        bool terrain_ready = view.refresh(world, camera, width, view_height, live_opacity, next_batch);
        if (live) terrain_ready = tilt.schedule_ground(world, art_camera, art_w, art_h, next_batch) && terrain_ready;
        if (next_batch) next_batch->finish(world, capture);
        const int uploaded_chunks = int(view.uploaded_chunks() - previous_uploads);
        bake_ms = bake_ms * 0.9 + (now_seconds() - bake_start) * 1000.0 * 0.1;
        if (capture) terrain_ready = true;
        bool scene_ready = true;
        if (live) {
            // The animals have a picture of their own, drawn before the frame's.
            tilt.prepare_animals(world, art_camera, art_w, art_h, art_h, animation);
        }
        const phd::Animal* selected = watched ? find_animal(watched) : nullptr;
        const gfx::Texture* portrait = selected ? tilt.portrait(*selected, int(panel_width - 20) / 2, 44) : nullptr;
        // Present each complete world image before reusing its render target.
        // Map markers sit under the live image, so they fade with the baked crowns.
        int world_draws = 0;
        const auto present_world = [&](std::uint8_t alpha) {
            target.end(drawable_w, drawable_h);
            batch.begin(width, height);
            batch.quad(target.texture(), 0, 0, float(art_w * pixel), float(art_h * pixel), 0, 1, 1, 0,
                       {255, 255, 255, alpha});
        };
        gl::BindFramebuffer(gl::FRAMEBUFFER, 0);
        gl::ClearColor(0.04f, 0.05f, 0.05f, 1);
        gl::Clear(gl::COLOR_BUFFER_BIT);
        if (live_opacity < 1) {
            target.begin();
            batch.begin(art_w, art_h);
            view.draw(batch, font, world, art_camera, art_w, art_h, animation);
            batch.end();
            world_draws += batch.draw_calls();
            present_world(255);
            // On the map every animal is a dot in its own colour, on the art's
            // pixel grid: bigger for a bigger body and as the view closes in,
            // dimmed with the daylight like the land under it. One in the air
            // is paler, and has no dark rim.
            const float light = 0.45f + 0.55f * float(world.daylight()) / 255.0f;
            const float grid = float(pixel);
            for (const auto& c : world.carcasses()) {
                if (!c.energy) continue;
                float sx, sy;
                camera.to_screen(float(c.x) / 256, float(c.y) / 256, camera.focus_z, float(width), float(view_height), sx, sy);
                if (sx < 0 || sy < 0 || sx >= width || sy >= view_height) continue;
                sx = std::floor(sx / grid) * grid, sy = std::floor(sy / grid) * grid;
                font.rect(batch, sx - grid, sy, sx + grid, sy + grid, {184, 166, 132, 255});
            }
            for (const phd::Animal& a : world.animals()) {
                float sx, sy;
                animal_on_screen(a, sx, sy);
                if (sx < -8 || sy < -8 || sx > float(width) + 8 || sy > float(view_height) + 8) {
                    continue;
                }
                const gfx::creature::Body body = gfx::animal_body(a.genome, a.growth);
                // Never smaller than two art pixels across, so its colour shows.
                const float half = std::max(2.0f, std::round(body.length * camera.zoom / grid)) * grid * 0.5f;
                const float x0 = std::floor((sx - half) / grid) * grid, y0 = std::floor((sy - half) / grid) * grid;
                const float x1 = x0 + std::max(grid, std::round(2 * half / grid) * grid), y1 = y0 + (x1 - x0);
                const bool flying = phd::Medium(a.medium) == phd::Medium::air;
                const float k = (flying ? 1.5f : 1.2f) * light;
                const auto tone = [&](float c, float f) { return std::uint8_t(std::clamp(c * f, 0.0f, 255.0f)); };
                if (!flying) {
                    font.rect(batch, x0 - grid, y0 - grid, x1 + grid, y1 + grid,
                              {tone(body.primary.r, light * 0.3f), tone(body.primary.g, light * 0.3f), tone(body.primary.b, light * 0.34f), 255});
                }
                font.rect(batch, x0, y0, x1, y1, {tone(body.primary.r, k), tone(body.primary.g, k), tone(body.primary.b, k), 255});
            }
            batch.end();
            world_draws += batch.draw_calls();
        }
        if (live) {
            target.begin();
            if (++tilt_frame % 15 == 0 || capture) {
                tilt.refresh(world, 2);
            }
            scene_ready = tilt.draw(world, art_camera, view.live_terrain(), font, art_w, art_h, art_h,
                                    animation, capture);
            // Firelight on what is around a fire, then the sky over it all:
            // cloud shadows, fog, rain, snow and lightning.
            batch.begin(art_w, art_h);
            gl::BlendFunc(gl::DST_COLOR, gl::ONE);
            view.draw_firelight(batch, world, art_camera, art_w, art_h, animation);
            batch.end();
            world_draws += batch.draw_calls();
            tilt.draw_fire();
            batch.begin(art_w, art_h);
            view.draw(batch, font, world, art_camera, art_w, art_h, animation, true);
            batch.end();
            world_draws += batch.draw_calls() + tilt.draw_calls();
            present_world(std::uint8_t(std::lround(live_opacity * 255)));
            batch.end();
            world_draws += batch.draw_calls();
        }
        batch.begin(width, height);
        if (inspected >= 0) {
            // Outline the cell, projected through the camera at its ground height.
            const float x = float(inspected % world.width()), y = float(inspected / world.width());
            const gfx::Color mark{255, 240, 120, 230};
            float px[4], py[4];
            const float corner_x[4] = {x, x + 1, x + 1, x}, corner_y[4] = {y, y, y + 1, y + 1};
            for (int k = 0; k < 4; ++k) {
                const float z = live ? tilt.ground(corner_x[k], corner_y[k]) : camera.focus_z;
                camera.to_screen(corner_x[k], corner_y[k], z, float(width), float(view_height), px[k], py[k]);
            }
            font.rect(batch, px[0] - 1, std::min(py[0], py[1]) - 1, px[1] + 1, std::min(py[0], py[1]) + 1, mark);
            font.rect(batch, px[3] - 1, std::max(py[2], py[3]) - 1, px[2] + 1, std::max(py[2], py[3]) + 1, mark);
            font.rect(batch, px[0] - 1, py[0], px[0] + 1, py[3], mark);
            font.rect(batch, px[1] - 1, py[1], px[1] + 1, py[2], mark);
        }

        // The animal inspector: what it is, how it is doing, and its organs.
        if (const phd::Animal* a = selected) {
            const phd::Frame f = phd::frame(a->genome, a->growth);
            // A mark at its feet.
            float sx, sy;
            const float reach = animal_on_screen(*a, sx, sy);
            const gfx::Color mark{255, 240, 120, 230};
            const float r = std::min(reach, 60.0f), tick = std::max(4.0f, r / 3);
            for (int k = 0; k < 4; ++k) {
                const float cx = sx + (k % 2 ? r : -r), cy = sy + (k / 2 ? r : -r);
                font.rect(batch, std::min(cx, cx + (k % 2 ? -tick : tick)), cy - 1, std::max(cx, cx + (k % 2 ? -tick : tick)), cy + 1, mark);
                font.rect(batch, cx - 1, std::min(cy, cy + (k / 2 ? -tick : tick)), cx + 1, std::max(cy, cy + (k / 2 ? -tick : tick)), mark);
            }

            const float px = float(width) - panel_width, left = px + 10, right = float(width) - 10;
            font.rect(batch, px, 0, float(width), float(view_height), panel);
            // What its organs make it, in a word; there are no kinds of animal.
            const char* plan = f.air_speed ? "FLIER" : !f.breathes_air ? "SWIMMER" : f.breathes_water || f.fin ? "AMPHIBIAN"
                               : f.leg_pairs ? "WALKER" : "CRAWLER";
            // And what it is called: its own kind's name, then its species and family.
            const auto place = taxa.classify(a->genome);
            const auto fitted = [&](float at, const std::string& text, gfx::Color color) {
                font.text(batch, left, at, text, font.width(text, 2) <= right - left ? 2 : 1, color);
            };
            fitted(4, taxa.common(place), accent);
            fitted(20, taxa.name(place), white);
            fitted(35, format("%s  %s %u", taxa.family(place).c_str(), plan, a->id), dim);

            if (portrait) batch.quad(*portrait, left, 52, right, 140, 0, 1, 1, 0, white);

            // Bars: its reserve of food, its wind, health, breath.
            float y = 146;
            const auto bar = [&](const char* name, int value, int full, gfx::Color color) {
                font.text(batch, left, y, name, 2, white);
                const float x0 = left + 88, x1 = right;
                font.rect(batch, x0, y + 1, x1, y + 13, {40, 48, 44, 255});
                font.rect(batch, x0, y + 1, x0 + (x1 - x0) * std::clamp(float(value) / float(std::max(1, full)), 0.0f, 1.0f), y + 13, color);
                y += 20;
            };
            bar("SATIETY", a->energy, f.store, {214, 178, 72, 255});
            bar("ENERGY", a->stamina, f.stamina, {122, 196, 110, 255});
            bar("HEALTH", a->health, 255, {188, 84, 70, 255});
            bar("BREATH", a->breath, phd::breath_ticks, {96, 168, 204, 255});

            const auto medium = phd::Medium(a->medium);
            const char* doing = a->pain ? "HURT" : medium == phd::Medium::air ? "FLYING" :
                                medium == phd::Medium::water ? "SWIMMING" : (a->wants & phd::wants_rest) ? "SLEEPING" :
                                a->pace == 0 ? "RESTING" : a->pace > phd::cruise_pace && a->stamina > 0 ? "SPRINTING" : "WALKING";
            using gfx::creature::Action;
            switch (actions.gesture(*a).action) {
            case Action::feed: doing = "FEEDING"; break;
            case Action::mate: doing = "MATING"; break;
            case Action::strike: doing = "STRIKING"; break;
            case Action::recoil: doing = "RECOILING"; break;
            case Action::call: doing = "CALLING"; break;
            case Action::idle: break;
            }
            const auto speed = [](int v) { return double(v) * phd::cell_decimetres / 256.0; }; // m a second
            std::vector<std::string> lines = {
                doing,
                format("AGE %d DAYS  YOUNG %d", a->age, a->young),
                a->growth < 255 ? format("JUVENILE %d%% GROWN", (a->growth - phd::newborn_growth) * 100 / (255 - phd::newborn_growth))
                                : a->drive >= phd::mating_drive ? std::string("SEEKING A MATE") : std::string("ADULT"),
                a->mother ? format("OF %u AND %u", a->mother, a->father) : std::string("A FOUNDER"),
                f.mass >= 1000 ? format("%.2f M  %.1f KG", f.length / 1000.0, f.mass / 1000.0) : format("%.2f M  %d G", f.length / 1000.0, f.mass),
                f.leg_pairs ? format("LEGS %d PAIRS %.2f M", f.leg_pairs, f.leg / 1000.0) : std::string("NO LEGS"),
            };
            if (f.wing) {
                lines.push_back(format("WINGS %.2f M%s", f.wing / 1000.0, f.air_speed ? "" : " SMALL"));
            }
            lines.push_back(std::string(f.breathes_air && f.breathes_water ? "LUNGS AND GILLS" : f.breathes_air ? "LUNGS" : "GILLS") +
                            (f.fin ? "  FINS" : ""));
            const bool grazes = a->genome.gene[phd::agene_plant_gut] >= 32;
            const bool eats_meat = a->genome.gene[phd::agene_meat_gut] >= 32;
            lines.push_back(!grazes && !eats_meat ? "NO DIGESTIVE GUT" : phd::diet_name(phd::diet_of(a->genome)));
            lines.push_back(format("GUT PLANT %d MEAT %d", a->genome.gene[phd::agene_plant_gut], a->genome.gene[phd::agene_meat_gut]));
            lines.push_back(format("HARD MOUTH %d HEAD %d BACK %d", a->genome.gene[phd::agene_hard_mouth],
                                   a->genome.gene[phd::agene_hard_front], a->genome.gene[phd::agene_hard_back]));
            lines.push_back(format("LAND %.1f WATER %.1f", speed(f.land_speed), speed(f.water_speed)));
            if (f.air_speed) {
                lines.push_back(format("AIR %.1f M/S", speed(f.air_speed)));
            }
            for (const auto& line : lines) {
                font.text(batch, left, y + 4, line, 2, &line == &lines[0] ? accent : white);
                y += 17;
            }
            // Its brain at work: what it senses, what each hidden unit says,
            // and what comes out. Green for more, red for less.
            {
                const auto index = std::size_t(a - world.animals().data());
                const phd::brain::Senses sensed = world.senses(index);
                const phd::brain::Decision thought = phd::brain::think(sensed, world.brain_of(index));
                const auto lamp = [&](float x0, float x1, float row, int value) {
                    const float m = std::min(1.0f, std::abs(float(value)) / 96.0f);
                    const gfx::Color c = value > 0 ? gfx::Color{std::uint8_t(40 + 40 * m), std::uint8_t(60 + 180 * m), std::uint8_t(50 + 40 * m), 255}
                                         : value < 0 ? gfx::Color{std::uint8_t(60 + 180 * m), std::uint8_t(44 + 20 * m), std::uint8_t(40 + 20 * m), 255}
                                                     : gfx::Color{36, 42, 40, 255};
                    font.rect(batch, x0, row, x1 - 1, row + 6, c);
                };
                const float top = float(view_height) - 68, span = right - left;
                for (int i = 0; i < phd::brain::inputs; ++i) {
                    lamp(left + span * float(i) / phd::brain::inputs, left + span * float(i + 1) / phd::brain::inputs, top, sensed.in[i]);
                }
                for (int i = 0; i < phd::brain::hidden; ++i) {
                    lamp(left + span * float(i) / phd::brain::hidden, left + span * float(i + 1) / phd::brain::hidden, top + 7, thought.unit[i]);
                }
                for (int i = 0; i < phd::brain::outputs; ++i) {
                    lamp(left + span * float(i) / phd::brain::outputs, left + span * float(i + 1) / phd::brain::outputs, top + 14, thought.out[i]);
                }
            }
            font.text(batch, left, float(view_height) - 42, following ? "DRAG VIEW TO STOP" : "TAP IT TO FOLLOW", 2, dim);
            font.text(batch, left, float(view_height) - 22, "TAP HERE TO CLOSE", 2, dim);
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
        font.rect(batch, xray_left, float(view_height) + 4, xray_right, float(height) - 4,
                  camera.tree_xray ? gfx::Color{40, 80, 55, 255} : gfx::Color{30, 36, 33, 255});
        font.text(batch, xray_left + 6, float(view_height) + 9, "TREE X-RAY", 2, camera.tree_xray ? accent : dim);
        const auto clock = clock_label(world.tick()), seed_label = format("SEED %u", world.seed());
        const float clock_x = xray_right + 10, bar_y = float(view_height) + 9;
        const int clock_scale = clock_x + font.width(clock, 2) + 4 <= width ? 2 : 1;
        if (clock_x + font.width(clock, clock_scale) <= width) font.text(batch, clock_x, bar_y, clock, clock_scale, white);
        const float seed_x = float(width) - font.width(seed_label, 2) - 8;
        if (seed_x > clock_x + font.width(clock, clock_scale) + 4) font.text(batch, seed_x, bar_y, seed_label, 2, dim);

        // The budget HUD.
        if (hud) {
            const double pi_ms = tick_ms * pi_factor;
            const std::string lines[] = {
                format("TICK %" PRIu64 "  %.3f MS  PI~%.2f/%.0f MS", world.tick(), tick_ms, pi_ms, pi_tick_budget_ms),
                format("FRAME %.2f MS  %.0f FPS  ZOOM %.0f  PITCH %.0f%s", frame_ms, fps, camera.zoom,
                       camera.pitch(), camera.top_down ? " LOCK" : tilt_enabled ? "" : " FLAT"),
                format("DRAWS %d  TRIS %d  BAKE %.2f MS  %dX%d Z%d", world_draws + batch.draw_calls(),
                       live ? tilt.triangles() : 0, bake_ms, art_w, art_h, target.depth_bits()),
                format("SIM %.2f MB  RSS %.1f MB", world.memory_bytes() / 1048576.0, resident_bytes() / 1048576.0),
                format("FRONTS %d  WIND %d  TREES %d  %s", int(world.fronts().size()), world.wind_speed(),
                       live ? tilt.trees() : 0, gl::is_es() ? "GLES" : "GL"),
            };
            font.rect(batch, 4, 4, 4 + 6 * 2 * 44 + 8, 4 + 5 * 18 + 6, panel);
            for (int i = 0; i < 5; ++i) {
                font.text(batch, 10, 10 + float(i) * 18, lines[i], 2, i == 0 && pi_ms > pi_tick_budget_ms ? warn : white);
            }
        }
        // What is alive, most numerous first. Counted every couple of seconds.
        if (kinds_shown) {
            if (now_seconds() - kinds_counted > 2) {
                kinds_counted = now_seconds();
                taxa.census(world.animals());
                kind_rows.clear();
                for (std::size_t s = 0; s < taxa.species().size(); ++s) {
                    const auto& races = taxa.species()[s].subspecies;
                    for (std::size_t r = 0; r < races.size(); ++r) {
                        if (!races[r].living) continue;
                        const phd::Diet diet = phd::diet_of(races[r].type);
                        const char* eats = diet == phd::Diet::herbivore ? "HERB" : diet == phd::Diet::omnivore ? "OMNI" : "CARN";
                        kind_rows.push_back({races[r].living, std::string(eats) + " " + taxa.common({int(s), int(r)})});
                    }
                }
                kinds_alive = int(kind_rows.size());
                std::stable_sort(kind_rows.begin(), kind_rows.end(), [](const KindRow& a, const KindRow& b) { return a.living > b.living; });
                if (kind_rows.size() > 12) kind_rows.resize(12);
            }
            const float top = hud ? 4 + 5 * 18 + 6 + 6 : 4;
            font.rect(batch, 4, top, 4 + 6 * 2 * 34 + 8, top + float(kind_rows.size() + 1) * 18 + 8, panel);
            font.text(batch, 10, top + 6, format("%d KINDS ALIVE  %d ANIMALS", kinds_alive, int(world.animals().size())), 2, accent);
            for (std::size_t i = 0; i < kind_rows.size(); ++i) {
                const std::string line = format("%4d %s", kind_rows[i].living, kind_rows[i].name.c_str());
                font.text(batch, 10, top + 6 + float(i + 1) * 18, line, font.width(line, 2) <= 6 * 2 * 34 ? 2 : 1, white);
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
            if (!detail_ready_frame && terrain_ready && scene_ready) {
                detail_ready_frame = int(frame_samples.size());
                detail_ready_ms = (frame_start - bench_start) * 1000.0 + frame_ms;
            }
            if (int(frame_samples.size()) >= bench_frames) {
                std::vector<double> sorted = frame_samples;
                std::sort(sorted.begin(), sorted.end());
                double sum = 0;
                for (const double v : sorted) {
                    sum += v;
                }
                std::printf("frames=%zu frame_ms_mean=%.2f frame_ms_p99=%.2f frame_ms_max=%.2f tick_ms=%.3f bake_ms=%.2f ground_ms=%.2f ground_ms_max=%.2f "
                            "ticks=%" PRIu64 " triangles=%d sprites=%d draws=%d atlas=%d%s "
                            "frame_ms_first=%.2f detail_ready_frame=%d detail_ready_ms=%.2f bake_chunks_last=%d "
                            "performance=%s art=%dx%d\n",
                            sorted.size(), sum / double(sorted.size()), sorted[sorted.size() * 99 / 100], sorted.back(),
                            tick_ms, bake_ms, tilt.ground_ms(), tilt.ground_ms_max(), world.tick(), live ? tilt.triangles() : 0,
                            live ? tilt.sprites() : 0, live ? tilt.draw_calls() : 0, tilt.atlas_sprites(),
                            tilt.atlas_full() ? " (full)" : "", frame_samples.front(), detail_ready_frame,
                            detail_ready_ms, uploaded_chunks, low_performance ? "low" : "desktop", art_w, art_h);
                running = false;
            }
        }
        SDL_GL_SwapWindow(window);
        if (low_performance && !bench_frames && running) {
            const double remaining = 1.0 / 30.0 - (now_seconds() - frame_start);
            if (remaining > 0) SDL_Delay(Uint32(std::ceil(remaining * 1000.0)));
        }
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
