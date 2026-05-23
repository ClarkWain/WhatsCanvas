#pragma once

#include <cmath>

namespace crushedpixel {

/**
 * A two-dimensional float vector.
 * It exposes the x and y fields
 * as required by the Polyline2D functions.
 */
struct Vec2 {
	Vec2() :
			Vec2(0, 0) {}

	Vec2(float x, float y) :
			x(x), y(y) {}

	virtual ~Vec2() = default;

	float x, y;
};

namespace Vec2Maths {

static constexpr float epsilon = 0.0001f;

template<typename Vec2>
static bool equal(const Vec2 &a, const Vec2 &b) {
	return std::abs(a.x - b.x) <= epsilon && std::abs(a.y - b.y) <= epsilon;
}

template<typename Vec2>
static Vec2 multiply(const Vec2 &a, const Vec2 &b) {
	return {a.x * b.x, a.y * b.y};
}

template<typename Vec2>
static Vec2 multiply(const Vec2 &vec, float factor) {
	return {vec.x * factor, vec.y * factor};
}

template<typename Vec2>
static Vec2 divide(const Vec2 &vec, float factor) {
	return {vec.x / factor, vec.y / factor};
}

template<typename Vec2>
static Vec2 add(const Vec2 &a, const Vec2 &b) {
	return {a.x + b.x, a.y + b.y};
}

template<typename Vec2>
static Vec2 subtract(const Vec2 &a, const Vec2 &b) {
	return {a.x - b.x, a.y - b.y};
}

template<typename Vec2>
static float magnitude(const Vec2 &vec) {
	return std::sqrt(vec.x * vec.x + vec.y * vec.y);
}

template<typename Vec2>
static Vec2 withLength(const Vec2 &vec, float len) {
	auto mag = magnitude(vec);
	if (mag <= epsilon) {
		return {0, 0};
	}
	auto factor = mag / len;
	return divide(vec, factor);
}

template<typename Vec2>
static Vec2 normalized(const Vec2 &vec) {
	return withLength(vec, 1);
}

/**
 * Calculates the dot product of two vectors.
 */
template<typename Vec2>
static float dot(const Vec2 &a, const Vec2 &b) {
	return a.x * b.x + a.y * b.y;
}

/**
 * Calculates the cross product of two vectors.
 */
template<typename Vec2>
static float cross(const Vec2 &a, const Vec2 &b) {
	return a.x * b.y - a.y * b.x;
}

/**
 * Calculates the angle between two vectors.
 */
template<typename Vec2>
static float angle(const Vec2 &a, const Vec2 &b) {
	auto mag = magnitude(a) * magnitude(b);
	if (mag <= epsilon) {
		return 0;
	}
	auto value = dot(a, b) / mag;
	if (value < -1.0f) value = -1.0f;
	if (value > 1.0f) value = 1.0f;
	return std::acos(value);
}

} // namespace Vec2Maths

}