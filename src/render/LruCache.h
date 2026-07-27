#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace wsc::render {

namespace detail {

template <typename Value, typename = void>
struct CacheValueBytes
{
    static std::size_t measure(const Value &)
    {
        return sizeof(Value);
    }
};

template <typename Value>
struct CacheValueBytes<
    Value,
    std::void_t<decltype(
        std::declval<const Value &>().residentBytes())>>
{
    static std::size_t measure(const Value &value)
    {
        return value.residentBytes();
    }
};

template <typename Value, typename Allocator>
struct CacheValueBytes<std::vector<Value, Allocator>, void>
{
    static std::size_t measure(const std::vector<Value, Allocator> &value)
    {
        return value.capacity() * sizeof(Value);
    }
};

} // namespace detail

/// A small, fixed-capacity least-recently-used cache keyed by a 64-bit hash.
///
/// Used to retain transform-independent CPU geometry (e.g. path tessellations)
/// across frames so identical shapes are not rebuilt every frame. Lookups move
/// the entry to the most-recently-used position; inserting past the capacity
/// or byte budget evicts the least-recently-used entry. A value larger than the
/// entire byte budget is retained as the sole entry so insert() can continue to
/// return a stable reference. Hit/miss/eviction counters support observability
/// and test acceptance.
template <typename Value>
class LruCache
{
public:
    explicit LruCache(std::size_t maxEntries = 256,
                      std::size_t maxResidentBytes = 0)
        : maxEntries_(maxEntries == 0 ? 1 : maxEntries),
          maxResidentBytes_(maxResidentBytes)
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
        const std::size_t valueBytes = detail::CacheValueBytes<Value>::measure(value);
        auto it = index_.find(key);
        if (it != index_.end()) {
            residentBytes_ -= detail::CacheValueBytes<Value>::measure(it->second->second);
            it->second->second = std::move(value);
            residentBytes_ += valueBytes;
            order_.splice(order_.begin(), order_, it->second);
            trimToBudget();
            return it->second->second;
        }

        while (!order_.empty()
               && (index_.size() >= maxEntries_
                   || wouldExceedByteBudget(valueBytes))) {
            evictOldest();
        }
        order_.emplace_front(key, std::move(value));
        index_.emplace(key, order_.begin());
        residentBytes_ += valueBytes;
        return order_.front().second;
    }

    void clear()
    {
        order_.clear();
        index_.clear();
        residentBytes_ = 0;
    }

    std::size_t size() const { return index_.size(); }
    std::size_t capacity() const { return maxEntries_; }
    std::size_t residentBytes() const { return residentBytes_; }
    std::size_t byteCapacity() const { return maxResidentBytes_; }
    std::size_t hitCount() const { return hitCount_; }
    std::size_t missCount() const { return missCount_; }
    std::size_t evictionCount() const { return evictionCount_; }
    void resetStats() { hitCount_ = missCount_ = evictionCount_ = 0; }

private:
    bool wouldExceedByteBudget(std::size_t incomingBytes) const
    {
        return maxResidentBytes_ > 0
            && (incomingBytes > maxResidentBytes_
                || residentBytes_ > maxResidentBytes_ - incomingBytes);
    }

    void evictOldest()
    {
        if (order_.empty()) {
            return;
        }
        residentBytes_ -= detail::CacheValueBytes<Value>::measure(order_.back().second);
        index_.erase(order_.back().first);
        order_.pop_back();
        ++evictionCount_;
    }

    void trimToBudget()
    {
        while (order_.size() > 1
               && maxResidentBytes_ > 0
               && residentBytes_ > maxResidentBytes_) {
            evictOldest();
        }
    }

    using Entry = std::pair<std::uint64_t, Value>;
    using EntryList = std::list<Entry>;

    std::size_t maxEntries_;
    std::size_t maxResidentBytes_ = 0;
    std::size_t residentBytes_ = 0;
    EntryList order_; // front = most-recently-used, back = least-recently-used
    std::unordered_map<std::uint64_t, typename EntryList::iterator> index_;
    std::size_t hitCount_ = 0;
    std::size_t missCount_ = 0;
    std::size_t evictionCount_ = 0;
};

} // namespace wsc::render
