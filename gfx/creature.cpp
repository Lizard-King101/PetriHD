#include "creature.hpp"

#include <algorithm>
#include <cmath>

namespace gfx::creature {
namespace {

constexpr float turn = 6.2831853f;

struct V3 {
    float x, y, z;
};
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float length(V3 a) { return std::sqrt(dot(a, a)); }
V3 normalize(V3 a, V3 otherwise = {0, 0, 1}) {
    const float l = length(a);
    return l > 1e-6f ? a * (1 / l) : otherwise;
}

struct Paint {
    Color primary, secondary;
};

struct Mesh {
    std::vector<Vertex>& out;

    void vertex(V3 p, V3 n, float along, float around, const Paint& paint) {
        const auto pack = [](float v) { return std::int8_t(std::clamp(v, -1.0f, 1.0f) * 127); };
        out.push_back({p.x, p.y, p.z, pack(n.x), pack(n.y), pack(n.z), 0, along, around, paint.primary, paint.secondary});
    }

    // A tube through `points`, `sides` round, each ring `flat` times as tall
    // as it is wide, closed to a point at both ends. `along` runs from a0 at
    // the first point to a1 at the last.
    // With `fade`, the pigment runs from `first` at the first point to `fade`
    // by the third: a leg takes the body's colour where it leaves the body.
    void tube(const V3* points, const float* radii, int count, int sides, float flat, const Paint& first, float a0 = 0, float a1 = 1,
              const Paint* fade = nullptr) {
        struct Ring {
            V3 centre, across, up;
            float radius, along;
            Paint paint;
        };
        std::vector<Ring> rings;
        rings.resize(std::size_t(count));
        for (int i = 0; i < count; ++i) {
            const V3 ahead = normalize(points[std::min(i + 1, count - 1)] - points[std::max(i - 1, 0)], {1, 0, 0});
            const V3 across = normalize(cross({0, 0, 1}, ahead), {0, 1, 0});
            Paint paint = first;
            if (fade) {
                const float t = std::min(1.0f, float(i) / 2.0f);
                const auto mix = [t](std::uint8_t a, std::uint8_t b) { return std::uint8_t(float(a) + (float(b) - float(a)) * t); };
                paint.primary = {mix(first.primary.r, fade->primary.r), mix(first.primary.g, fade->primary.g),
                                 mix(first.primary.b, fade->primary.b), first.primary.a};
                paint.secondary = fade->secondary;
            }
            rings[std::size_t(i)] = {points[i], across, normalize(cross(ahead, across)), radii[i],
                                     a0 + (a1 - a0) * float(i) / float(std::max(1, count - 1)), paint};
        }
        const auto corner = [&](const Ring& r, int k, V3& p, V3& n) {
            const float a = float(k % sides) / float(sides) * turn;
            n = normalize(r.across * std::cos(a) + r.up * std::sin(a));
            p = r.centre + r.across * (std::cos(a) * r.radius) + r.up * (std::sin(a) * r.radius * flat);
        };
        for (int i = 0; i + 1 < count; ++i) {
            const Ring& a = rings[std::size_t(i)];
            const Ring& b = rings[std::size_t(i + 1)];
            for (int k = 0; k < sides; ++k) {
                V3 p0, n0, p1, n1, p2, n2, p3, n3;
                corner(a, k, p0, n0);
                corner(a, k + 1, p1, n1);
                corner(b, k, p2, n2);
                corner(b, k + 1, p3, n3);
                const float v0 = float(k) / float(sides), v1 = float(k + 1) / float(sides);
                vertex(p0, n0, a.along, v0, a.paint), vertex(p2, n2, b.along, v0, b.paint), vertex(p3, n3, b.along, v1, b.paint);
                vertex(p0, n0, a.along, v0, a.paint), vertex(p3, n3, b.along, v1, b.paint), vertex(p1, n1, a.along, v1, a.paint);
            }
        }
        // The ends, each closed to its centre.
        for (int end = 0; end < 2; ++end) {
            const Ring& r = rings[std::size_t(end ? count - 1 : 0)];
            const V3 out_n = normalize(cross(r.across, r.up)) * (end ? 1.0f : -1.0f);
            for (int k = 0; k < sides; ++k) {
                V3 p0, n0, p1, n1;
                corner(r, k, p0, n0);
                corner(r, k + 1, p1, n1);
                vertex(r.centre, out_n, r.along, 0.5f, r.paint), vertex(p0, normalize(n0 + out_n), r.along, 0, r.paint),
                    vertex(p1, normalize(n1 + out_n), r.along, 0, r.paint);
            }
        }
    }

