#include "creature.hpp"

#include <algorithm>
#include <cstdio>
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
} // namespace

int main() {
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
    return 0;
}
