#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <vector>

// Minimal typed publish/subscribe event bus.
// Subscribe with a callback taking const E&; publish delivers synchronously.
class EventBus {
public:
    using SubscriptionId = uint64_t;

    template <typename E>
    SubscriptionId subscribe(std::function<void(const E&)> handler) {
        auto& list = handlersFor(typeid(E));
        SubscriptionId id = nextId_++;
        list.push_back({id, [handler = std::move(handler)](const void* ev) {
                            handler(*static_cast<const E*>(ev));
                        }});
        return id;
    }

    template <typename E>
    void unsubscribe(SubscriptionId id) {
        auto it = handlers_.find(typeid(E));
        if (it == handlers_.end()) return;
        auto& list = it->second;
        list.erase(std::remove_if(list.begin(), list.end(),
                                  [id](const Entry& e) { return e.id == id; }),
                   list.end());
    }

    template <typename E>
    void publish(const E& event) const {
        auto it = handlers_.find(typeid(E));
        if (it == handlers_.end()) return;
        // Copy so handlers may subscribe/unsubscribe during dispatch.
        auto list = it->second;
        for (auto& entry : list) entry.fn(&event);
    }

private:
    struct Entry {
        SubscriptionId id;
        std::function<void(const void*)> fn;
    };

    std::vector<Entry>& handlersFor(std::type_index type) { return handlers_[type]; }

    std::unordered_map<std::type_index, std::vector<Entry>> handlers_;
    SubscriptionId nextId_ = 1;
};
