#pragma once

#include "creature.hpp"
#include "math.hpp"
#include "world.hpp"

#include <unordered_map>

namespace gfx {

// Short motions belong to the viewer. Their clock follows simulation time,
// including fractional ticks while playing and the last drawn pose on pause.
class CreatureActions final : public phd::StepObserver {
    using Action = creature::Action;
    struct Event {
        creature::Gesture gesture;
        std::uint64_t tick = 0;
        int duration = 0;
    };
    std::unordered_map<std::uint32_t, Event> events_;
    double time_ = 0;

    void start(const phd::World& world, const phd::Animal& a, Action action, int duration,
               int bearing, float food_height = 0) {
        auto& event = events_[a.id];
        // Let a mouthful finish even if the next tick takes another. A hit
        // interrupts a bite, and a bite interrupts a feeding or calling pose.
        if (world.tick() - event.tick < std::uint64_t(event.duration) && event.gesture.action >= action) return;
        const int relative = bearing - a.heading;
        event = {{action, 0, phd::isin(relative) / 1024.0f, phd::icos(relative) / 1024.0f, food_height},
                 world.tick(), duration};
    }

public:
    void set_time(double tick) { time_ = tick; }

    creature::Gesture gesture(const phd::Animal& a) const {
        const auto found = events_.find(a.id);
        if (found == events_.end()) return {};
        const Event& e = found->second;
        const float t = float((time_ - double(e.tick)) / e.duration);
        if (t < 0 || t >= 1) return {};
        const auto ease = [](float v) {
            v = std::clamp(v, 0.0f, 1.0f);
            return v * v * (3 - 2 * v);
        };
        auto g = e.gesture;
        switch (g.action) {
        case Action::strike:
            g.amount = -0.28f * ease(t / 0.2f) + 1.28f * ease((t - 0.2f) / 0.22f) - ease((t - 0.42f) / 0.58f);
            break;
        case Action::feed:
            g.amount = ease(t / 0.40f) * (1 - ease((t - 0.68f) / 0.32f));
            break;
        case Action::mate:
            // Lean in together, ease apart a little, then nuzzle again.
            g.amount = ease(t / 0.28f) - 0.55f * ease((t - 0.28f) / 0.20f)
                     + 0.55f * ease((t - 0.48f) / 0.22f) - ease((t - 0.70f) / 0.30f);
            break;
        case Action::recoil:
            g.amount = ease(t / 0.20f) * (1 - ease((t - 0.20f) / 0.80f));
            break;
        case Action::call:
            g.amount = ease(t / 0.32f) * (1 - ease((t - 0.54f) / 0.46f));
            break;
        case Action::idle: break;
        }
        return g;
    }

    void ate_at(const phd::World& w, const phd::Animal& a, int, bool meat, int x, int y, int height_dm) override {
        const int bearing = x == a.x && y == a.y ? a.heading : phd::angle_of(x - a.x, y - a.y);
        if (phd::Medium(a.medium) == phd::Medium::water) {
            // Carcasses float. Plants grow from the bed.
            const auto cell = std::size_t(y >> 8) * w.width() + (x >> 8);
            height_dm = a.z + (meat ? 0 : height_dm - w.water_depth_dm(cell));
        }
        start(w, a, Action::feed, 7, bearing, float(height_dm) / phd::cell_decimetres);
    }
    void struck(const phd::World& w, const phd::Animal& a, const phd::Animal* victim, int damage) override {
        const int bearing = victim && (victim->x != a.x || victim->y != a.y)
                            ? phd::angle_of(victim->x - a.x, victim->y - a.y) : a.heading;
        start(w, a, Action::strike, 5, bearing);
        if (victim && damage > 0) start(w, *victim, Action::recoil, 4, bearing);
    }
    void called(const phd::World& w, const phd::Animal& a) override { start(w, a, Action::call, 6, a.heading); }
    void mated(const phd::World& w, const phd::Animal& a, const phd::Animal& b) override {
        const int bearing = a.x == b.x && a.y == b.y ? a.heading : phd::angle_of(b.x - a.x, b.y - a.y);
        start(w, a, Action::mate, 14, bearing);
        start(w, b, Action::mate, 14, bearing + 128);
    }
    void died(const phd::World&, const phd::Animal& a, phd::DeathCause, std::uint32_t) override { events_.erase(a.id); }
};

} // namespace gfx
