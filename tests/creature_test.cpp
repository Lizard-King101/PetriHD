#include "creature.hpp"
#include "creature_actions.hpp"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <limits>

using namespace gfx::creature;

namespace {
float flat_ground(float, float) { return 0; }

struct Extent {
    float left = std::numeric_limits<float>::max(), right = std::numeric_limits<float>::lowest();
    float bottom = std::numeric_limits<float>::max();
};

Extent extent(const Body& body, Pose pose, float phase) {
    pose.phase = phase;
    std::vector<Vertex> vertices;
    build(vertices, body, pose, flat_ground);
    Extent bounds;
    for (const Vertex& v : vertices) {
        bounds.left = std::min(bounds.left, v.y);
        bounds.right = std::max(bounds.right, v.y);
        bounds.bottom = std::min(bounds.bottom, v.z);
    }
    return bounds;
}

bool action_poses() {
    // The eye sockets follow the head even when looking down the length of
    // the muzzle would obscure its movement. Compare them in rig space.
    const Body body;
    Pose pose;
    pose.pace = 0;
    const auto eye = [&](Gesture gesture) {
        pose.gesture = gesture;
        std::vector<Vertex> mesh;
        build(mesh, body, pose, flat_ground);
        return *std::find_if(mesh.begin(), mesh.end(), [](const Vertex& v) { return v.primary.a == unlit; });
    };
    const auto rest = eye({});
    const auto low = eye({Action::feed, 1, -1, 0, 0});
    const auto high = eye({Action::feed, 1, 1, 0, 1});
    const auto draw_back = eye({Action::strike, -0.28f});
    const auto lunge = eye({Action::strike, 1});
    const auto flinch = eye({Action::recoil, 1, -1, 0});
    const auto call = eye({Action::call, 1});
    const auto nuzzle = eye({Action::mate, 1, 1, 0});
    return low.z < rest.z - 0.12f && low.z > 0 && low.y < rest.y - 0.1f &&
           high.z > rest.z && high.y > rest.y && draw_back.x < rest.x && lunge.x > rest.x + 0.2f &&
           flinch.z > rest.z && flinch.y < rest.y && call.z > rest.z + 0.15f &&
           nuzzle.z > rest.z && nuzzle.y > rest.y + 0.10f;
}

bool portrait_fits() {
    Body body;
    body.frill = true;
    body.mouth_hardness = body.front_hardness = body.back_hardness = 1;
    body.leg_length = 0.9f;
    body.wing = 1.7f;
    constexpr int w = 120, h = 44;
    const auto frame = frame_portrait(body, w, h);
    std::vector<Vertex> mesh;
    for (Medium medium : {Medium::land, Medium::water, Medium::air}) {
        auto pose = portrait_pose(body, medium);
        for (Gesture g : {Gesture{}, {Action::strike, -0.28f}, {Action::strike, 0.75f},
                          {Action::mate, 0.8f, -0.7f, 0.7f}, {Action::call, 1},
                          {Action::feed, 1, -0.6f, 0.8f, 5}, {Action::feed, 1, 0.8f, -0.6f, -5}}) {
            pose.gesture = g;
            mesh.clear();
            build(mesh, body, pose, flat_ground);
            for (const auto& v : mesh) {
                const float x = (v.x - frame.cx) * frame.zoom + w * 0.5f;
                const float y = (v.y * frame.sin_pitch - v.z * frame.cos_pitch - frame.cy) * frame.zoom + h * 0.5f;
                const float edge = 1 + (v.primary.a == unlit ? std::abs(v.along) * frame.zoom : 0);
                if (x < edge || x > w - edge || y < edge || y > h - edge) return false;
            }
        }
    }
    return true;
}

bool action_events() {
    phd::WorldConfig cfg;
    cfg.width = cfg.height = 32;
    cfg.arena_land = 28;
    cfg.layout = phd::Layout::arena;
    phd::World world(cfg, {{8, 8, 1, 1}});
    const auto& body = phd::founder_genome(phd::FounderKind::grazer);
    const auto mind = [](int output) {
        phd::brain::Brain brain{};
        auto* bias = brain.weight + phd::brain::hidden * phd::brain::unit_bytes + phd::brain::hidden;
        bias[phd::brain::out_pace * phd::brain::output_bytes] = -127;
        bias[output * phd::brain::output_bytes] = 127;
        return brain;
    };
    world.add_animal(body, 8 * 256, 8 * 256, 0, 25, mind(phd::brain::out_eat));
    world.add_animal(body, 12 * 256, 12 * 256, 0, 80, mind(phd::brain::out_attack));
    world.add_animal(body, 12 * 256 + 8, 12 * 256, 128, 80, mind(phd::brain::out_call));
    world.add_animal(body, 20 * 256, 12 * 256, 64, 80, mind(phd::brain::out_call));
    world.add_animal(body, 24 * 256, 12 * 256, 0, 80, mind(phd::brain::out_attack));
    for (int offset : {0, 128}) {
        world.add_animal(body, 19 * 256, 20 * 256 + offset, 0, 100, mind(phd::brain::out_mate));
        const_cast<phd::Animal&>(world.animals().back()).drive = 255;
    }
    auto replay = world;
    gfx::CreatureActions actions;
    for (int tick = 1; tick <= 13; ++tick) {
        world.step(&actions);
        replay.step();
        actions.set_time(tick + 0.25);
        const auto motion = [&](int index) { return actions.gesture(world.animals()[index]); };
        if (tick == 5 && !(motion(0).action == Action::feed && motion(0).amount > 0.5f &&
                           motion(3).action == Action::call)) return false;
        // Nothing is at this one's mouth, so it never lunges.
        if (motion(4).action != Action::idle) return false;
        if (tick == 8 && !(motion(1).action == Action::strike && motion(1).amount < 0 &&
                           motion(2).action == Action::recoil && motion(2).forward < 0)) return false;
        if (tick == 10 && !(motion(1).amount > 0.85f && motion(2).amount > 0)) return false;
        if (tick == 8 && (motion(5).action == Action::mate || motion(6).action == Action::mate)) return false;
        if (tick == 10 && !(motion(5).action == Action::mate && motion(6).action == Action::mate &&
                            motion(5).amount > 0 && motion(5).amount == motion(6).amount &&
                            motion(5).side > 0.9f && motion(6).side < -0.9f)) return false;
        if (tick == 13 && !(motion(1).action == Action::idle && motion(2).action != Action::recoil)) return false;
    }
    return world.save() == replay.save();
}
} // namespace

