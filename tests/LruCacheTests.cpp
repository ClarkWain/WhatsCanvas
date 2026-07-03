// Unit tests for the generic LRU cache used to retain CPU geometry.

#include <iostream>
#include <string>
#include <vector>

#include "render/LruCache.h"

namespace {

bool expect(bool condition, const std::string &message)
{
    if (condition) {
        return true;
    }
    std::cerr << "EXPECTATION FAILED: " << message << std::endl;
    return false;
}

bool testHitAndMissCounting()
{
    wsc::render::LruCache<int> cache(4);
    bool ok = expect(cache.find(1) == nullptr, "empty cache should miss");
    ok = expect(cache.missCount() == 1, "miss should be counted") && ok;

    cache.insert(1, 100);
    const int *value = cache.find(1);
    ok = expect(value != nullptr && *value == 100, "inserted value should be found") && ok;
    ok = expect(cache.hitCount() == 1, "hit should be counted") && ok;
    ok = expect(cache.size() == 1, "size should reflect one entry") && ok;
    return ok;
}

bool testReplaceDoesNotGrow()
{
    wsc::render::LruCache<int> cache(4);
    cache.insert(7, 1);
    cache.insert(7, 2);
    const int *value = cache.find(7);
    return expect(cache.size() == 1, "replacing a key should not grow the cache")
        && expect(value != nullptr && *value == 2, "replacing should update the stored value");
}

bool testLruEviction()
{
    wsc::render::LruCache<int> cache(2);
    cache.insert(1, 10);
    cache.insert(2, 20);
    cache.insert(3, 30); // evicts key 1 (least-recently-used)

    bool ok = expect(cache.size() == 2, "cache should not exceed capacity");
    ok = expect(cache.evictionCount() == 1, "one eviction should be recorded") && ok;
    ok = expect(cache.find(1) == nullptr, "least-recently-used key should be evicted") && ok;
    ok = expect(cache.find(2) != nullptr, "key 2 should remain") && ok;
    ok = expect(cache.find(3) != nullptr, "key 3 should remain") && ok;
    return ok;
}

bool testFindRefreshesRecency()
{
    wsc::render::LruCache<int> cache(2);
    cache.insert(1, 10);
    cache.insert(2, 20);
    cache.find(1);       // key 1 becomes most-recently-used
    cache.insert(3, 30); // should now evict key 2, not key 1

    return expect(cache.find(1) != nullptr, "recently-used key 1 should survive eviction")
        && expect(cache.find(2) == nullptr, "key 2 should be evicted after 1 was refreshed")
        && expect(cache.find(3) != nullptr, "newest key 3 should remain");
}

bool testStoresComplexValues()
{
    wsc::render::LruCache<std::vector<float>> cache(2);
    cache.insert(42, {1.0f, 2.0f, 3.0f});
    const std::vector<float> *value = cache.find(42);
    return expect(value != nullptr && value->size() == 3 && (*value)[1] == 2.0f,
                  "cache should store and return complex values intact");
}

bool testClearAndResetStats()
{
    wsc::render::LruCache<int> cache(4);
    cache.insert(1, 1);
    cache.find(1);
    cache.find(2);
    cache.clear();
    bool ok = expect(cache.size() == 0, "clear should remove all entries");
    ok = expect(cache.find(1) == nullptr, "cleared entry should not be found") && ok;
    cache.resetStats();
    ok = expect(cache.hitCount() == 0 && cache.missCount() == 0 && cache.evictionCount() == 0,
                "resetStats should zero all counters") && ok;
    return ok;
}

bool testZeroCapacityIsClampedToOne()
{
    wsc::render::LruCache<int> cache(0);
    cache.insert(1, 1);
    cache.insert(2, 2);
    return expect(cache.capacity() == 1, "zero capacity should be clamped to one")
        && expect(cache.size() == 1, "cache should hold at most one entry");
}

} // namespace

int main()
{
    bool ok = true;
    ok = testHitAndMissCounting() && ok;
    ok = testReplaceDoesNotGrow() && ok;
    ok = testLruEviction() && ok;
    ok = testFindRefreshesRecency() && ok;
    ok = testStoresComplexValues() && ok;
    ok = testClearAndResetStats() && ok;
    ok = testZeroCapacityIsClampedToOne() && ok;
    return ok ? 0 : 1;
}
