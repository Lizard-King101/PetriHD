#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "animals.hpp"
#include "biome.hpp"
#include "brain.hpp"
#include "founders.hpp"
#include "plants.hpp"
#include "random.hpp"
#include "terrain.hpp"

namespace phd {

struct WorldConfig {
    std::uint32_t seed = 1;
    int width = 384;
    int height = 224;
    int ocean_percent = 20;
    // The valley's climate: mean temperature over the year and the swing
    // between summer and winter, in tenths of a degree C.
    int climate = 90;
    int seasons = 140;
    Layout layout = Layout::valley;
    std::uint8_t hungry_drive_loss = usual_hungry_drive_loss; // drive lost per metabolic pass below half energy
    // An arena's settings (Layout::arena): everything about it is one of these.
    std::uint16_t arena_depth_dm = 20; // of its water
    std::uint16_t arena_food = 600;    // the most edible energy a planted cell holds
    // With arena_regrow zero, every planted cell is filled this often.
    std::uint32_t arena_refill_ticks = 1;
    std::uint8_t arena_light = 200;    // steady, unless arena_days
    std::uint16_t arena_land = 0;      // columns of dry ground along the west side; the rest is water
    // Edible energy a planted cell gains each metabolic pass (16 ticks), in
    // sixteenths, up to arena_food. A cell grazed bare stays bare until it
    // has grown back.
    std::uint16_t arena_regrow = 0;
    bool arena_days = false;           // day and night as in a valley
};

// A rectangular bed of fodder at an arena's creation. The plants themselves are saved.
struct ArenaPatch {
    int x, y, width, height;
    bool contains(int cx, int cy) const { return cx >= x && cy >= y && cx - x < width && cy - y < height; }
};

class World;
enum class DeathCause : std::uint8_t { starved, suffocated, killed };
enum class EnergyUse : std::uint8_t { upkeep, stamina, call, growth, healing, reproduction };

// Watches a step without touching it. A world steps the same, to the bit,
// whether or not anything is watching: this is how the nursery and tests
// measure what animals do, in place of counters built into the simulation.
struct StepObserver {
    virtual ~StepObserver() = default;
    virtual void born(const World&, const Animal& /*young*/, const Animal& /*mother*/, const Animal& /*father*/) {}
    virtual void died(const World&, const Animal&, DeathCause, std::uint32_t /*killer*/) {}
    // A lunge, before its wound is dealt: at nothing, or at the body it reached.
    virtual void struck(const World&, const Animal& /*attacker*/, const Animal* /*victim*/, int /*damage*/) {}
    // Energy it absorbed from one mouthful.
    virtual void ate(const World&, const Animal&, int /*gained*/, bool /*meat*/) {}
    // Food position in 1/256 cells and plant height in dm. Existing observers
    // can keep counting meals without using their position.
    virtual void ate_at(const World& world, const Animal& animal, int gained, bool meat, int, int, int) {
        ate(world, animal, gained, meat);
    }
    // Carcass provenance, while preserving existing meal observers.
    virtual void ate_carcass(const World& world, const Animal& animal, int gained, const Carcass& carcass) {
        ate_at(world, animal, gained, true, carcass.x, carcass.y, 0);
    }
    // Food energy charged, including unpaid costs while starving. Newborn
    // reserves are a transfer within a family, so only their tissue is charged.
    virtual void spent(const World&, const Animal&, int, EnergyUse) {}
    virtual void called(const World&, const Animal&) {}
    virtual void mated(const World&, const Animal&, const Animal&) {}
    // Each animal's metabolic pass, after upkeep, growth and its drive are
    // settled and before it can mate.
    virtual void metabolised(const World&, const Animal&, const Frame&) {}
};

enum class Water : std::uint8_t { none, ocean, lake, river };

// A storm front: a moving disc of cloud that carries water from the sea and
// drops it as it goes, more where it climbs.
struct Front {
    std::int32_t x, y;   // 1/256 cell
    std::int16_t vx, vy; // 1/256 cell per tick
    std::uint16_t radius;// cells
    std::uint16_t life;  // ticks left
    std::int32_t load;   // water carried
    std::int16_t last_height;
};

struct Lightning {
    std::uint64_t tick;
    std::int16_t x, y;
};

struct LakeState {
    std::uint16_t level;  // 0..1024 of the way from bottom to spill
    std::uint32_t inflow; // since the last hydrology pass
};

// The world of milestone 1: land and water, climate, day and night, seasons
// and weather. Everything is allocated at creation, never inside a tick.
class World {
public:
    static constexpr int ticks_per_day = 240;
    static constexpr int days_per_year = 16;
    static constexpr int ticks_per_year = ticks_per_day * days_per_year;
    static constexpr int node = 8; // cells per weather node
    static constexpr int max_fronts = 6;
    static constexpr int lightning_kept = 16;
    static constexpr int max_plants = 262144;
    static constexpr int max_animals = 4096;
    static constexpr int plant_stagger = 16; // each plant grows every 16 ticks