int main() {
    // Regional armor must be visible up close without increasing the triangle
    // budget. Tiny creatures omit both the plates and the frill.
    for (Detail detail : {Detail::full, Detail::distant}) {
        Body body;
        Pose pose = portrait_pose(body, Medium::land);
        std::vector<Vertex> soft, armored, frilled;
        build(soft, body, pose, flat_ground, nullptr, detail);
        body.mouth_hardness = body.front_hardness = body.back_hardness = 1;
        build(armored, body, pose, flat_ground, nullptr, detail);
        body.frill = true;
        build(frilled, body, pose, flat_ground, nullptr, detail);
        if (soft.size() != armored.size()) return 1;
        const auto same_surface = [](const Vertex& a, const Vertex& b) {
            return a.x == b.x && a.y == b.y && a.z == b.z && a.primary.r == b.primary.r;
        };
        const bool same = std::equal(soft.begin(), soft.end(), armored.begin(), same_surface);
        if (same != (detail == Detail::distant) || (frilled.size() > armored.size()) != (detail == Detail::full)) {
            std::puts("Unexpected armor or frill detail at this scale");
            return 1;
        }
    }
    if (!portrait_fits()) {
        std::puts("All poses should fit inside a single fixed portrait frame");
        return 1;
    }
    if (!action_poses()) {
        std::puts("Expected a directed feeding reach, strike, recoil and raised calling pose");
        return 1;
    }
    if (!action_events()) {
        std::puts("Viewer gestures should follow the simulated contacts and leave the replay identical");
        return 1;
    }
    Body body;
    Pose pose;
    pose.height = 2;
    for (Medium medium : {Medium::land, Medium::water, Medium::air}) {
        pose.medium = medium;
        std::size_t previous_size = 0;
        for (int pairs : {0, 1, 2, 4}) {
            body.leg_pairs = pairs;
            std::vector<Vertex> vertices;
            std::vector<Foot> contacts;
            build(vertices, body, pose, flat_ground, &contacts);
            if (vertices.size() <= previous_size || contacts.size() != std::size_t(medium == Medium::land ? pairs * 2 : 0)) {
                std::printf("Unexpected mesh or contacts with %d leg pairs in medium %d\n", pairs, int(medium));
                return 1;
            }
            previous_size = vertices.size();
        }
    }

    // On a narrow-bodied animal the alternating paddle can be read from
    // the silhouette. In flight the same legs fold closer to the belly.
    body.leg_pairs = 1;
    body.girth = 0.07f;
    body.leg_length = 0.6f;
    pose.medium = Medium::water;
    const Extent first = extent(body, pose, 0.25f), second = extent(body, pose, 0.75f);
    pose.medium = Medium::air;
    const Extent tucked = extent(body, pose, 0.25f);
    if (!(first.left < second.left && second.right > first.right && tucked.bottom > first.bottom)) {
        std::puts("Expected alternating paddle strokes in water and tucked legs in flight");
        return 1;
    }
    // With fins absent the two-sided triangles are exactly the wings.
    body.wing = 1.8f;
    body.leg_length = 0.04f;
    pose.heading = 0;
    for (float incline : {0.0f, -0.6f, 0.6f}) {
        const auto slope = [incline](float x, float y) { return incline * x + 0.3f * y; };
        for (float phase : {0.0f, 0.25f, 0.75f}) {
            pose.medium = Medium::land;
            pose.phase = phase;
            std::vector<Vertex> mesh;
            build(mesh, body, pose, slope);
            bool swept_back = false;
            for (const Vertex& v : mesh) {
                if (v.primary.a != two_sided) continue;
                if (v.z <= slope(v.x, v.y) || std::fabs(v.y) > body.girth * 1.3f) {
                    std::puts("Landed wing should hug the body and clear the slope");
                    return 1;
                }
                swept_back |= v.x < -body.length * 0.4f;
            }
            if (!swept_back) return 1;
        }
    }
    pose.medium = Medium::air;
    const Extent extended = extent(body, pose, 0.5f);
    if (extended.right - extended.left < body.wing * 1.9f) {
        std::puts("Airborne wings should extend to full span");
        return 1;
    }
    // The primaries still form a broad fan near the tip at full extension.
    // Measure the front and rear intersections through the actual silhouette.
    pose.phase = 0.5f;
    std::vector<Vertex> airborne;
    build(airborne, body, pose, flat_ground);
    const float cut = body.wing * 0.85f;
    float fore = std::numeric_limits<float>::lowest(), aft = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < airborne.size(); i += 3) {
        if (airborne[i].primary.a != two_sided) continue;
        for (int edge = 0; edge < 3; ++edge) {
            const Vertex& a = airborne[i + edge];
            const Vertex& b = airborne[i + (edge + 1) % 3];
            if ((a.y < cut) == (b.y < cut)) continue;
            const float x = a.x + (b.x - a.x) * (cut - a.y) / (b.y - a.y);
            fore = std::max(fore, x);
            aft = std::min(aft, x);
        }
    }
    if (fore < aft + body.length * 0.4f) {
        std::puts("Expected a broad outer feather fan");
        return 1;
    }
    // The overview mesh is cheaper but reaches the same airborne wingtips
    // and plants its walking feet at the same positions as the full rig.
    std::vector<Vertex> distant;
    build(distant, body, pose, flat_ground, nullptr, Detail::distant);
    const auto span_tip = [](const auto& mesh) {
        return std::max_element(mesh.begin(), mesh.end(), [](const Vertex& a, const Vertex& b) { return a.y < b.y; })->y;
    };
    if (distant.size() * 3 >= airborne.size() || std::abs(span_tip(airborne) - span_tip(distant)) > 0.01f) {
        std::puts("Expected a simplified distant mesh with full flight span");
        return 1;
    }
    body.leg_length = 0.5f;
    pose.medium = Medium::land;
    pose.grain = 1.0f / 16;
    std::vector<Foot> detailed_feet, distant_feet;
    const auto bank = [](float x, float y) { return 0.3f * x - 0.2f * y; };
    build(airborne, body, pose, bank, &detailed_feet);
    build(distant, body, pose, bank, &distant_feet, Detail::distant);
    if (distant_feet.size() != detailed_feet.size() || distant_feet.empty()) return 1;
    for (std::size_t i = 0; i < distant_feet.size(); ++i) {
        const auto a = detailed_feet[i], b = distant_feet[i];
        if (a.x != b.x || a.y != b.y || a.z != b.z) return 1;
    }
    return 0;
}
