// Unit tests for the separable Gaussian blur kernel.

#include <cmath>
#include <array>
#include <iostream>
#include <limits>
#include <string>

#include "render/GaussianKernel.h"
#include "wsc/ImageFilter.h"

namespace {

bool expect(bool condition, const std::string &message)
{
    if (condition) {
        return true;
    }
    std::cerr << "EXPECTATION FAILED: " << message << std::endl;
    return false;
}

// Full symmetric sum: centre + both side taps.
float symmetricSum(const wsc::render::GaussianKernel &kernel)
{
    float total = kernel.weights[0];
    for (std::size_t i = 1; i < kernel.weights.size(); ++i) {
        total += 2.0f * kernel.weights[i];
    }
    return total;
}

bool testZeroRadiusIsPassThrough()
{
    const wsc::render::GaussianKernel kernel = wsc::render::computeGaussianKernel(0.0f);
    return expect(kernel.radius() == 0, "zero radius should have radius 0")
        && expect(kernel.weights.size() == 1 && std::abs(kernel.weights[0] - 1.0f) < 1e-6f,
                  "zero radius should be a single unit weight");
}

bool testNegativeRadiusIsPassThrough()
{
    const wsc::render::GaussianKernel kernel = wsc::render::computeGaussianKernel(-5.0f);
    return expect(kernel.radius() == 0 && std::abs(kernel.weights[0] - 1.0f) < 1e-6f,
                  "negative radius should clamp to a pass-through kernel");
}

bool testWeightsAreNormalized()
{
    for (float radius : {2.0f, 5.0f, 12.0f, 40.0f}) {
        const wsc::render::GaussianKernel kernel = wsc::render::computeGaussianKernel(radius);
        if (!expect(std::abs(symmetricSum(kernel) - 1.0f) < 1e-4f,
                    "kernel weights should sum to 1 across the full symmetric convolution")) {
            return false;
        }
    }
    return true;
}

bool testWeightsAreMonotonicallyDecreasing()
{
    const wsc::render::GaussianKernel kernel = wsc::render::computeGaussianKernel(10.0f);
    for (std::size_t i = 1; i < kernel.weights.size(); ++i) {
        if (!expect(kernel.weights[i] <= kernel.weights[i - 1],
                    "Gaussian weights should not increase away from the centre")) {
            return false;
        }
    }
    return expect(kernel.weights.front() > kernel.weights.back(),
                  "centre weight should exceed the outermost weight");
}

bool testLinearSamplePackingPreservesDiscreteWeights()
{
    for (float radius : {1.0f, 2.0f, 7.0f, 12.0f}) {
        const wsc::render::GaussianKernel kernel =
            wsc::render::computeGaussianKernel(radius);
        const wsc::render::GaussianLinearSampleKernel packed =
            wsc::render::packGaussianKernelForLinearSampling(kernel);
        if (!expect(!packed.weights.empty()
                        && packed.weights.size() == packed.offsets.size(),
                    "linear-sample kernel should carry matched weights and offsets")
            || !expect(std::abs(packed.weights[0] - kernel.weights[0]) < 1e-7f,
                       "linear-sample kernel should preserve the center tap")) {
            return false;
        }
        for (std::size_t sample = 1; sample < packed.weights.size(); ++sample) {
            const int firstTap = static_cast<int>(sample * 2u - 1u);
            const float fraction = packed.offsets[sample]
                - static_cast<float>(firstTap);
            const float firstReconstructed =
                packed.weights[sample] * (1.0f - fraction);
            const float secondReconstructed =
                packed.weights[sample] * fraction;
            const float expectedSecond = firstTap + 1 <= kernel.radius()
                ? kernel.weights[static_cast<std::size_t>(firstTap + 1)]
                : 0.0f;
            if (!expect(std::abs(firstReconstructed
                                 - kernel.weights[static_cast<std::size_t>(firstTap)])
                            < 1e-6f,
                        "bilinear packing should reconstruct the first discrete tap")
                || !expect(std::abs(secondReconstructed - expectedSecond) < 1e-6f,
                           "bilinear packing should reconstruct the second discrete tap")) {
                return false;
            }
        }
    }
    return true;
}

bool testLinearSamplePackingWritesCallerStorage()
{
    const wsc::render::GaussianKernel kernel =
        wsc::render::computeGaussianKernel(12.0f);
    const wsc::render::GaussianLinearSampleKernel expected =
        wsc::render::packGaussianKernelForLinearSampling(kernel);
    std::array<float, 16> weights{};
    std::array<float, 16> offsets{};
    const int count = wsc::render::packGaussianKernelForLinearSampling(
        kernel, weights.data(), offsets.data(),
        static_cast<int>(weights.size()));
    if (!expect(count == static_cast<int>(expected.weights.size()),
                "caller storage should receive every packed sample")) {
        return false;
    }
    for (int index = 0; index < count; ++index) {
        if (!expect(std::abs(weights[static_cast<std::size_t>(index)]
                             - expected.weights[static_cast<std::size_t>(index)])
                            < 1e-7f
                        && std::abs(offsets[static_cast<std::size_t>(index)]
                                    - expected.offsets[static_cast<std::size_t>(index)])
                               < 1e-7f,
                    "caller storage should preserve bilinear packed taps")) {
            return false;
        }
    }
    return true;
}

bool testRadiusScalesWithRequest()
{
    const wsc::render::GaussianKernel small = wsc::render::computeGaussianKernel(4.0f);
    const wsc::render::GaussianKernel large = wsc::render::computeGaussianKernel(20.0f);
    return expect(large.radius() > small.radius(), "a larger requested radius should widen the kernel")
        && expect(small.radius() == 4 && large.radius() == 20, "radius should round to the requested pixel reach");
}

bool testSigmaAndFrostedGlassFactories()
{
    const wsc::ImageFilter sigma = wsc::ImageFilter::blurSigma(4.0f);
    const wsc::ImageFilter glass = wsc::ImageFilter::frostedGlass(6.0f);
    return expect(std::abs(sigma.radiusX() - 12.0f) < 1e-6f
                      && std::abs(sigma.radiusY() - 12.0f) < 1e-6f,
                  "blurSigma should convert standard deviation to a three-sigma reach")
        && expect(std::abs(glass.radiusX() - 18.0f) < 1e-6f,
                  "frostedGlass should use sigma-based blur")
        && expect(glass.hasColorAdjustment()
                      && glass.saturation() > 1.0f
                      && glass.brightness() > 1.0f
                      && glass.contrast() > 1.0f
                      && glass.hasGrain(),
                  "frostedGlass should add color adjustment and subtle grain");
}

bool testInnerShadowFactory()
{
    wsc::ImageFilter shadow = wsc::ImageFilter::innerShadowSigma(
        4.0f, 3.0f, 5.0f, wsc::Color(10, 20, 30, 128));
    const bool factoryOk = expect(shadow.isValid()
                      && shadow.type() == wsc::ImageFilter::Type::InnerShadow,
                  "innerShadowSigma should create a valid inner-shadow filter")
        && expect(std::abs(shadow.radiusX() - 12.0f) < 1e-6f
                      && std::abs(shadow.radiusY() - 12.0f) < 1e-6f,
                  "innerShadowSigma should convert sigma to a three-sigma reach")
        && expect(std::abs(shadow.offsetX() - 3.0f) < 1e-6f
                      && std::abs(shadow.offsetY() - 5.0f) < 1e-6f,
                  "inner shadow should preserve finite offsets")
        && expect(std::abs(shadow.samplingOutset() - 17.0f) < 1e-6f,
                  "inner shadow should include blur and offset in its sampling outset")
        && expect(shadow.outputOutset() == 0.0f,
                  "inner shadow must not expand visible layer output")
        && expect(shadow.shadowColor().getA() == 128,
                  "inner shadow should preserve its color");
    shadow.setColorAdjustment(0.0f).setGrain(0.2f);
    return factoryOk
        && expect(!shadow.hasColorAdjustment() && !shadow.hasGrain(),
                  "blur-only post-processing mutators should ignore inner shadows");
}

bool testNonFiniteFilterArguments()
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const wsc::ImageFilter blur = wsc::ImageFilter::blur(inf, -inf);
    const wsc::ImageFilter shadow = wsc::ImageFilter::innerShadow(
        nan, inf, inf, -inf, wsc::Color(0, 0, 0, 255));
    const wsc::ImageFilter overflow =
        wsc::ImageFilter::innerShadowSigma(inf, 0.0f, 0.0f,
                                           wsc::Color(0, 0, 0, 255));
    return expect(blur.radiusX() == wsc::ImageFilter::kMaxBlurRadius
                      && blur.radiusY() == 0.0f,
                  "signed infinite blur radii should clamp to their limits")
        && expect(shadow.radiusX() == 0.0f
                      && shadow.radiusY() == wsc::ImageFilter::kMaxBlurRadius,
                  "NaN should sanitize to zero while positive infinity clamps high")
        && expect(shadow.offsetX() == wsc::ImageFilter::kMaxShadowOffset
                      && shadow.offsetY() == -wsc::ImageFilter::kMaxShadowOffset,
                  "signed infinite offsets should clamp to their limits")
        && expect(overflow.radiusX() == wsc::ImageFilter::kMaxBlurRadius,
                  "overflowing sigma should clamp to the maximum radius");
}