    // An arena with no beds given is planted in every cell.
    explicit World(const WorldConfig& config, const std::vector<ArenaPatch>& fodder_beds = {});

    void step(StepObserver* observer = nullptr);

    std::uint64_t fingerprint() const;
    std::uint64_t tick() const { return tick_; }
    std::uint32_t seed() const { return config_.seed; }
    const WorldConfig& config() const { return config_; }
    int width() const { return config_.width; }
    int height() const { return config_.height; }
    std::size_t cells() const { return std::size_t(config_.width) * config_.height; }
    std::size_t memory_bytes() const;

    // Time. Angles are binary (256 a turn); the year starts at the spring equinox.
    int season_angle() const { return int((tick_ % ticks_per_year) * 256 / ticks_per_year); }
    int season() const { return int((tick_ % ticks_per_year) / (ticks_per_year / 4)); } // 0 spring .. 3 winter
    int day_angle() const { return int((tick_ % ticks_per_day) * 256 / ticks_per_day); } // 0 midnight
    int daylight() const; // 0 night .. 255 noon

    // The static land.
    const Terrain& terrain() const { return terrain_; }
    int elevation(std::size_t i) const { return terrain_.elevation[i]; } // decimetres
    Substrate substrate(std::size_t i) const { return Substrate(terrain_.substrate[i]); }

    // The climate and water, per cell.
    Water water(std::size_t i) const { return Water(water_[i]); }
    int water_depth_dm(std::size_t i) const { return depth_[i]; } // decimetres
    int lake_surface_dm(std::uint16_t id) const { return lake_surfaces_[id].stage; }
    int temperature(std::size_t i) const { return temperature_[i]; } // tenths of a degree C
    int soil_water(std::size_t i) const { return soil_[i]; }         // 0..255
    int moisture(std::size_t i) const { return moisture_[i] >> 8; }  // long-run soil water
    int snow(std::size_t i) const { return snow_[i]; }
    int nutrients(std::size_t i) const { return nutrients_[i]; }
    std::uint32_t flow(std::size_t i) const { return flow_[i]; }
    Biome biome(std::size_t i) const { return Biome(biome_[i]); }
    const std::uint8_t* biomes() const { return biome_.data(); }

    // Weather, on a coarse grid of nodes every 8 cells.
    int nodes_x() const { return nodes_x_; }
    int nodes_y() const { return nodes_y_; }
    int rain(int nx, int ny) const { return rain_[std::size_t(ny * nodes_x_ + nx)]; }
    int rain_at(int x, int y) const; // local precipitation at a world cell, interpolated between weather nodes
    int cloud(int nx, int ny) const { return cloud_[std::size_t(ny * nodes_x_ + nx)]; }
    int wind_angle() const { return wind_angle_; }
    int wind_speed() const { return wind_speed_; } // 1/256 cell per tick
    const std::vector<Front>& fronts() const { return fronts_; }
    const std::array<Lightning, lightning_kept>& lightning() const { return lightning_; }
    const std::vector<LakeState>& lakes() const { return lakes_; }

