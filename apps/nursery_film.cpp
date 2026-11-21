#include "nursery_film.hpp"
#include "nursery_page.hpp"
#include "file.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>

namespace nursery {
namespace {
using namespace phd;

void write(const std::filesystem::path& dst, const std::string& s) {
    if (!write_file_atomic(dst.string(), {s.begin(), s.end()})) throw std::runtime_error("Could not write " + dst.string());
}

std::string array(const std::vector<std::string>& parts) {
    std::string out = "[";
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) out += ',';
        out += parts[i];
    }
    return out + ']';
}

std::string capture(World& w, const std::string& id, unsigned limit, unsigned candidate = 0, bool family = false) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\"seed\":" << w.seed() << ",\"candidate\":" << candidate << ",\"brain\":\"" << id << "\",\"box\":[0,0,"
        << w.width() << ',' << w.height() << "],\"bed\":[";
    std::vector<int> depths(w.cells());
    std::vector<std::size_t> reeds;
    const auto depth_at = [&](std::size_t c) { return w.water(c) == Water::none ? 0 : w.water_depth_dm(c); };
    for (std::size_t cell = 0; cell < w.cells(); ++cell) {
        if (cell) out << ',';
        int plants = 0, tallest = 0;
        for (int slot = 0; slot < 4; ++slot) {
            if (const auto* p = w.occupant(cell, slot)) {
                ++plants;
                tallest = std::max(tallest, int(p->height));
            }
        }
        depths[cell] = depth_at(cell);
        if (family && plants) reeds.push_back(cell);
        out << depths[cell] << ',' << w.elevation(cell)
            << ',' << plants << ',' << tallest << ',' << (w.tree(cell) != nullptr);
    }
    out << "],\"frames\":[";
    const unsigned interval = family ? 24 : candidate ? 4 : 6;
    std::array<int, 2> bites{};
    PondTick pass;
    std::set<std::uint32_t> born;
    std::uint64_t first_birth = 0, first_ready = 0, first_adult = 0, first_grandchild = 0;
    std::vector<std::string> water;
    for (;;) {
        int large = 0, small = 0, ready = 0;
        bool pond_event = false;
        if (!first_ready && (pass.founders[0].reached_ready || pass.founders[1].reached_ready)) first_ready = w.tick();
        for (const auto& a : w.animals()) {
            if (candidate && w.tick() && a.id <= 2) {
                if (!a.eating) ++bites[a.id - 1];
                if (!family) pond_event |= !a.eating || (w.tick() + a.id) % animal_stagger == 0;
            }
            if (a.stock == FishStock::small_fish) ++small;
            if (a.stock != FishStock::large_swimmer) continue;
            ++large;
            ready += a.growth == 255 && a.drive >= mating_drive;
            if (a.mother) {
                const bool newborn = born.insert(a.id).second;
                if (!first_birth) first_birth = w.tick();
                if (family) {
                    pond_event |= newborn;
                    if (a.mother <= 2 && a.father <= 2 && a.growth == 255 && !first_adult) first_adult = w.tick();
                    if ((a.mother > 2 || a.father > 2) && !first_grandchild) first_grandchild = w.tick();
                }
            }
        }
        const bool last = w.tick() == limit || (!candidate && !large);
        if (w.tick() && (w.tick() % (World::ticks_per_day / 2) == 0 || w.tick() == first_birth || last)) {
            std::ostringstream change;
            bool any = false;
            for (std::size_t i = 0; i < depths.size(); ++i) {
                const int current = depth_at(i);
                if (depths[i] == current) continue;
                if (any) change << ',';
                change << i << ',' << current;
                depths[i] = current;
                any = true;
            }
            if (any) water.push_back("[" + std::to_string(w.tick()) + ",[" + change.str() + "]]");
        }
        if (w.tick() % interval == 0 || pond_event || last || (family && (w.tick() == first_ready || w.tick() == first_adult))) {
            if (w.tick()) out << ',';
            out << '[' << w.tick() << ',' << large << ',' << small << ',' << ready << ',' << born.size()
                << ',' << w.hunts().kills << ",[";
            bool first = true;
            for (const auto& a : w.animals()) {
                if (a.stock == FishStock::other) continue;
                if (!first) out << ',';
                first = false;
                const auto body = frame(a.genome, a.growth);
                const unsigned flags = unsigned(a.stock == FishStock::large_swimmer) | (unsigned(a.mother != 0) << 1)
                    | (unsigned(a.pain > 24) << 2) | (unsigned(w.tick() && !a.eating) << 3)
                    | (unsigned(Medium(a.medium) == Medium::water) << 4);
                out << '[' << a.id << ',' << a.x << ',' << a.y << ',' << int(a.heading) << ',' << body.length << ','
                    << std::clamp(a.energy * 100 / body.store, 0, 100) << ',' << int(a.drive) << ',' << flags << ',' << a.stride;
                if (a.mother && a.stock == FishStock::large_swimmer) out << ',' << a.mother << ',' << a.father;
                else if (candidate && a.id <= 2) out << ',' << int(a.eating) << ',' << std::clamp(a.energy * 1000 / body.store, 0, 1000);
                out << ']';
            }
            out << "],[";
            first = true;
            for (const auto& c : w.carcasses()) {
                if (!c.energy) continue;
                if (!first) out << ',';
                first = false;
                out << '[' << c.x << ',' << c.y << ',' << c.length_mm << ',' << int(c.heading) << ']';
            }
            out << ']';
            if (candidate) out << ",[" << bites[0] << ',' << bites[1] << ',' << pass.birth_drives[0] << ',' << pass.birth_drives[1] << ']';
            if (family) {
                int stocked = 0;
                for (auto c : reeds) stocked += w.occupant(c, 0)->energy > 8;
                out << ',' << stocked;
            }
            out << ']';
        }
        if (last) break;
        w.step(candidate ? &pass : nullptr);
    }
    out << "],\"water\":" << array(water) << ",\"firstBirth\":" << first_birth << ",\"firstReady\":" << first_ready;
    if (family) out << ",\"family\":true,\"firstAdult\":" << first_adult << ",\"firstGrandchild\":" << first_grandchild
                    << ",\"reedCells\":" << reeds.size() << ",\"refillTicks\":" << w.config().pond_refill_ticks;
    out << ",\"fingerprint\":\"" << std::hex << std::setw(16) << std::setfill('0') << w.fingerprint() << "\"}";
    return out.str();
}
} // namespace

