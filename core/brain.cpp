#include "brain.hpp"

#include <algorithm>

namespace phd::brain {
namespace {

constexpr int clip(int value) { return value < -127 ? -127 : value > 127 ? 127 : value; }

// A unit's activation from its accumulator, where 64 is unity. The quotient
// truncates toward zero, which is the same on every compiler.
constexpr int activation(int sum) { return clip(sum / 64); }

// From Petri dc03377, core/brain.cpp: next.
std::uint32_t next(std::uint32_t& state) {
    std::uint32_t value = (state += 0x9e3779b9u);
    value = (value ^ (value >> 16)) * 0x85ebca6bu;
    value = (value ^ (value >> 13)) * 0xc2b2ae35u;
    return value ^ (value >> 16);
}

constexpr int output_base = hidden * unit_bytes;

} // namespace

Decision think(const Senses& senses, const Brain& brain) {
    Decision d;
    int unit[hidden];
    for (int h = 0; h < hidden; ++h) {
        const std::int8_t* w = brain.weight + h * unit_bytes;
        int sum = w[inputs] * 64;
        for (int i = 0; i < inputs; ++i) {
            sum += w[i] * senses.in[i];
        }
        unit[h] = activation(sum);
        d.unit[h] = static_cast<std::int8_t>(unit[h]);
    }
    int out[outputs];
    for (int o = 0; o < outputs; ++o) {
        const std::int8_t* w = brain.weight + output_base + o * output_bytes;
        int sum = w[hidden] * 64;
        for (int h = 0; h < hidden; ++h) {
            sum += w[h] * unit[h];
        }
        out[o] = activation(sum);
        d.out[o] = static_cast<std::int8_t>(out[o]);
    }
    d.turn = static_cast<std::int8_t>(std::clamp(out[out_turn] / 16, -6, 6));
    d.pace = static_cast<std::uint8_t>(std::clamp(128 + out[out_pace], 0, 255));
    d.eat = out[out_eat] > 0;
    d.mate = out[out_mate] > 0;
    d.fly = out[out_fly] > 0;
    d.call = out[out_call] > 0;
    // Attack is an aimed action. The mouth sense must have found contact
    // when the decision was made; two ticks of windup still allow an escape.
    d.attack = out[out_attack] > 0 && senses.in[self_input(self_mouth_size)] != 127;
    d.rest = out[out_rest] > 0;
    for (int m = 0; m < memories; ++m) {
        d.memory[m] = static_cast<std::int8_t>(out[out_memory0 + m]);
    }
    return d;
}

void combine(Brain& child, const Brain& first, const Brain& second, std::uint32_t& state) {
    const auto pick = [&]() -> const Brain& { return next(state) & 1u ? second : first; };
    for (int h = 0; h < hidden; ++h) {
        const Brain& source = pick();
        for (int i = 0; i < unit_bytes; ++i) {
            child.weight[h * unit_bytes + i] = source.weight[h * unit_bytes + i];
        }
        for (int o = 0; o < outputs; ++o) {
            child.weight[output_base + o * output_bytes + h] = source.weight[output_base + o * output_bytes + h];
        }
    }
    for (int o = 0; o < outputs; ++o) {
        const int bias = output_base + o * output_bytes + hidden;
        child.weight[bias] = pick().weight[bias];
    }
}

int mutate(Brain& brain, std::uint32_t& state) {
    int changes = 0;
    for (int i = 0; i < bytes; ++i) {
        const auto roll = next(state);
        if ((roll & 127u) == 0) {
            const int step = 1 + int((roll >> 7) & 3u);
            const int weight = clip(brain.weight[i] + ((roll >> 9) & 1u ? step : -step));
            changes += weight != brain.weight[i];
            brain.weight[i] = static_cast<std::int8_t>(weight);
        }
    }
    return changes;
}

void shuffle(Brain& brain, std::uint32_t& state) {
    for (int i = bytes - 1; i > 0; --i) {
        const int j = int(next(state) % std::uint32_t(i + 1));
        std::swap(brain.weight[i], brain.weight[j]);
    }
}

Brain founder(bool hunts, bool spares_kin, bool pursues) {
    Brain b{};
    const auto in = [&](int unit, int input) -> std::int8_t& { return b.weight[unit * unit_bytes + input]; };
    const auto bias = [&](int unit) -> std::int8_t& { return b.weight[unit * unit_bytes + inputs]; };
    const auto out = [&](int output, int unit) -> std::int8_t& { return b.weight[output_base + output * output_bytes + unit]; };
    const auto out_bias = [&](int output) -> std::int8_t& { return b.weight[output_base + output * output_bytes + hidden]; };

    // 0: grazing to the right more than to the left. It turns that way.
    in(0, sector_input(right, in_food)) = 64, in(0, sector_input(far_right, in_food)) = 48;
    in(0, sector_input(left, in_food)) = -64, in(0, sector_input(far_left, in_food)) = -48;
    out(out_turn, 0) = 64;
    // 1: ground it cannot go on to the left more than to the right. It turns away.
    // Gently: grazing is often at the edge of where it can go.
    in(1, sector_input(left, in_hazard)) = 40, in(1, sector_input(far_left, in_hazard)) = 12;
    in(1, sector_input(right, in_hazard)) = -40, in(1, sector_input(far_right, in_hazard)) = -12;
    out(out_turn, 1) = 64;
    // 2: its next steps lead where it cannot go. It turns right, hard, and slows.
    in(2, self_input(self_ahead_hazard)) = 64;
    out(out_turn, 2) = 127, out(out_pace, 2) = -32;
    // 3: restlessness. It never walks a straight line for long.
    in(3, self_input(self_noise)) = 8;
    out(out_turn, 3) = 64;
    // 4: hunger, below about three quarters full. It eats.
    in(4, self_input(self_energy)) = -64, bias(4) = 92;
    out(out_eat, 4) = 64;
    // 5: ready to mate. It tries to.
    in(5, self_input(self_drive)) = 64, bias(5) = -98;
    out(out_mate, 5) = 64;
    // 6: a mate to the right more than to the left. It turns that way, and
    // that outweighs grazing.
    in(6, sector_input(right, in_mate)) = 64, in(6, sector_input(far_right, in_mate)) = 64;
    in(6, sector_input(left, in_mate)) = -64, in(6, sector_input(far_left, in_mate)) = -64;
    out(out_turn, 6) = 127;
    // 7: nothing to eat here or in sight. One with wings takes to the air
    // until there is.
    for (int s = 0; s < sectors; ++s) {
        in(7, sector_input(s, in_food)) = -32;
    }
    in(7, self_input(self_food)) = -64, in(7, self_input(self_energy)) = -24, bias(7) = 40;
    out(out_fly, 7) = 64;
    // (8 is left blank. Following the smell of grazing sounds useful and was
    // not: every founder made for the same lush places and starved there
    // together. What to do with that smell is for evolution to find.)
    // 9: a mate's scent stronger to the right. It turns that way.
    in(9, self_input(smell_mate_right)) = 64;
    out(out_turn, 9) = 64;
    // 10: ready to mate. It calls.
    in(10, self_input(self_drive)) = 64, bias(10) = -98;
    out(out_call, 10) = 64;
    // 11: a call in a voice like its own, to the right. It turns toward it.
    in(11, self_input(heard_right)) = 64;
    out(out_turn, 11) = 48;
    // A fresh hit from the right turns the founder left and increases its pace.
    in(12, self_input(pain_right)) = -64;
    out(out_turn, 12) = 96;
    in(13, self_input(self_pain)) = 64;
    out(out_pace, 13) = 80;
    out_bias(out_attack) = -64;
    // It lies still only when it has cause. One that does not hunt has a
    // unit to spare for it: spent, below about a fifth of its wind, and not
    // just hurt, it rests until it has some back.
    out_bias(out_rest) = -64;
    if (!hunts) {
        in(14, self_input(self_stamina)) = -127, bias(14) = 60;
        out(out_rest, 14) = 127, out_bias(out_rest) = -16;
        out(out_rest, 13) = -127;
    }
    if (hunts) {
        // The large swimmer starts with two heritable reflexes. A smaller body
        // at its mouth and hunger favour a strike. Smaller bodies in view draw a turn.
        in(14, self_input(self_mouth_size)) = -96;
        out(out_attack, 14) = 40;
        out(out_attack, 4) = 64;
        out_bias(out_attack) = -96;
        in(15, sector_input(left, in_size)) = 40, in(15, sector_input(far_left, in_size)) = 24;
        in(15, sector_input(right, in_size)) = -40, in(15, sector_input(far_right, in_size)) = -24;
        out(out_turn, 15) = 16;
    }
    if (hunts && pursues) {
        // The opportunist above strikes more readily the smaller its prey,
        // and at a body near its own size only when it is nearly starved:
        // too late. A hunter strikes any body right in front of it, less
        // readily one bigger than itself, whenever it is less than full.
        in(14, self_input(self_mouth_size)) = -64;
        in(14, sector_input(ahead, in_animal)) = 127, bias(14) = -127;
        out(out_attack, 14) = 64;
        out(out_turn, 15) = 127;
        // (It is not wired to sprint. Running flat out at whatever is ahead was
        // tried and halved what it caught: when to spend its wind is for the
        // hunting school and for evolution to find.)
    }
    if (hunts && spares_kin) {
        // 8: its own kind in front of it. It does not strike: without this a
        // hungry parent's first prey is its own young.
        in(8, sector_input(ahead, in_kin)) = 64;
        in(8, sector_input(left, in_kin)) = 32, in(8, sector_input(right, in_kin)) = 32;
        out(out_attack, 8) = -127;
    }
    // It keeps a steady pace unless something slows it.
    out_bias(out_pace) = 40;
    return b;
}

} // namespace phd::brain