bool testBlurDownsamplePolicy()
{
    const auto horizontal =
        wsc::render::chooseGaussianBlurDownsampleFactors(512, 256, 24.0f, 8.0f);
    const auto vertical =
        wsc::render::chooseGaussianBlurDownsampleFactors(127, 512, 64.0f, 64.0f);
    return expect(horizontal.x == 2 && horizontal.y == 1,
                  "only the long horizontal blur axis should be downsampled")
        && expect(vertical.x == 1 && vertical.y == 2,
                  "a narrow target should still downsample its eligible vertical axis")
        && expect(wsc::render::chooseGaussianBlurDownsample(512, 256, 24.0f, 8.0f) == 2,
                  "the compatibility helper should report the largest axis factor")
        && expect(wsc::render::chooseGaussianBlurDownsample(512, 256, 23.9f, 8.0f) == 1,
                  "short blur kernels should remain full resolution")
        && expect(!wsc::render::chooseGaussianBlurDownsampleFactors(
                       127, 127, 64.0f, 64.0f).active(),
                  "small target extents should remain full resolution");
}

bool testComposableFilterChain()
{
    const std::array<float, 20> grayscale = {
        0.2126f, 0.7152f, 0.0722f, 0.0f, 0.0f,
        0.2126f, 0.7152f, 0.0722f, 0.0f, 0.0f,
        0.2126f, 0.7152f, 0.0722f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
    };
    wsc::ImageFilterChain chain;
    chain.append(wsc::ImageFilter::blur(4.0f))
        .appendColorMatrix(grayscale)
        .appendOffset(3.0f, -2.0f);
    wsc::LayerOptions options;
    options.setImageFilter(chain);
    wsc::ImageFilterChain bounded = chain;
    for (std::size_t index = bounded.size();
         index < wsc::ImageFilterChain::kMaxNodes + 2u; ++index) {
        bounded.appendOffset(static_cast<float>(index), 0.0f);
    }
    wsc::ImageFilterChain invalid;
    std::array<float, 20> invalidMatrix = grayscale;
    invalidMatrix[0] = std::numeric_limits<float>::quiet_NaN();
    invalid.appendColorMatrix(invalidMatrix)
        .appendOffset(std::numeric_limits<float>::infinity(), 0.0f);
    return expect(
               bounded.size() == wsc::ImageFilterChain::kMaxNodes,
               "filter chain should enforce its node bound")
        && expect(invalid.empty(),
                  "filter chain should reject non-finite parameters")
        && expect(
            chain[1].type
                == wsc::ImageFilterChain::NodeType::ColorMatrix,
            "second node should be the color matrix")
        && expect(std::abs(chain.samplingOutset() - 7.0f) < 1e-6f,
                  "sampling outset should include blur and offset")
        && expect(options.hasImageFilter()
                      && options.imageFilterChain().size() == 3,
                  "LayerOptions should retain a composable chain");
}

} // namespace

int main()
{
    bool ok = true;
    ok = testZeroRadiusIsPassThrough() && ok;
    ok = testNegativeRadiusIsPassThrough() && ok;
    ok = testWeightsAreNormalized() && ok;
    ok = testWeightsAreMonotonicallyDecreasing() && ok;
    ok = testLinearSamplePackingPreservesDiscreteWeights() && ok;
    ok = testLinearSamplePackingWritesCallerStorage() && ok;
    ok = testRadiusScalesWithRequest() && ok;
    ok = testSigmaAndFrostedGlassFactories() && ok;
    ok = testInnerShadowFactory() && ok;
    ok = testNonFiniteFilterArguments() && ok;
    ok = testBlurDownsamplePolicy() && ok;
    ok = testComposableFilterChain() && ok;
    return ok ? 0 : 1;
}
