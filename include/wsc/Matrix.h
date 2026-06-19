#pragma once

#include <array>
#include <cstddef>

#include "Export.h"

namespace wsc {

/// Column-major 4x4 transform matrix used by the public Canvas API.
class WSC_API Matrix4
{
public:
    Matrix4();
    explicit Matrix4(const std::array<float, 16> &values);
    explicit Matrix4(const float *columnMajorValues);

    static Matrix4 identity();

    const std::array<float, 16> &values() const;
    const float *data() const;
    float *data();
    float at(std::size_t column, std::size_t row) const;
    void set(std::size_t column, std::size_t row, float value);

private:
    std::array<float, 16> values_;
};

} // namespace wsc
