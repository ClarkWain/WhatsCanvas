// Unit tests for the separable Gaussian blur kernel.

#include <cmath>
#include <iostream>
#include <string>

#include "render/GaussianKernel.h"

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

bool testRadiusScalesWithRequest()
{
    const wsc::render::GaussianKernel small = wsc::render::computeGaussianKernel(4.0f);
    const wsc::render::GaussianKernel large = wsc::render::computeGaussianKernel(20.0f);
    return expect(large.radius() > small.radius(), "a larger requested radius should widen the kernel")
        && expect(small.radius() == 4 && large.radius() == 20, "radius should round to the requested pixel reach");
}

} // namespace

int main()
{
    bool ok = true;
    ok = testZeroRadiusIsPassThrough() && ok;
    ok = testNegativeRadiusIsPassThrough() && ok;
    ok = testWeightsAreNormalized() && ok;
    ok = testWeightsAreMonotonicallyDecreasing() && ok;
    ok = testRadiusScalesWithRequest() && ok;
    return ok ? 0 : 1;
}
