#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "world.hpp"

namespace gfx {

// Downsample all channels on the painting thread before the upload.
template<int channels>
std::vector<std::uint8_t> half_terrain(const std::vector<std::uint8_t>& source, int width, int height) {
    std::vector<std::uint8_t> result(std::size_t(width / 2) * (height / 2) * channels);
    const int stride = width * channels;
    for (int y = 0; y < height / 2; ++y) {
        for (int x = 0; x < width / 2; ++x) {
            const auto* pixel = &source[std::size_t(2 * y) * stride + 2 * x * channels];
            auto* out = &result[(std::size_t(y) * (width / 2) + x) * channels];
            for (int c = 0; c < channels; ++c) {
                out[c] = std::uint8_t((pixel[c] + pixel[channels + c] + pixel[stride + c] + pixel[stride + channels + c] + 2) / 4);
            }
        }
    }
    return result;
}

// Paint one visible set from a single tick. Only the render thread uploads
// textures, and it publishes the whole set before drawing the next frame.
class TerrainBatch {
public:
    using Upload = std::function<void()>;
    using Paint = std::function<Upload(const phd::World&)>;

    ~TerrainBatch() {
        if (worker_.joinable()) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                stop_ = true;
            }
            wake_.notify_one();
            worker_.join();
        }
    }

    bool begin(bool capture) {
        if (pending_.valid()) {
            if (!capture && pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;
            for (auto& upload : pending_.get()) if (upload) upload();
        }
        return true;
    }

    void add(Paint paint) { jobs_.push_back(std::move(paint)); }

    void finish(const phd::World& world, bool capture) {
        if (jobs_.empty()) return;
        auto paint = [jobs = std::move(jobs_)](const phd::World& source) {
            std::vector<Upload> uploads;
            uploads.reserve(jobs.size());
            for (const auto& job : jobs) uploads.push_back(job(source));
            return uploads;
        };
        jobs_.clear();
        if (capture) {
            for (auto& upload : paint(world)) if (upload) upload();
        } else {
            if (!snapshot_) snapshot_.emplace(world);
            const auto& source = snapshot_->capture(world);
            if (!worker_.joinable()) worker_ = std::thread([this] { run(); });
            Work next([paint = std::move(paint), &source] { return paint(source); });
            pending_ = next.get_future();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                work_ = std::move(next);
            }
            wake_.notify_one();
        }
    }

private:
    using Work = std::packaged_task<std::vector<Upload>()>;
    void run() {
        for (;;) {
            Work next;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [this] { return stop_ || work_.valid(); });
                if (stop_) return;
                next = std::move(work_);
            }
            next();
        }
    }
    std::vector<Paint> jobs_;
    std::optional<phd::LandscapeSnapshot> snapshot_;
    std::future<std::vector<Upload>> pending_;
    Work work_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::thread worker_;
    bool stop_ = false;
};

} // namespace gfx
