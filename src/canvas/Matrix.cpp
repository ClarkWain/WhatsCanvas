#include "Matrix.h"

#include <algorithm>

namespace wsc {

namespace {

constexpr std::array<float, 16> kIdentityMatrix = {
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f
};

} // namespace

Matrix4::Matrix4()
    : values_(kIdentityMatrix)
{
}

Matrix4::Matrix4(const std::array<float, 16> &values)
    : values_(values)
{
}

Matrix4::Matrix4(const float *columnMajorValues)
    : values_(kIdentityMatrix)
{
    if (columnMajorValues != nullptr) {
        std::copy(columnMajorValues, columnMajorValues + values_.size(), values_.begin());
    }
}

Matrix4 Matrix4::identity()
{
    return Matrix4(kIdentityMatrix);
}

const std::array<float, 16> &Matrix4::values() const
{
    return values_;
}

const float *Matrix4::data() const
{
    return values_.data();
}

float *Matrix4::data()
{
    return values_.data();
}

float Matrix4::at(std::size_t column, std::size_t row) const
{
    if (column >= 4 || row >= 4) {
        return 0.0f;
    }

    return values_[column * 4 + row];
}

void Matrix4::set(std::size_t column, std::size_t row, float value)
{
    if (column >= 4 || row >= 4) {
        return;
    }

    values_[column * 4 + row] = value;
}

} // namespace wsc
