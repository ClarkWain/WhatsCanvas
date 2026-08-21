#pragma once

#include <array>
#include <cstddef>

#include "Export.h"

namespace wsc {

/// Column-major 4x4 transform matrix used by Canvas::setMatrix/concat.
///
/// The default constructor and identity() produce identity. Storage index is
/// `column * 4 + row`, matching OpenGL/GLM conventions. Canvas treats values as
/// logical transforms and preserves its device-pixel-ratio base separately.
class WSC_API Matrix4
{
public:
    /// Construct identity.
    Matrix4();
    /// Copy all 16 column-major values.
    explicit Matrix4(const std::array<float, 16> &values);
    /// Copy 16 column-major values; a null pointer produces identity.
    explicit Matrix4(const float *columnMajorValues);

    static Matrix4 identity();

    const std::array<float, 16> &values() const;
    const float *data() const;
    float *data();
    /// Read/write one cell. Out-of-range reads return 0 and writes are ignored.
    float at(std::size_t column, std::size_t row) const;
    void set(std::size_t column, std::size_t row, float value);

private:
    std::array<float, 16> values_;
};

} // namespace wsc
