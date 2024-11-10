#pragma once

#include <string>
#include <sstream>
#include <iomanip>
#include <stdexcept>

class Color
{
public:
    static const Color RED;
    static const Color GREEN;
    static const Color BLUE;
    static const Color WHITE;
    static const Color BLACK;
    static const Color YELLOW;
    static const Color CYAN;
    static const Color MAGENTA;

public:
    Color() : r_(0), g_(0), b_(0), a_(255) {}

    Color(int r, int g, int b, int a = 255) : r_(r), g_(g), b_(b), a_(a) {}

    static Color fromHex(const std::string &hex)
    {
        if (hex.size() != 9 && hex.size() != 7)
        {
            throw std::invalid_argument("Invalid hex color format");
        }

        int r, g, b, a = 255;
        std::istringstream(hex.substr(1, 2)) >> std::hex >> r;
        std::istringstream(hex.substr(3, 2)) >> std::hex >> g;
        std::istringstream(hex.substr(5, 2)) >> std::hex >> b;
        if (hex.size() == 9)
        {
            std::istringstream(hex.substr(7, 2)) >> std::hex >> a;
        }

        return Color(r, g, b, a);
    }

    int getR() const { return r_; }
    int getG() const { return g_; }
    int getB() const { return b_; }
    int getA() const { return a_; }

    float a() const { return a_ / 255.0f; }
    float r() const { return r_ / 255.0f; }
    float g() const { return g_ / 255.0f; }
    float b() const { return b_ / 255.0f; }

    void getNormalized(float* rgba) const
    {
        rgba[0] = r_ / 255.0f;
        rgba[1] = g_ / 255.0f;
        rgba[2] = b_ / 255.0f;
        rgba[3] = a_ / 255.0f;
    }

private:
    int r_, g_, b_, a_;
};

class Paint
{
public:
    enum class Style
    {
        FILL,
        STROKE,
        FILL_AND_STROKE
    };

    enum class StrokeCap
    {
        BUTT,
        ROUND,
        SQUARE
    };

    Paint() = default;

    // Copy constructor
    Paint(const Paint &other)
    {
        color_ = other.color_;
        strokeColor_ = other.strokeColor_;
        strokeWidth_ = other.strokeWidth_;
        antiAlias_ = other.antiAlias_;
        style_ = other.style_;
        strokeCap_ = other.strokeCap_;
    }

    // Assignment operator
    Paint &operator=(const Paint &other)
    {
        if (this != &other)
        {
            color_ = other.color_;
            strokeColor_ = other.strokeColor_;
            strokeWidth_ = other.strokeWidth_;
            antiAlias_ = other.antiAlias_;
            style_ = other.style_;
            strokeCap_ = other.strokeCap_;
        }
        return *this;
    }

    // Move constructor
    Paint(Paint &&other) noexcept : color_(std::move(other.color_)), strokeColor_(std::move(other.strokeColor_)),
                                    strokeWidth_(other.strokeWidth_), antiAlias_(other.antiAlias_),
                                    style_(other.style_), strokeCap_(other.strokeCap_)
    {
        other.strokeWidth_ = 0.0f;
        other.antiAlias_ = false;
        other.style_ = Style::FILL;
        other.strokeCap_ = StrokeCap::BUTT;
    }

    // 设置抗锯齿
    void setAntiAlias(bool aa)
    {
        antiAlias_ = aa;
    }

    // 获取抗锯齿
    bool isAntiAlias() const
    {
        return antiAlias_;
    }

    // 设置颜色
    void setColor(const Color &color)
    {
        color_ = color;
    }

    // 设置颜色
    void setColor(int r, int g, int b, int a = 255)
    {
        color_ = Color(r, g, b, a);
    }

    // 设置颜色
    void setColor(float r, float g, float b, float a = 1.0f)
    {
        color_ = Color(static_cast<int>(r * 255), static_cast<int>(g * 255),
                       static_cast<int>(b * 255), static_cast<int>(a * 255));
    }

    // 获取颜色
    Color getColor() const
    {
        return color_;
    }

    // 设置线条宽度
    void setStrokeWidth(float width)
    {
        strokeWidth_ = width;
    }

    // 获取线条宽度
    float getStrokeWidth() const
    {
        return strokeWidth_;
    }

    // 设置线条颜色
    void setStrokeColor(const Color &color)
    {
        strokeColor_ = color;
    }

    // 获取线条颜色
    Color getStrokeColor() const
    {
        return strokeColor_;
    }

    // 设置样式
    void setStyle(Style style)
    {
        style_ = style;
    }

    // 获取样式
    Style getStyle() const
    {
        return style_;
    }

    // 设置线条端点样式
    void setStrokeCap(StrokeCap cap)
    {
        strokeCap_ = cap;
    }

    // 获取线条端点样式
    StrokeCap getStrokeCap() const
    {
        return strokeCap_;
    }

private:
    Color color_ = Color::BLACK;
    Color strokeColor_ = Color::BLACK;
    float strokeWidth_ = 1.0f;
    bool antiAlias_ = false;
    Style style_ = Style::FILL;
    StrokeCap strokeCap_ = StrokeCap::BUTT;
};