Film::Film(std::filesystem::path directory, unsigned lesson) : directory_(std::move(directory)), lesson_(lesson),
    session_(std::to_string(std::chrono::system_clock::now().time_since_epoch().count())) {}

void Film::add(const phd::WorldConfig& config, const phd::brain::Brain& brain, const std::string& id,
               int round, const char* role, const std::string& winner, int birth_maps, unsigned ticks) {
    const auto key = std::to_string(config.seed) + '/' + id + '/' + std::to_string(ticks);
    const auto index = std::size_t(std::find(keys_.begin(), keys_.end(), key) - keys_.begin());
    if (index == keys_.size()) {
        phd::World w(config);
        w.seed_animals(56, brain);
        takes_.push_back(capture(w, id, ticks));
        keys_.push_back(key);
    }
    chapters_.push_back("{\"take\":" + std::to_string(index) + ",\"round\":" + std::to_string(round)
        + ",\"role\":\"" + role + "\",\"winner\":\"" + winner + "\",\"birthMaps\":" + std::to_string(birth_maps) + "}");
}

void Film::add_pond(phd::World& start, const std::string& id, unsigned candidate, const char* panel,
                    int rank, const char* result, unsigned ticks, std::uint64_t expected_fingerprint, bool family) {
    const auto index = takes_.size();
    takes_.push_back(capture(start, id, ticks, candidate, family));
    if (start.fingerprint() != expected_fingerprint) throw std::runtime_error("Pond replay disagrees with scored trial");
    chapters_.push_back("{\"take\":" + std::to_string(index) + ",\"role\":\"pond\",\"panel\":\"" + panel
                        + "\",\"rank\":" + std::to_string(rank) + ",\"result\":\"" + result + "\"}");
}

void Film::publish(bool complete) const {
    const auto data = "{\"session\":\"" + session_ + "\",\"lesson\":" + std::to_string(lesson_) + ",\"complete\":" + (complete ? "true" : "false")
        + ",\"chapters\":" + array(chapters_) + ",\"takes\":" + array(takes_) + "}";
    std::string page(nursery_page);
    const std::string marker = "/*NURSERY_REEL*/null";
    page.replace(page.find(marker), marker.size(), data);
    write(directory_ / "nursery.html", page);
    write(directory_ / "nursery-live.js", "window.nurseryUpdate(\"" + session_ + "\"," + std::to_string(chapters_.size()) + ");");
}
} // namespace nursery