    // A flat piece, seen from both sides and lit the same from both.
    void flat(V3 a, V3 b, V3 c, Paint paint) {
        paint.primary.a = two_sided;
        const V3 n = normalize(cross(b - a, c - a));
        vertex(a, n, 0.5f, 0.5f, paint), vertex(b, n, 0.5f, 0.5f, paint), vertex(c, n, 0.5f, 0.5f, paint);
    }

    // The shader fits this mark to the pixel grid at its socket's depth.
    void eye(V3 socket, V3 outward, float radius) {
        const Paint pigment{{22, 20, 26, unlit}, {22, 20, 26, 0}};
        const int corners[6][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}};
        for (const auto& corner : corners) {
            vertex(socket, outward, float(corner[0]) * radius, float(corner[1]) * radius, pigment);
        }
    }
};

Color darker(Color c, float k) {
    return {std::uint8_t(float(c.r) * k), std::uint8_t(float(c.g) * k), std::uint8_t(float(c.b) * k), c.a};
}

} // namespace

float ride_height(const Body& body) {
    // Legs under the body hold it at nearly their length; sprawled, lower.
    return body.leg_pairs ? body.leg_length * (0.86f - 0.45f * body.sprawl) + body.girth * 0.7f : body.girth;
}

void build(std::vector<Vertex>& out, const Body& b, const Pose& pose, const Ground& ground, std::vector<Foot>* feet) {
    Mesh mesh{out};
    const V3 fore{std::cos(pose.heading), std::sin(pose.heading), 0}, side{-fore.y, fore.x, 0}, up{0, 0, 1};
    const V3 at{pose.x, pose.y, 0};
    const Paint skin{{b.primary.r, b.primary.g, b.primary.b, lit},
                     {b.secondary.r, b.secondary.g, b.secondary.b, std::uint8_t(b.pattern)}};
    const Paint limb{darker(skin.primary, 0.82f), {b.secondary.r, b.secondary.g, b.secondary.b, std::uint8_t(Pattern::plain)}};
    const bool walks = pose.medium == Medium::land && b.leg_pairs > 0;
    const float beat = pose.phase * turn;
    const float L = b.length, G = b.girth;

    // The middle of the body, `along` cells ahead of its centre. On land it
    // follows the ground under its hips and shoulders, so it tips on a slope.
    const float rear = ground(pose.x - fore.x * L * 0.35f, pose.y - fore.y * L * 0.35f);
    const float front = ground(pose.x + fore.x * L * 0.35f, pose.y + fore.y * L * 0.35f);
    const float ride = ride_height(b);
    const auto spine = [&](float along, float lift) {
        // It wags from side to side, more toward the tail: a swimmer with
        // its whole body, a walker only a little. The wave runs from the
        // head back to the tail, which is what pushes a swimmer forward.
        const float back = std::clamp((L * 0.3f - along) / std::max(0.05f, L + b.tail), 0.0f, 1.0f);
        const float sway = (pose.medium == Medium::water ? G * 1.5f : G * 0.2f * pose.pace) * back * std::sin(beat + along / std::max(0.1f, L) * 3.2f);
        float z;
        if (pose.medium == Medium::land) {
            z = rear + (front - rear) * (along / (L * 0.7f) + 0.5f) + ride + (walks ? G * 0.06f * pose.pace * std::sin(beat * 2) : 0.0f);
        } else {
            z = pose.height + (pose.medium == Medium::air ? G * 0.25f * std::sin(beat) : 0.0f);
        }
        return at + fore * along + side * sway + up * (z + lift);
    };

    // The spine, from the tip of the tail to the snout: where each ring is,
    // how thick, and how far it is raised.
    struct Station {
        float along, radius, lift;
    };
    std::vector<Station> stations;
    const float raised = walks ? b.neck * 1.1f : 0.0f; // a walker carries its head up
    if (b.tail > 0) {
        stations.push_back({-L * 0.5f - b.tail, G * 0.06f, walks ? -G * 0.1f : 0.0f});
        stations.push_back({-L * 0.5f - b.tail * 0.5f, G * 0.3f, 0});
    }
    stations.push_back({-L * 0.5f, G * 0.62f, 0});
    stations.push_back({-L * 0.28f, G * 0.94f, 0});
    stations.push_back({0, G, 0});
    stations.push_back({L * 0.28f, G * 0.95f, 0});
    stations.push_back({L * 0.5f, G * 0.66f, raised * 0.2f});
    const float skull = L * 0.5f + b.neck;
    if (b.neck > b.head) {
        // A neck long enough to show: narrower than the head it carries.
        stations.push_back({L * 0.5f + b.neck * 0.5f, std::min(G * 0.42f, b.head * 0.8f), raised * 0.65f});
    }
    stations.push_back({skull, b.head * 0.78f, raised});
    const std::size_t brow = stations.size();
    stations.push_back({skull + b.head * 0.55f, b.head, raised});
    stations.push_back({skull + b.head, b.head * 0.74f, raised * 0.96f});
    stations.push_back({skull + b.head + b.snout, b.head * 0.3f, raised * 0.9f});
    std::vector<V3> points;
    std::vector<float> radii;
    for (const Station& s : stations) {
        points.push_back(spine(s.along, s.lift));
        radii.push_back(s.radius);
    }
    mesh.tube(points.data(), radii.data(), int(points.size()), 8, 0.9f, skin);

    if (b.eye > 0 && b.head > 0) {
        // Take the same ring and axes as the head mesh, including its tilt on
        // a slope. Each socket sits halfway up the first facet on its side.
        const V3 ahead = normalize(points[brow + 1] - points[brow - 1], fore);
        const V3 across = normalize(cross(up, ahead), side);
        const V3 head_up = normalize(cross(ahead, across));
        for (const float s : {-1.0f, 1.0f}) {
            const V3 socket = points[brow] + (across * (s * 0.854f) + head_up * 0.318f) * b.head;
            mesh.eye(socket, normalize(across * (s * 0.924f) + head_up * 0.426f), b.eye);
        }
    }

    // All three gaits use the same hips, bones and skin. The medium sets
    // the ankle target and the direction the knee folds toward.
    if (b.leg_pairs > 0) {
        const float thigh = b.leg_length * 0.53f, stride = b.leg_length * 0.75f * pose.pace;
        for (int pair = 0; pair < b.leg_pairs; ++pair) {
            const float along = b.leg_pairs == 1 ? 0.0f : L * (0.36f - 0.72f * float(pair) / float(b.leg_pairs - 1));
            const V3 middle = spine(along, 0);
            for (int k = 0; k < 2; ++k) {
                const float s = k ? 1.0f : -1.0f;
                // Neighbouring legs take opposite halves of the stride or paddle.
                const float cycle = pose.phase + ((pair + k) % 2 ? 0.5f : 0.0f);
                const float t = cycle - std::floor(cycle);
                // The hip is inside the body, low under it for legs that stand
                // beneath it and out at its flank for legs that sprawl, so the
                // top of the leg comes out of the body and not off its skin.
                const V3 hip = middle + side * (s * G * (0.42f + 0.3f * b.sprawl)) - up * (G * (0.45f - 0.35f * b.sprawl));
                const V3 outward = side * s;
                V3 bend = fore * ((pair * 2 < b.leg_pairs ? 1.0f : -1.0f) * (1 - b.sprawl)) + side * (s * b.sprawl) + up * (0.7f * b.sprawl);
                V3 foot, toes = fore;
                if (walks) {
                    // The foot stays on the ground until it swings ahead.
                    const float swing = t < 0.62f ? 0.5f - t / 0.62f : -0.5f + (t - 0.62f) / 0.38f;
                    const float lifted = t < 0.62f ? 0.0f : std::sin((t - 0.62f) / 0.38f * 3.14159265f) * b.leg_length * 0.3f * pose.pace;
                    foot = at + fore * (along + swing * stride) + side * (s * (G * 0.5f + b.leg_length * (0.03f + 0.7f * b.sprawl)));
                    foot.z = ground(foot.x, foot.y) + lifted;
                } else if (pose.medium == Medium::water) {
                    // Sweep back wide and deep on the power stroke. Lift the
                    // foot close to the flank as it comes forward again.
                    const float sweep = std::cos(t * turn) * pose.pace, push = std::sin(t * turn) * pose.pace;
                    const V3 paddle = fore * (0.48f * sweep - 0.12f) + outward * (0.34f + 0.22f * b.sprawl + 0.19f * push)
                                      - up * (0.34f + 0.18f * push);
                    foot = hip + paddle * b.leg_length;
                    toes = normalize(outward * (0.6f + 0.3f * push) - fore);
                } else {
                    // Knees forward, ankles drawn back and toes streaming
                    // aft under the belly. The whole fold rides with the hip.
                    foot = hip + (outward * (0.22f + 0.12f * b.sprawl) - fore * 0.12f - up * 0.25f) * b.leg_length;
                    bend = fore;
                    toes = fore * -1.0f;
                }
                // No further than the leg reaches.
                V3 reach = foot - hip;
                const float far = std::min(length(reach), thigh * 1.97f);
                reach = normalize(reach, {0, 0, -1}) * far;
                foot = hip + reach;
                bend = normalize(bend - normalize(reach) * dot(bend, normalize(reach)));
                const float rise = std::sqrt(std::max(0.0f, thigh * thigh - far * far / 4));
                const V3 knee = hip + reach * 0.5f + bend * rise;
                const V3 leg[4] = {hip, knee, foot + up * (b.leg_girth * 0.6f), foot + toes * (b.leg_girth * 1.6f) + up * (b.leg_girth * 0.35f)};
                if (feet && walks) {
                    feet->push_back({(leg[2].x + leg[3].x) * 0.5f, (leg[2].y + leg[3].y) * 0.5f, foot.z});
                }
                // A shoulder or a haunch where it leaves the body, heavier
                // on legs that carry the body than on legs that sprawl.
                // A leg is never thinner than the pixels it is drawn in.
                const float shin = creature::shin(b, pose.grain);
                const float joint = std::max(shin * 1.3f, G * (0.5f - 0.32f * b.sprawl));
                const float thick[4] = {joint, shin, std::max(shin * 0.72f, pose.grain * 0.5f), std::max(shin * 0.6f, pose.grain * 0.5f)};
                // In the body's own colour at the hip, darkening down the leg.
                Paint top = skin;
                top.secondary.a = std::uint8_t(Pattern::plain);
                mesh.tube(leg, thick, 4, 6, 1.0f, top, 0, 1, &limb);
            }
        }
    }

    // Fins: the tail's, upright; one along the back; one on each flank.
    Paint fin = limb;
    fin.primary = darker(skin.primary, 0.7f);
    if (b.tail_fin > 0) {
        const V3 root = spine(-L * 0.5f - b.tail * 0.85f, 0), tip = spine(-L * 0.5f - b.tail - b.tail_fin * 0.9f, 0);
        const V3 notch = root + (tip - root) * 0.55f;
        mesh.flat(root, tip + up * (b.tail_fin * 0.8f), notch, fin);
        mesh.flat(root, notch, tip - up * (b.tail_fin * 0.8f), fin);
    }
    if (b.dorsal_fin > 0) {
        const V3 a = spine(L * 0.2f, G * 0.85f), c = spine(-L * 0.25f, G * 0.8f);
        mesh.flat(a, c + up * b.dorsal_fin - fore * (b.dorsal_fin * 0.3f), c, fin);
    }
    if (b.side_fins > 0) {
        for (const float s : {-1.0f, 1.0f}) {
            const V3 a = spine(L * 0.3f, -G * 0.3f) + side * (s * G * 0.8f), c = spine(L * 0.1f, -G * 0.3f) + side * (s * G * 0.85f);
            const float flap = std::sin(beat * 2 + s) * 0.35f;
            mesh.flat(a, c, c + side * (s * b.side_fins) - fore * (b.side_fins * 0.5f) + up * (b.side_fins * flap), fin);
        }
    }

    // Wings fold down at rest. In flight the wrist trails the shoulder's
    // motion, flexing slightly up on the downstroke and folding on the rise.
    if (b.wing > 0) {
        float shoulder_angle = -1.1f, outer_angle = -1.845f;
        if (pose.medium == Medium::air) {
            shoulder_angle = 0.85f * std::sin(beat);
            const float rise = std::cos(beat); // signed speed of the shoulder
            const float wrist_bend = -rise * (0.16f + 0.94f * std::max(rise, 0.0f));
            outer_angle = shoulder_angle + wrist_bend;
        }
        const float chord = L * 0.55f;
        for (const float s : {-1.0f, 1.0f}) {
            const V3 shoulder = spine(L * 0.2f, G * 0.5f) + side * (s * G * 0.5f);
            const V3 inner = side * (s * std::cos(shoulder_angle)) + up * std::sin(shoulder_angle);
            const V3 outer = side * (s * std::cos(outer_angle)) + up * std::sin(outer_angle);
            const V3 wrist = shoulder + inner * (b.wing * 0.45f), tip = wrist + outer * (b.wing * 0.55f) - fore * (chord * 0.3f);
            const V3 back = fore * -chord;
            mesh.flat(shoulder, wrist, wrist + back * 0.9f, fin);
            mesh.flat(shoulder, wrist + back * 0.9f, shoulder + back, fin);
            mesh.flat(wrist, tip, wrist + back * 0.9f, fin);
        }
    }
}

} // namespace gfx::creature
