// Unit tests for the generic LRU cache used to retain CPU geometry.

#include <iostream>
#include <string>
#include <vector>

#include "render/LruCache.h"

namespace {

struct ResidentValue
{
    std::size_t bytes = 0;

    std::size_t residentBytes() const { return bytes; }
};

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
                  "cache should store and return complex values intact")
        && expect(cache.residentBytes() >= 3u * sizeof(float),
                  "vector resident bytes should include retained element capacity");
}

bool testByteBudgetEvictsLeastRecentlyUsed()
{
    wsc::render::LruCache<std::vector<float>> cache(8, 6u * sizeof(float));
    cache.insert(1, std::vector<float>(3, 1.0f));
    cache.insert(2, std::vector<float>(3, 2.0f));
    cache.find(1);
    cache.insert(3, std::vector<float>(3, 3.0f));

    return expect(cache.size() == 2, "byte budget should bound retained values")
        && expect(cache.find(1) != nullptr, "recently used value should survive byte eviction")
        && expect(cache.find(2) == nullptr, "least recently used value should be byte-evicted")
        && expect(cache.find(3) != nullptr, "new value should be retained")
        && expect(cache.residentBytes() <= cache.byteCapacity(),
                  "resident bytes should remain within the configured budget");
}

bool testCustomResidentByteAccounting()
{
    wsc::render::LruCache<ResidentValue> cache(8, 12);
    cache.insert(1, ResidentValue{8});
    cache.insert(2, ResidentValue{8});

    return expect(cache.size() == 1,
                  "custom resident byte accounting should enforce the byte budget")
        && expect(cache.find(1) == nullptr,
                  "custom resident byte accounting should evict the oldest value")
        && expect(cache.find(2) != nullptr,
                  "custom resident byte accounting should retain the newest value")
        && expect(cache.residentBytes() == 8,
                  "resident bytes should come from the value's residentBytes method");
}

bool testOversizedValueIsSoleEntry()
{
    wsc::render::LruCache<std::vector<float>> cache(8, 4u * sizeof(float));
    cache.insert(1, std::vector<float>(2, 1.0f));
    const std::vector<float> &oversized =
        cache.insert(2, std::vector<float>(8, 2.0f));

    return expect(cache.size() == 1, "an oversized value should evict other entries")
        && expect(oversized.size() == 8, "insert should return the oversized retained value")
        && expect(cache.find(1) == nullptr, "older values should not remain beside an oversized entry")
        && expect(cache.find(2) != nullptr, "the oversized value should remain usable");
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

bool testStringKeys()
{
    wsc::render::LruCache<int, std::string> cache(2);
    cache.insert("first", 1);
    cache.insert("second", 2);
    cache.find("first");
    cache.insert("third", 3);

    const int *first = cache.find("first");
    return expect(first != nullptr && *first == 1,
                  "custom string key should retrieve its value")
        && expect(cache.find("second") == nullptr,
                  "custom string key should participate in LRU eviction")
        && expect(cache.find("third") != nullptr,
                  "new custom string key should remain cached");
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
    ok = testByteBudgetEvictsLeastRecentlyUsed() && ok;
    ok = testCustomResidentByteAccounting() && ok;
    ok = testOversizedValueIsSoleEntry() && ok;
    ok = testClearAndResetStats() && ok;
    ok = testZeroCapacityIsClampedToOne() && ok;
    ok = testStringKeys() && ok;
    return ok ? 0 : 1;
}
