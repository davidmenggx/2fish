#pragma once

#include <bit>
#include <list>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

template <typename Key, typename Value, std::size_t Size>
class LRUCache {
    static_assert(std::has_single_bit(Size), "Size must be a power of 2");

private:
    struct CacheItem {
        Key key;
        Value data;
    };

public:
    bool fetch(const Key& key, Value& out_data) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);

        auto it = cache_.find(key);
        if (it == cache_.end()) {
            return false;
        }

        out_data = it->second->data;

        return true;
    }

    void insert(const Key& key, const Value& data) {
        std::unique_lock<std::shared_mutex> lock(mutex_);

        auto it = cache_.find(key);

        if (it != cache_.end()) {
            items_.splice(items_.begin(), items_, it->second);
            it->second->data = data;
            return;
        }

        if (cache_.size() >= Size) {
            const auto& last = items_.back();
            cache_.erase(last.key);
            items_.pop_back();
        }

        items_.push_front({ key, data });
        cache_[key] = items_.begin();
    }

private:
    mutable std::shared_mutex mutex_;

    std::list<CacheItem> items_;

    std::unordered_map<Key, typename std::list<CacheItem>::iterator> cache_;
};
