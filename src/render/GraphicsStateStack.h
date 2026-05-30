#pragma once

#include <algorithm>
#include <vector>

#include "GraphicsState.h"

class GraphicsStateStack
{
public:
    const GraphicsState &current() const
    {
        return current_;
    }

    GraphicsState &current()
    {
        return current_;
    }

    int save()
    {
        const int savedCount = getSaveCount();
        stack_.push_back(current_);
        return savedCount;
    }

    bool canRestore() const
    {
        return !stack_.empty();
    }

    bool restore()
    {
        if (stack_.empty()) {
            return false;
        }

        current_ = stack_.back();
        stack_.pop_back();
        return true;
    }

    int getSaveCount() const
    {
        return static_cast<int>(stack_.size()) + 1;
    }

    void restoreToCount(int saveCount)
    {
        const int targetCount = std::max(1, saveCount);
        while (getSaveCount() > targetCount && !stack_.empty()) {
            restore();
        }
    }

    void clearSavedStates()
    {
        stack_.clear();
    }

private:
    GraphicsState current_;
    std::vector<GraphicsState> stack_;
};