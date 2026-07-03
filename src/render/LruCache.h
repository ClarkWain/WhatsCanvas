#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <unordered_map>
#include <utility>

namespace wsc::render {

/// A small, fixed-capacity least-recently-used cache keyed by a 64-bit hash.
///
/// Used to retain transform-independent CPU geometry (e.g. path tessellations)
/// across frames so identical shapes are not rebuilt every frame. Lookups move
/// the entry to the most-recently-used position; inserting past the capacity
/// evicts the least-recently-used entry. Hit/miss/eviction counters support
/// observability and test acceptance.
template <typename Value>
class LruCache
{
public:
    explicit LruCache(std::size_t maxEntries = 256)
        : maxEntries_(maxEntries == 0 ? 1 : maxEntries)
    {
    }

    /// Returns a stable pointer to the cached value and marks it as
    /// most-recently-used, or nullptr on a miss. Updates hit/miss counters.
    const Value *find(std::uint64_t key)
    {
        auto it = index_.find(key);
        if (it == index_.end()) {
            ++missCount_;
            return nullptr;
        }
        ++hitCount_;
        order_.splice(order_.begin(), order_, it->second);
        return &it->second->second;
    }

    /// Inserts (or replaces) a value, evicting the least-recently-used entry if
    /// the capacity would be exceeded. Returns a reference to the stored value,
    /// valid until the entry is evicted or the cache is cleared.
    const Value &insert(std::uint64_t key, Value value)
    {
        auto it = index_.find(key);
        if (it != index_.end()) {
            it->second->second = std::move(value);
            order_.splice(order_.begin(), order_, it->second);
            return it->second->second;
        }

        order_.emplace_front(key, std::move(value));
        index_.emplace(key, order_.begin());
        if (index_.size() > maxEntries_) {
            const std::uint64_t evictKey = order_.back().first;
            index_.erase(evictKey);
            order_.pop_back();
            ++evictionCount_;
        }
        return order_.front().second;
    }

    void clear()
    {
        order_.clear();
        index_.clear();
    }

    std::size_t size() const { return index_.size(); }
    std::size_t capacity() const { return maxEntries_; }
    std::size_t hitCount() const { return hitCount_; }
    std::size_t missCount() const { return missCount_; }
    std::size_t evictionCount() const { return evictionCount_; }
    void resetStats() { hitCount_ = missCount_ = evictionCount_ = 0; }

private:
    using Entry = std::pair<std::uint64_t, Value>;
    using EntryList = std::list<Entry>;

    std::size_t maxEntries_;
    EntryList order_; // front = most-recently-used, back = least-recently-used
    std::unordered_map<std::uint64_t, typename EntryList::iterator> index_;
    std::size_t hitCount_ = 0;
    std::size_t missCount_ = 0;
    std::size_t evictionCount_ = 0;
};

} // namespace wsc::render
