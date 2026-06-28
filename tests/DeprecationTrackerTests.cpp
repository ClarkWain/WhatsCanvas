#include "render/DeprecationTracker.h"

#include <iostream>
#include <string>

namespace {

bool expect(bool condition, const std::string &message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << std::endl;
        return false;
    }
    return true;
}

void warnFromFirstSite()
{
    WCS_DEPRECATED("legacyApi", "newApi");
}

void warnFromSecondSite()
{
    WCS_DEPRECATED("legacyApi", "newApi");
}

bool testWarnsOncePerCallSite()
{
    DeprecationTracker::instance().clear();
    warnFromFirstSite();
    warnFromFirstSite();
    bool ok = expect(DeprecationTracker::instance().warningCount() == 1,
                     "same call site should warn once");

    warnFromSecondSite();
    ok = expect(DeprecationTracker::instance().warningCount() == 2,
                "different call sites should be tracked independently") && ok;

    DeprecationTracker::instance().clear();
    ok = expect(DeprecationTracker::instance().warningCount() == 0,
                "clear should reset warning count") && ok;
    return ok;
}

} // namespace

int main()
{
    return testWarnsOncePerCallSite() ? 0 : 1;
}
