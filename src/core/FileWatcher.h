#pragma once
#include <filesystem>
#include <string>
#include <unordered_map>

#include "core/EventBus.h"
#include "core/Events.h"

// Polling based file watcher: checks last write times at a fixed interval
// and publishes FileChangedEvent on the event bus.
class FileWatcher {
public:
    explicit FileWatcher(EventBus& bus) : bus_(bus) {}

    void watch(const std::string& path) {
        std::error_code ec;
        auto t = std::filesystem::last_write_time(path, ec);
        watched_[path] = ec ? std::filesystem::file_time_type{} : t;
    }

    void update(float dt) {
        timer_ += dt;
        if (timer_ < interval_) return;
        timer_ = 0.0f;
        for (auto& [path, stamp] : watched_) {
            std::error_code ec;
            auto t = std::filesystem::last_write_time(path, ec);
            if (ec) continue;
            if (stamp != std::filesystem::file_time_type{} && t != stamp) {
                bus_.publish(FileChangedEvent{path});
            }
            stamp = t;
        }
    }

private:
    EventBus& bus_;
    std::unordered_map<std::string, std::filesystem::file_time_type> watched_;
    float timer_ = 0.0f;
    float interval_ = 0.5f;
};
