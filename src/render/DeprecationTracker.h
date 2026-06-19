#pragma once

#include <iostream>
#include <set>
#include <string>

/// Tracks deprecated API usage and logs warnings once per call site.
/// Prevents repeated warnings from flooding the console.
class DeprecationTracker
{
public:
    /// Get the singleton instance.
    static DeprecationTracker &instance()
    {
        static DeprecationTracker tracker;
        return tracker;
    }

    /// Log a deprecation warning. Only emits the warning once per
    /// unique (function, file, line) combination.
    void warn(const char *function, const char *file, int line,
              const char *replacement = nullptr)
    {
        const std::string key = std::string(file) + ":" + std::to_string(line) + ":" + function;
        if (warned_.count(key)) {
            return;
        }
        warned_.insert(key);

        std::cerr << "[Deprecation] " << function << "() at " << file << ":" << line
                  << " is deprecated.";
        if (replacement) {
            std::cerr << " Use " << replacement << " instead.";
        }
        std::cerr << std::endl;
    }

    /// Get the number of unique deprecation warnings emitted.
    std::size_t warningCount() const { return warned_.size(); }

    /// Clear all tracked warnings (e.g. for testing).
    void clear() { warned_.clear(); }

private:
    DeprecationTracker() = default;
    std::set<std::string> warned_;
};

/// Macro for emitting a deprecation warning (once per call site).
#define WCS_DEPRECATED(func, replacement) \
    DeprecationTracker::instance().warn(func, __FILE__, __LINE__, replacement)