    // Plants. A cell's slots 0-3 are its quarters; a tree fills all four.
    const Plant* occupant(std::size_t cell, int slot) const {
        const auto at = occupancy_[cell * 4 + std::size_t(slot)];
        return at ? &plants_[at - 1] : nullptr;
    }
    // The tree filling a cell, or null.
    const Plant* tree(std::size_t cell) const {
        const Plant* p = occupant(cell, 0);
        return p && p->slot == whole_cell ? p : nullptr;
    }
    const std::vector<Plant>& plant_pool() const { return plants_; }
    // Animals: only the living, in no particular order.
    const std::vector<Animal>& animals() const { return animals_; }
    bool has_had_animals() const { return next_animal_id_ != 1; }
    const std::vector<Carcass>& carcasses() const { return carcasses_; }
    const HuntStats& hunts() const { return hunts_; }
    // Puts `count` founders into the world, each where its body can live.
    // Each kind's founders take that kind's brains in turn.
    // `without` leaves kinds out, a bit for each FounderKind. The others are
    // placed exactly as they would be with every kind there.
    void seed_animals(int count, const FounderBrains& brains = FounderBrains::hand_wired(), unsigned without = 0);
    // Founders that come later, as a hunter does once there is something to
    // hunt: `count` of a kind, in families, wherever the kind can live.
    // Returns how many found a place.
    int arrive(FounderKind kind, int count, const FounderBrains& brains);
    // A family of any body: `count` of it, varied a little, near each other
    // somewhere it can live. Returns how many found a place.
    int settle(const AnimalGenome& body, int count, const brain::Brain& mind);
    // Puts one grown animal down at a place (1/256 of a cell), with this much
    // of its energy store and no drive to mate. Returns its id, or 0 if the
    // place is off the map or the world is full.
    std::uint32_t add_animal(const AnimalGenome& genome, int x, int y, std::uint8_t heading, int energy_percent,
                             const brain::Brain& brain, FishStock stock = FishStock::other);
    const brain::Brain& brain_of(std::size_t index) const { return brains_[index]; }
    // What the animal at this index senses now.
    brain::Senses senses(std::size_t index) const;
    BrainMode brain_mode() const { return brain_mode_; }
    const std::vector<Call>& calls() const { return calls_; }
    void set_brain_mode(BrainMode mode) { brain_mode_ = mode; }
    std::size_t plant_count() const { return plants_.size() - free_.size(); }
    int canopy_height(std::size_t i) const { return canopy_height_[i]; } // dm, of the crowns over a cell
    int canopy_cover(std::size_t i) const { return canopy_cover_[i]; }   // 0..255
    int litter(std::size_t i) const { return litter_[i]; }
    // A snag: the dead trunk a tree leaves standing where it died, of
    // whatever cause. It takes no light or water, and other plants grow
    // under it, but it counts as half a tree when a new one looks for room.
    // It rots over a few years, losing its top as it goes, and a fire clears
    // it. Its height now in dm, 0 for none; and where in its cell it stands,
    // which is where the tree stood.
    int snag(std::size_t i) const { return snag_height_[i] * (128 + snag_rot_[i] / 2) / 255; }
    std::uint32_t snag_place(std::size_t i) const { return snag_place_[i]; }
    int fire(std::size_t i) const { return fire_[i]; }   // burning: intensity; 0 not
    int burnt(std::size_t i) const { return burnt_[i]; } // 255 just burnt, fading
    // Sets a cell alight if there is anything to burn; for lightning and tests.
    bool ignite(std::size_t i);

    // A cell's temperature over the year, without season or day: the valley's
    // climate, a little colder up high and in hollows, warmer on slopes
    // facing the sun. What the biome follows.
    int mean_temperature(std::size_t i) const;

    std::vector<std::uint8_t> save() const;
    // Returns false and leaves the world unchanged if the data isn't a valid save.
    bool load(const std::uint8_t* data, std::size_t size);

private:
    friend class LandscapeSnapshot;
    World() = default; // storage for the read-only terrain painter
    int temperature_at(std::size_t i, bool water) const;
    void update_wind();
    void update_fronts();
    void start_development_front(int duration);
    int rasterize_front(const Front& front, int load, int boost);
    void restore_weather_from_fronts();
    void update_soil(int row);
    void update_biomes(int row);
    void update_hydrology();
    void refresh_water();
    void fill_flood_pockets();
    int nominal_lake_surface_dm(std::size_t id) const;
    void back_up_lakes();
    Biome classify(int x, int y) const;

    // Plants (plants.cpp).
    void seed_plants();
    void update_plants();
    void plant_arena(const std::vector<ArenaPatch>& fodder_beds);
    void refill_arena();
    void regrow_arena();
    void advance_landscape();
    void update_animals();
    void index_animals();
    void refresh_forage();
    std::int32_t bite_target(const Animal& animal, int length_mm) const;
    void strike(Animal& animal, const Frame& body);
    void leave_carcass(const Animal& dead, std::uint32_t killer, DeathCause cause);
    bool eat_meat(Animal& animal, const Frame& body);
    void index_carcass(std::size_t index);
    void decay_carcasses();
    void record_hunter_birth(Animal& young, Animal& mother, Animal& father);
    void grow_plant(std::uint32_t index);
    void reproduce(std::uint32_t index);
    bool establish(const PlantGenome& genome, std::uint32_t lineage, int cell, bool clone);
    std::int32_t place(const PlantGenome& genome, std::uint32_t lineage, int cell, int slot, int height);
    void kill_plant(std::uint32_t index);
    void update_canopy();
    void update_fire();
    int fuel(std::size_t i) const;
    bool room_for_tree(int cell) const;

    WorldConfig config_;
    Terrain terrain_;
    int nodes_x_, nodes_y_;
    std::uint64_t tick_ = 0;
    Random weather_rng_;
    int wind_angle_ = 0, wind_speed_ = 40;

