#pragma once
#include <vector>

struct DrawPointsData {
    std::vector<float> points;  // 每个点包含x,y坐标
    float size;
    float color[4];
    size_t getPointCount() const { return points.size() / 2; }
};


struct DrawLinesData {
    std::vector<float> points;  // 每个点包含x,y坐标
    float width;
    float color[4];
    size_t getLineCount() const { return points.size() / 4; }
};