#include "creature.hpp"

#include <algorithm>
#include <array>
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

Color darker(Color c, float k) {
    return {std::uint8_t(float(c.r) * k), std::uint8_t(float(c.g) * k), std::uint8_t(float(c.b) * k), c.a};
}

struct Mesh {
    std::vector<Vertex>& out;

    static Vertex make_vertex(V3 p, V3 n, float along, float around, const Paint& paint) {
        const auto pack = [](float v) { return std::int8_t(std::clamp(v, -1.0f, 1.0f) * 127); };
        return {p.x, p.y, p.z, pack(n.x), pack(n.y), pack(n.z), 0, along, around, paint.primary, paint.secondary};
    }

    void vertex(V3 p, V3 n, float along, float around, const Paint& paint) {
        out.push_back(make_vertex(p, n, along, around, paint));
    }

    // A tube through `points`, `sides` round, each ring `flat` times as tall
    // as it is wide, closed to a point at both ends. `along` runs from a0 at
    // the first point to a1 at the last.
    // With `fade`, the pigment runs from `first` at the first point to `fade`
    // by the third: a leg takes the body's colour where it leaves the body.
    template<int sides, bool armored = false, std::size_t capacity>
    void tube(const V3 (&points)[capacity], const float (&radii)[capacity], int count, float flat, const Paint& first, float a0 = 0, float a1 = 1,
              const Paint* fade = nullptr, const float* hardness = nullptr, bool winged = false) {
        struct Direction { float cosine, sine; };
        static const auto circle = [] {
            std::array<Direction, sides> result;
            for (int k = 0; k < sides; ++k) {
                const float angle = float(k) / float(sides) * turn;
                result[k] = {std::cos(angle), std::sin(angle)};
            }
            return result;
        }();
        struct Ring {
            Vertex rim[sides + 1];
        };
        Ring rings[capacity];
        // The caps use axial normals and separate pattern coordinates.
        struct Cap { Vertex centre, rim[sides]; };
        Cap caps[2];
        for (int i = 0; i < count; ++i) {
            const V3 ahead = normalize(points[std::min(i + 1, count - 1)] - points[std::max(i - 1, 0)], {1, 0, 0});
            const V3 across = normalize(cross({0, 0, 1}, ahead), {0, 1, 0});
            const V3 up = normalize(cross(ahead, across));
            Paint paint = first;
            if (fade) {
                const float t = std::min(1.0f, float(i) / 2.0f);
                const auto mix = [t](std::uint8_t a, std::uint8_t b) { return std::uint8_t(float(a) + (float(b) - float(a)) * t); };
                paint.primary = {mix(first.primary.r, fade->primary.r), mix(first.primary.g, fade->primary.g),
                                 mix(first.primary.b, fade->primary.b), first.primary.a};
                paint.secondary = fade->secondary;
            }
            const float along = a0 + (a1 - a0) * float(i) / float(std::max(1, count - 1));
            Cap* cap = i == 0 ? &caps[0] : i == count - 1 ? &caps[1] : nullptr;
            V3 axial{};
            if (cap) {
                axial = normalize(cross(across, up)) * (i == 0 ? -1.0f : 1.0f);
                cap->centre = make_vertex(points[i], axial, along, 0.5f, paint);
            }
            auto& rim = rings[i].rim;
            float coverage = 0;
            Paint shell = paint;
            if constexpr (armored) {
                coverage = hardness[i];
                if (coverage > 0) {
                    const auto keratin = [&](int c, int secondary) {
                        return std::uint8_t(c + (58 + 0.16f * secondary - 0.43f * c) * coverage);
                    };
                    shell.primary = {keratin(paint.primary.r, paint.secondary.r), keratin(paint.primary.g, paint.secondary.g),
                                     keratin(paint.primary.b, paint.secondary.b), lit};
                    shell.secondary = darker(shell.primary, 0.69f);
                    shell.secondary.a = std::uint8_t(Pattern::stripes);
                }
            }
            for (int k = 0; k < sides; ++k) {
                const auto [c, s] = circle[k];
                const V3 normal = across * c + up * s;
                V3 position = points[i] + across * (c * radii[i]) + up * (s * radii[i] * flat);
                const float around = float(k) / sides;
                const Paint* pigment = &paint;
                float pattern_u = along;
                if constexpr (armored) {
                    const bool jaw = i >= count - 2;
                    const bool under_wing = winged && i < count - 4 && s > 0.45f;
                    const bool eye_socket = i == count - 3 && std::abs(c) > 0.65f;
                    if (coverage > 0 && !under_wing && !eye_socket && (jaw || s > 1.14f - coverage * 1.22f)) {
                        const float ridge = jaw ? 0.14f : (i % 2 ? 0.31f : 0.10f);
                        position = position + normal * (radii[i] * coverage * ridge);
                        pattern_u = jaw ? 0 : along * 1.65f + 0.13f * std::abs(c);
                        pigment = &shell;
                    }
                }
                rim[k] = make_vertex(position, normal, pattern_u, around, *pigment);
                if (cap) {
                    cap->rim[k] = rim[k];
                    // The rim and axial normals are perpendicular unit vectors.
                    const auto pack = [](float v) { return std::int8_t(std::clamp(v * (127 * 0.7071068f), -127.f, 127.f)); };
                    cap->rim[k].nx = pack(normal.x + axial.x);
                    cap->rim[k].ny = pack(normal.y + axial.y);
                    cap->rim[k].nz = pack(normal.z + axial.z);
                }
            }
            rim[sides] = rim[0];
            rim[sides].around = 1;
        }
        const auto first_vertex = out.size();
        out.resize(first_vertex + 6 * sides * count);
        Vertex* dest = out.data() + first_vertex;
        for (int i = 0; i + 1 < count; ++i) {
            const auto& a = rings[i].rim;
            const auto& b = rings[i + 1].rim;
            for (int k = 0; k < sides; ++k) {
                *dest++ = a[k]; *dest++ = b[k];     *dest++ = b[k + 1];
                *dest++ = a[k]; *dest++ = b[k + 1]; *dest++ = a[k + 1];
            }
        }
        for (const Cap& cap : caps) {
            for (int k = 0; k < sides; ++k) {
                Vertex next = cap.rim[(k + 1) % sides];
                next.around = 1;
                *dest++ = cap.centre; *dest++ = cap.rim[k]; *dest++ = next;
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

// Wing coordinates run out from the shoulder and aft from the leading edge.
// Both poses use the same feather fan. The pose supplies its curve in space.
template<class Place>
void feathered_wing(Mesh& mesh, const Place& place, Paint plumage, Detail detail) {
    Paint flights = plumage;
    flights.primary = darker(plumage.primary, 0.79f);
    if (detail == Detail::distant) {
        // One fan through the same shoulder, wrist and longest primaries.
        const V3 root = place(0, 0.1f, 0), wrist = place(0.50f, -0.16f, 0.2f);
        const V3 inner = place(0.08f, 0.95f, 0), outer = place(1, 0.41f, 0);
        const V3 trailing = place(0.76f, 1.2f, 0);
        mesh.flat(root, wrist, inner, plumage);
        mesh.flat(inner, wrist, trailing, flights);
        mesh.flat(wrist, outer, trailing, plumage);
        return;
    }
    struct Section { float span, leading, trailing, arch; };
    constexpr Section arm[] = {
        {0.00f, 0.10f, 0.69f, 0.06f}, {0.14f, -0.03f, 0.98f, 0.22f},
        {0.34f, -0.13f, 1.07f, 0.27f}, {0.50f, -0.16f, 1.03f, 0.23f},
        {0.65f, -0.08f, 0.83f, 0.16f}, {0.78f, 0.02f, 0.56f, 0.08f}
    };
    V3 prev_front{}, prev_ridge{}, prev_back{};
    for (const Section& s : arm) {
        const V3 front = place(s.span, s.leading, 0.0f);
        const V3 ridge = place(s.span, s.leading + (s.trailing - s.leading) * 0.43f, s.arch);
        const V3 back = place(s.span, s.trailing, -0.07f);
        if (s.span > 0) {
            mesh.flat(prev_front, front, ridge, plumage);
            mesh.flat(prev_front, ridge, prev_ridge, plumage);
            mesh.flat(prev_ridge, ridge, back, flights);
            mesh.flat(prev_ridge, back, prev_back, flights);
        }
        prev_front = front;
        prev_ridge = ridge;
        prev_back = back;
    }

    // The outer primaries reach farthest. The inner ones sweep toward
    // the tail. Their vanes overlap at the wrist and separate near the tips.
    struct Feather { float span, aft; };
    constexpr Feather tips[] = {
        {0.96f, 0.12f}, {1.00f, 0.41f}, {0.98f, 0.74f},
        {0.91f, 1.03f}, {0.80f, 1.20f}, {0.66f, 1.23f}
    };
    for (int i = 0; i < 6; ++i) {
        const Feather tip = tips[i], before = tips[std::max(0, i - 1)], after = tips[std::min(5, i + 1)];
        const float width = i == 0 || i == 5 ? 0.69f : 0.35f;
        const float ds = (after.span - before.span) * width, da = (after.aft - before.aft) * width;
        const float root_span = 0.66f - i * 0.045f, root_aft = 0.05f + i * 0.145f;
        const auto vane = [&](float u, float side, float loft) {
            return place(root_span + (tip.span - root_span) * u + side * ds,
                         root_aft + (tip.aft - root_aft) * u + side * da, loft);
        };
        const V3 root = vane(0, 0, 0.0f), left = vane(0.48f, -1, -0.03f), right = vane(0.48f, 1, -0.06f);
        const V3 shaft = vane(0.62f, 0, 0.11f);
        const V3 outer_left = vane(0.86f, -0.72f, 0), outer_right = vane(0.86f, 0.72f, -0.03f);
        const V3 end = vane(1, 0, 0.01f);
        mesh.flat(root, left, shaft, plumage);
        mesh.flat(left, outer_left, shaft, plumage);
        mesh.flat(outer_left, end, shaft, plumage);
        mesh.flat(root, shaft, right, flights);
        mesh.flat(shaft, outer_right, right, flights);
        mesh.flat(shaft, end, outer_right, flights);
    }
}

} // namespace

float ride_height(const Body& body) {
    // Legs under the body hold it at nearly their length; sprawled, lower.
    return body.leg_pairs ? body.leg_length * (0.86f - 0.45f * body.sprawl) + body.girth * 0.7f : body.girth;
}

void build(std::vector<Vertex>& out, const Body& b, const Pose& pose, const Ground& ground, std::vector<Foot>* feet,
           Detail detail) {
    const bool ornaments = detail == Detail::full && 2 * b.girth >= 5 * pose.grain;
    const bool fish = b.leg_pairs == 0 && b.side_fins > 0;
    const bool winged = b.wing > 0, low = b.sprawl > 0.55f;
    Mesh mesh{out};
    const V3 fore{std::cos(pose.heading), std::sin(pose.heading), 0}, side{-fore.y, fore.x, 0}, up{0, 0, 1};
    const V3 at{pose.x, pose.y, 0};
    const Paint skin{{b.primary.r, b.primary.g, b.primary.b, lit},
                     {b.secondary.r, b.secondary.g, b.secondary.b, std::uint8_t(b.pattern)}};
    const Paint limb{darker(skin.primary, 0.82f), {b.secondary.r, b.secondary.g, b.secondary.b, std::uint8_t(Pattern::plain)}};
    const bool walks = pose.medium == Medium::land && b.leg_pairs > 0;
    const float beat = pose.phase * turn;
    const float L = b.length, G = b.girth;
    const auto leg_station = [&](int pair) {
        return b.leg_pairs == 1 ? 0.0f : L * (0.36f - 0.72f * float(pair) / float(b.leg_pairs - 1));
    };

    // The middle of the body, `along` cells ahead of its centre. On land it
    // follows the ground under its hips and shoulders, so it tips on a slope.
    const float rear = ground(pose.x - fore.x * L * 0.35f, pose.y - fore.y * L * 0.35f);
    const float front = ground(pose.x + fore.x * L * 0.35f, pose.y + fore.y * L * 0.35f);
    const float ride = ride_height(b);
    const float raised = fish ? G * 0.15f : low ? G * 0.28f : b.neck * 1.1f + G * 0.65f;
    const float skull = L * 0.5f + b.neck;
    const float nose = skull + b.head + b.snout;
    const auto& g = pose.gesture;
    V3 reach{};
    switch (g.action) {
    case Action::feed: {
        reach = fore * (L * (0.08f + 0.19f * g.forward)) + side * (L * 0.43f * g.side);
        float target = g.food_height;
        if (pose.medium == Medium::land) {
            const V3 mouth = at + fore * nose + reach;
            const float resting = rear + (front - rear) * (nose / (L * 0.7f) + 0.5f) + ride + raised * 0.9f;
            target = ground(mouth.x, mouth.y) + std::max(b.head * 0.75f, g.food_height) - resting;
        }
        reach.z = std::clamp(target, -(ride + raised + G), L * 0.38f + b.neck);
        break;
    }
    case Action::mate:
        reach = fore * (L * (0.06f + 0.16f * g.forward)) + side * (L * 0.40f * g.side);
        reach.z = 0.14f * L + 0.25f * G;
        break;
    case Action::strike:
        reach = fore * (L * (0.16f + 0.38f * g.forward)) + side * (L * 0.46f * g.side);
        reach.z = -0.52f * (G + raised);
        break;
    case Action::recoil:
        reach = fore * (L * 0.30f * g.forward) + side * (L * 0.42f * g.side);
        reach.z = G * 0.9f;
        break;
    case Action::call:
        reach = fore * (-L * 0.16f);
        reach.z = L * 0.26f + G * 0.74f;
        break;
    case Action::idle: break;
    }
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
        V3 p = at + fore * along + side * sway + up * (z + lift);
        if (g.amount != 0) {
            // Plant the hips. The chest starts the gesture, the neck carries
            // most of it and the muzzle follows through with the eyes.
            const float t = std::clamp((along + L * 0.20f) / (nose + L * 0.20f), 0.0f, 1.0f);
            p = p + reach * (t * t * (3 - 2 * t) * g.amount);
            if (g.action == Action::mate && along < -L * 0.5f && b.tail > 0) {
                const float tail = std::min(1.0f, (-along - L * 0.5f) / b.tail);
                p = p + (up * 0.30f - side * (0.32f * g.side)) * (b.tail * tail * tail * g.amount);
            }
        }
        return p;
    };

    // The spine, from the tip of the tail to the snout: where each ring is,
    // how thick, and how far it is raised.
    struct Station {
        float along, radius, lift;
        bool present = true;
    };
    Station stations[] = {
        {-L * 0.5f - b.tail, G * 0.06f, walks ? -G * 0.1f : 0.0f, b.tail > 0},
        {-L * 0.5f - b.tail * 0.5f, G * (fish ? 0.26f : 0.24f), 0, b.tail > 0},
        {-L * 0.5f, G * 0.49f, 0}, {-L * (fish ? 0.32f : 0.36f), G * 0.73f, 0},
        {fish ? -L * 0.07f : 0, G * 0.96f, 0}, {L * (fish ? 0.22f : 0.36f), G, 0},
        {L * 0.43f, G * 0.81f, 0}, {L * 0.49f + b.neck * 0.49f, G * (fish ? 0.60f : 0.46f), fish ? 0 : raised * 0.74f},
        {skull, b.head * 0.85f, raised + b.head * 0.05f}, {skull + b.head * 0.52f, b.head, raised + b.head * 0.17f},
        {skull + b.head, b.head * (fish || low ? 0.84f : 0.61f), raised - b.head * 0.17f},
        {nose, b.head * (winged ? 0.10f : low ? 0.54f : 0.35f), raised - b.head * (winged ? 0.51f : 0.29f)}
    };
    if (!fish && L > 0) {
        // Put mass over the limb roots. Leave a narrower waist between pairs.
        for (int i = 2; i <= 6; ++i) {
            const auto fullness = [&](float joint) {
                const float t = std::max(0.0f, 1.0f - std::abs(stations[i].along - joint) / (0.32f * L));
                return t * t * (3 - 2 * t);
            };
            float mass = winged ? 0.48f * fullness(0.29f * L) : 0;
            for (int pair = 0; pair < b.leg_pairs; ++pair) {
                const float strength = pair == 0 || pair == b.leg_pairs - 1 ? 0.50f : 0.25f;
                mass = std::max(mass, strength * fullness(leg_station(pair)));
            }
            stations[i].radius = G * (0.88f - 0.48f * std::abs(stations[i].along / L) + mass);
            stations[i].lift = G * (0.05f + 0.19f * mass) + (i == 6 ? 0.23f * raised : 0);
        }
    }
    const float depth = fish ? 0.90f : low ? 0.61f : 0.97f;
    V3 points[std::size(stations)];
    float radii[std::size(stations)];
    float axial[std::size(stations)];
    int count = 0;
    for (const Station& s : stations) {
        if (s.present) {
            axial[count] = s.along;
            points[count] = spine(s.along, s.lift);
            radii[count++] = s.radius;
        }
    }
    if (detail == Detail::distant) {
        // At this scale the tail, belly, brow and muzzle define the outline.
        const V3 hull[] = {points[0], spine(-L * 0.28f, 0), spine(L * 0.20f, 0), points[count - 3], points[count - 1]};
        const float width = std::max(G, pose.grain * 0.56f);
        const float widths[] = {radii[0], width * 0.94f, width, b.head, radii[count - 1]};
        mesh.tube<4>(hull, widths, 5, 0.9f, skin);
    } else if (ornaments && (b.mouth_hardness > 0 || b.front_hardness > 0 || b.back_hardness > 0)) {
        float strength[std::size(stations)];
        int ring = 0;
        for (unsigned i = 0; i < std::size(stations); ++i) {
            if (!stations[i].present) continue;
            strength[ring++] = i < 2 ? 0 : i <= 4 ? b.back_hardness : i <= 9 ? b.front_hardness : b.mouth_hardness;
        }
        mesh.tube<8, true>(points, radii, count, depth, skin, 0, 1, nullptr, strength, winged);
    } else {
        mesh.tube<8>(points, radii, count, depth, skin);
    }
    if (b.eye > 0 && b.head > 0) {
        // Take the same ring and axes as the head mesh, including its tilt on
        // a slope. Each socket sits halfway up the first facet on its side.
        const int brow = count - 3;
        const bool distant = detail == Detail::distant;
        const V3 ahead = distant ? normalize(points[count - 1] - spine(L * 0.20f, 0), fore)
                                 : normalize(points[brow + 1] - points[brow - 1], fore);
        const V3 across = normalize(cross(up, ahead), side);
        const V3 head_up = normalize(cross(ahead, across));
        for (const float s : {-1.0f, 1.0f}) {
            // Interpolate the upper cheek face on the four- or eight-sided
            // head. Its height and normal follow the head's actual flattening.
            const float flat = distant ? 0.9f : depth;
            const V3 outward = normalize(across * s + head_up * ((distant ? 1.f : 0.414214f) / flat));
            const V3 cheek = across * (s * (distant ? 0.58f : 0.7657f)) +
                             head_up * (flat * (distant ? 0.42f : 0.5657f));
            mesh.eye(points[brow] + cheek * b.head + outward * (b.eye * 0.10f), outward, b.eye);
        }
    }
    if (ornaments && b.frill) {
        Paint accent = skin;
        accent.primary = skin.secondary;
        accent.secondary.a = std::uint8_t(Pattern::plain);
        for (float s : {-1.0f, 1.0f}) {
            const V3 out = side * s;
            const V3 base = points[count - 4] - fore * (b.head * 0.35f) + up * (b.head * (low ? 0.12f : fish ? 0.20f : 0.43f));
            const V3 root = base + out * (b.head * 0.52f);
            const V3 front = root + fore * (b.head * 0.46f);
            const V3 back = root - fore * (b.head * 0.76f);
            const V3 ridge = root + out * (b.head * 0.82f) + up * (b.head * (low ? 0.40f : 0.88f));
            const V3 tip = root + out * (b.head * (low ? 1.42f : fish ? 1.22f : winged ? 0.52f : 1.08f))
                         - fore * (b.head * (winged ? 2.40f : fish || low ? 1.72f : 0.70f))
                         + up * (b.head * (low ? 0.40f : winged ? 1.03f : fish ? 0.35f : 1.67f));
            const V3 mid_front = (front + tip) * 0.5f + fore * (0.17f * b.head);
            const V3 mid_back = (back + tip) * 0.5f - fore * (0.24f * b.head);
            mesh.flat(front, ridge, mid_front, skin);
            mesh.flat(mid_front, ridge, tip, accent);
            mesh.flat(tip, ridge, mid_back, skin);
            mesh.flat(mid_back, ridge, back, skin);
            mesh.flat(front, back, ridge, skin);
            mesh.flat(back, front, tip, skin);
        }
        if (fish || low) {
            for (int i = 0; i < (fish ? 1 : 3); ++i) {
                const float along = L * (0.15f - i * 0.27f);
                const V3 a = spine(along + 0.14f * L, G * (low ? 0.38f : 0.98f));
                const V3 c = spine(along - (fish ? 0.51f : 0.16f) * L, G * (low ? 0.34f : 0.7f));
                const V3 tip = a - fore * (L * (fish ? 0.19f : 0.12f)) + up * (G * (fish ? 1.53f : 0.85f));
                const V3 ridge = (a + c) * 0.5f + side * (G * 0.20f);
                mesh.flat(a, ridge, tip, accent);
                mesh.flat(ridge, c, tip, skin);
            }
        }
    }

    // All three gaits use the same hips, bones and skin. The medium sets
    // the ankle target and the direction the knee folds toward.
    if (b.leg_pairs > 0) {
        const float thigh = b.leg_length * 0.53f, stride = b.leg_length * 0.75f * pose.pace;
        for (int pair = 0; pair < b.leg_pairs; ++pair) {
            const float along = leg_station(pair);
            const V3 middle = spine(along, 0);
            for (int k = 0; k < 2; ++k) {
                const float s = k ? 1.0f : -1.0f;
                // Neighbouring legs take opposite halves of the stride or paddle.
                const float cycle = pose.phase + ((pair + k) % 2 ? 0.5f : 0.0f);
                const float t = cycle - std::floor(cycle);
                // The hip is inside the body, low under it for legs that stand
                // beneath it and out at its flank for legs that sprawl, so the
                // top of the leg comes out of the body and not off its skin.
                const V3 hip = middle + side * (s * G * (0.67f + 0.3f * b.sprawl)) - up * (G * (0.45f - 0.35f * b.sprawl));
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
                if (detail == Detail::distant) {
                    const V3 edge = fore * shin;
                    mesh.flat(hip, knee + edge, knee - edge, limb);
                    mesh.flat(knee - edge, knee + edge, leg[3], limb);
                    continue;
                }
                const float joint = std::max(shin * 1.3f, G * (0.84f - 0.22f * b.sprawl));
                // Shape the thigh and sole on the four original joint rings.
                const float ankle = std::max(shin * 0.72f, pose.grain * 0.5f);
                const float toe = walks ? std::max(shin * 0.82f, G * 0.24f) : std::max(shin * 0.6f, pose.grain * 0.5f);
                const float widths[] = {joint, std::max(shin * 1.20f, joint * 0.43f), ankle, toe};
                Paint top = skin;
                top.secondary.a = std::uint8_t(Pattern::plain);
                mesh.tube<5>(leg, widths, 4, 1, top, 0, 1, &limb);
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

    if (b.wing <= 0) return;
    if (pose.medium != Medium::air) {
        // Roll the wing around the ribcage, bringing the primaries together
        // over the hip. Each feather follows the local slope of the spine.
        const float reach = std::min(b.wing, L * 0.98f);
        Paint folded = limb;
        const auto feather = [](int a, int b) { return std::uint8_t(0.54f * a + 0.32f * b + 23); };
        folded.primary = {feather(b.primary.r, b.secondary.r), feather(b.primary.g, b.secondary.g),
                          feather(b.primary.b, b.secondary.b), lit};
        for (const float s : {-1.0f, 1.0f}) {
            feathered_wing(mesh, [&](float span, float aft, float loft) {
                const float along = L * 0.40f - reach * (1.03f * span + 0.32f * aft);
                int i = 1;
                while (i < count - 1 && axial[i] < along) ++i;
                const float t = std::clamp((along - axial[i - 1]) / std::max(0.00001f, axial[i] - axial[i - 1]), 0.f, 1.f);
                const V3 center = points[i - 1] + (points[i] - points[i - 1]) * t;
                const float radius = radii[i - 1] + (radii[i] - radii[i - 1]) * t + G * (0.28f + 0.52f * loft + 0.22f * span);
                const float angle = 0.29f + 1.09f * aft + 0.14f * span;
                // At a broad shoulder the fold rides higher on the back.
                const float width = std::min(radius * std::sin(angle), G * 1.18f);
                const float height = depth * std::sqrt(std::max(0.0f, radius * radius - width * width));
                V3 p = center + side * (s * width) + up * height;
                if (pose.medium == Medium::land) p.z = std::max(p.z, ground(p.x, p.y) + G * 0.12f);
                return p;
            }, folded, detail);
        }
        return;
    }

    const float shoulder_angle = 0.85f * std::sin(beat), rise = std::cos(beat);
    const float wrist_angle = shoulder_angle - rise * (0.16f + 0.94f * std::max(rise, 0.0f));
    const float chord = std::min(L * 0.76f, b.wing * 0.67f);
    for (const float s : {-1.0f, 1.0f}) {
        const V3 shoulder = spine(L * 0.29f, G * 0.50f) + side * (s * G * 0.45f);
        const V3 arm = side * (s * std::cos(shoulder_angle)) + up * std::sin(shoulder_angle);
        const V3 hand = side * (s * std::cos(wrist_angle)) + up * std::sin(wrist_angle);
        const V3 normal = up * std::cos(wrist_angle) - side * (s * std::sin(wrist_angle));
        feathered_wing(mesh, [&](float span, float aft, float loft) {
            const float outer = std::max(0.0f, span - 0.50f);
            const float sweep = outer * (0.34f + std::max(rise, 0.0f) * 0.32f);
            return shoulder + arm * (b.wing * (span - outer)) + hand * (b.wing * outer)
                 - fore * (chord * aft + b.wing * sweep) + normal * (G * loft);
        }, limb, detail);
    }
}

Pose portrait_pose(const Body& body, Medium medium) {
    Pose pose;
    pose.medium = medium;
    pose.height = ride_height(body);
    pose.heading = 0.49f;
    pose.pace = 0;
    pose.phase = medium == Medium::air ? 0.52f : 0.12f;
    return pose;
}

PortraitFrame frame_portrait(const Body& adult, int width, int height) {
    // Fit once at adult size. Include the origin so the same camera also
    // frames the juvenile as its limbs grow.
    float left = 0, right = 0, top = 0, bottom = 0, depth = 0;
    const float reach = adult.length + adult.neck + adult.leg_length + adult.girth * 3;
    std::vector<Vertex> mesh;
    for (Medium medium : {Medium::land, Medium::water, Medium::air}) {
        if (medium == Medium::air && adult.wing == 0) continue;
        Pose pose = portrait_pose(adult, medium);
        const auto include = [&](Gesture gesture) {
            pose.gesture = gesture;
            mesh.clear();
            build(mesh, adult, pose, [](float, float) { return 0.0f; });
            for (const auto& v : mesh) {
                const float y = v.y * PortraitFrame::sin_pitch - v.z * PortraitFrame::cos_pitch;
                const float eye = v.primary.a == unlit ? std::abs(v.along) : 0;
                left = std::min(left, v.x - eye);
                right = std::max(right, v.x + eye);
                top = std::min(top, y - eye);
                bottom = std::max(bottom, y + eye);
                depth = std::max(depth, std::abs(v.y * PortraitFrame::cos_pitch + v.z * PortraitFrame::sin_pitch));
            }
        };
        include({});
        include({Action::call, 1});
        // Corners of the direction square enclose all target bearings.
        for (float side : {-1.0f, 1.0f}) {
            for (float forward : {-1.0f, 1.0f}) {
                include({Action::feed, 1, side, forward, -reach});
                include({Action::feed, 1, side, forward, reach});
                include({Action::strike, 1, side, forward});
                include({Action::recoil, 1, side, forward});
                include({Action::mate, 1, side, forward});
            }
        }
    }
    const float zoom = std::min((width - 8) / (right - left), (height - 8) / (bottom - top));
    return {(left + right) / 2, (top + bottom) / 2, zoom, 1 / (depth + adult.girth + 1)};
}

} // namespace gfx::creature