    std::vector<std::uint8_t> water_;
    std::vector<std::uint16_t> depth_;
    std::vector<std::int16_t> temperature_;
    std::vector<std::uint8_t> soil_;
    std::vector<std::uint16_t> moisture_; // 8.8 fixed point
    std::vector<std::uint8_t> snow_;
    std::vector<std::uint8_t> nutrients_;
    std::vector<std::uint32_t> runoff_;
    std::vector<std::uint32_t> flow_;
    std::vector<std::uint8_t> biome_;
    std::vector<LakeState> lakes_; // parallel to terrain_.basins
    std::vector<Front> fronts_;
    std::array<Lightning, lightning_kept> lightning_{};
    std::size_t lightning_next_ = 0;

    // The sky at the saved tick, including rain already released by the fronts.
    std::vector<std::uint8_t> rain_, cloud_;
    // Scratch, rebuilt every tick or pass.
    std::vector<std::uint32_t> accumulated_; // runoff, then the cell queue during flooding
    std::vector<std::uint32_t> lake_area_;
    static constexpr int no_stream = 1 << 30;
    struct LakeSurface {
        int stage = 0;          // dm, including temporary river backwater
        int lowest_river = 0;   // scratch while measuring the surrounding flood
        int through = -no_stream; // dm: the surface of a stream running through it, if one does
    };
    std::vector<LakeSurface> lake_surfaces_;
    std::vector<std::uint8_t> previous_water_;

    std::vector<Plant> plants_;              // the pool
    std::vector<std::uint32_t> free_;        // free pool entries, last out first
    std::vector<std::uint32_t> occupancy_;   // per cell, 4 slots: pool index + 1, or 0
    std::vector<std::uint16_t> canopy_height_;
    std::vector<std::uint8_t> canopy_cover_;
    std::vector<std::uint16_t> litter_;      // dead plant matter: fuel, then nutrients
    std::vector<std::uint8_t> snag_height_;  // dm when the tree died, to 25.5 m; 0 for none
    std::vector<std::uint8_t> snag_rot_;     // 255 when it died, down to 0 when it falls
    std::vector<std::uint16_t> snag_place_;
    std::vector<std::uint8_t> fire_;
    std::vector<std::uint8_t> fire_next_;    // scratch
    std::vector<std::uint8_t> burnt_;
    Random plant_rng_, fire_rng_;
    std::vector<Animal> animals_;
    std::vector<brain::Brain> brains_;       // parallel to animals_
    std::vector<Carcass> carcasses_;
    std::vector<std::int32_t> carcass_head_;
    HuntStats hunts_;
    BrainMode brain_mode_ = BrainMode::evolving;
    StepObserver* observer_ = nullptr; // only for the length of a step
    // Scratch, rebuilt as animals move and plants grow: who is in each cell
    // (a list through animal_next_), and the grazing there.
    std::vector<std::int32_t> animal_head_, animal_next_;
    std::vector<std::uint16_t> forage_;
    // Scents, on a grid of nodes four cells across, spreading and fading:
    // grazing, animals, and animals ready to mate. The last carries what
    // kind of body it came from, as three sums weighted by its strength.
    int scent_w_ = 0, scent_h_ = 0;
    std::vector<std::uint32_t> scent_plant_, scent_animal_, scent_ready_, scent_legs_, scent_water_, scent_wings_, scent_scratch_;
    std::vector<Call> calls_;
    void spread_scents();
    std::uint32_t scent_at(const std::vector<std::uint32_t>& field, int x, int y) const;
    std::uint32_t next_animal_id_ = 1;
    std::uint32_t next_plant_id_ = 1;
};

// Read-only world inputs for painting a landscape while the simulation advances.
// Static terrain is copied once. Animal brains and simulation scratch stay in the live world.
class LandscapeSnapshot {
public:
    explicit LandscapeSnapshot(const World& source) { state_.terrain_ = source.terrain_; }
    const World& capture(const World& source) {
        state_.config_ = source.config_;
        state_.tick_ = source.tick_;
        state_.water_ = source.water_;
        state_.depth_ = source.depth_;
        state_.lake_surfaces_ = source.lake_surfaces_;
        state_.temperature_ = source.temperature_;
        state_.soil_ = source.soil_;
        state_.snow_ = source.snow_;
        state_.burnt_ = source.burnt_;
        state_.plants_ = source.plants_;
        state_.occupancy_ = source.occupancy_;
        state_.canopy_cover_ = source.canopy_cover_;
        state_.snag_height_ = source.snag_height_;
        state_.snag_rot_ = source.snag_rot_;
        state_.snag_place_ = source.snag_place_;
        return state_;
    }
private:
    World state_;
};

} // namespace phd
