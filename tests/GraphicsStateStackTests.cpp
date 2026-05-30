#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include <glm.hpp>
#include <gtc/matrix_transform.hpp>

#include "canvas/Path.h"
#include "render/GraphicsStateStack.h"

namespace {

class FakeClipMaskResource final : public ClipMaskResource
{
public:
    explicit FakeClipMaskResource(int identifier)
        : identifier_(identifier)
    {
    }

    bool isValid() const override
    {
        return true;
    }

    void apply(const RenderContext &, const ScissorState &, std::size_t) const override
    {
    }

    int identifier() const
    {
        return identifier_;
    }

private:
    int identifier_ = 0;
};

bool expect(bool condition, const std::string &message)
{
    if (condition) {
        return true;
    }

    std::cerr << "EXPECTATION FAILED: " << message << std::endl;
    return false;
}

bool testSaveRestoreKeepsMatrixAndClipState()
{
    GraphicsStateStack stack;
    stack.current().matrix = glm::translate(glm::mat4(1.0f), glm::vec3(12.0f, 18.0f, 0.0f));
    stack.current().clip.enabled = true;
    stack.current().clip.rect = RectF(10.0f, 20.0f, 30.0f, 40.0f);

    const int savedCount = stack.save();
    if (!expect(savedCount == 1, "save should return the pre-save count")) {
        return false;
    }

    stack.current().matrix = glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 3.0f, 1.0f));
    stack.current().clip.rect = RectF(1.0f, 2.0f, 3.0f, 4.0f);
    stack.current().clip.enabled = false;

    if (!expect(stack.restore(), "restore should succeed when a state is saved")) {
        return false;
    }

    return expect(stack.current().clip.enabled, "restore should recover clip enabled state")
        && expect(stack.current().clip.rect.getX() == 10.0f, "restore should recover clip rect x")
        && expect(stack.current().clip.rect.getY() == 20.0f, "restore should recover clip rect y")
        && expect(stack.current().clip.rect.getWidth() == 30.0f, "restore should recover clip rect width")
        && expect(stack.current().clip.rect.getHeight() == 40.0f, "restore should recover clip rect height")
        && expect(stack.current().matrix[3][0] == 12.0f, "restore should recover matrix translation x")
        && expect(stack.current().matrix[3][1] == 18.0f, "restore should recover matrix translation y");
}

bool testRestoreToCountClampsAtBaseState()
{
    GraphicsStateStack stack;
    stack.current().clip.rect = RectF(1.0f, 1.0f, 10.0f, 10.0f);
    stack.save();
    stack.current().clip.rect = RectF(2.0f, 2.0f, 20.0f, 20.0f);
    stack.save();
    stack.current().clip.rect = RectF(3.0f, 3.0f, 30.0f, 30.0f);

    stack.restoreToCount(-99);

    return expect(stack.getSaveCount() == 1, "restoreToCount should clamp to the base state")
        && expect(stack.current().clip.rect.getX() == 1.0f, "restoreToCount should recover the base state")
        && expect(!stack.restore(), "restore should fail when no saved state remains");
}

bool testSavedClipResourcesDoNotBleedAcrossStates()
{
    GraphicsStateStack stack;

    ClipPathState originalPath;
    originalPath.deviceBounds = RectF(0.0f, 0.0f, 10.0f, 10.0f);
    originalPath.mask.points = {0.0f, 0.0f, 10.0f, 0.0f, 10.0f, 10.0f};
    originalPath.resource = std::make_shared<FakeClipMaskResource>(1);
    stack.current().clip.enabled = true;
    stack.current().clip.paths.push_back(originalPath);

    stack.save();

    ClipPathState mutatedPath = stack.current().clip.paths.front();
    mutatedPath.resource = std::make_shared<FakeClipMaskResource>(2);
    mutatedPath.deviceBounds = RectF(5.0f, 5.0f, 20.0f, 20.0f);
    stack.current().clip.paths.front() = mutatedPath;

    if (!expect(stack.restore(), "restore should recover the saved clip resource state")) {
        return false;
    }

    if (!expect(stack.current().clip.paths.size() == 1, "restore should recover clip path count")) {
        return false;
    }

    const auto restoredResource = std::dynamic_pointer_cast<FakeClipMaskResource>(stack.current().clip.paths.front().resource);
    return expect(restoredResource != nullptr, "restore should recover the original clip resource")
        && expect(restoredResource->identifier() == 1, "restore should recover the original clip resource identity")
        && expect(stack.current().clip.paths.front().deviceBounds.getX() == 0.0f,
                  "restore should recover original clip bounds");
}

bool testPathEvenOddContainsRespectsHole()
{
    Path path;
    path.setFillType(Path::FillType::EVEN_ODD);
    path.addRect(RectF(0.0f, 0.0f, 20.0f, 20.0f));
    path.addRect(RectF(5.0f, 5.0f, 10.0f, 10.0f));

    return expect(path.getContourCount() == 2, "even-odd path should keep both contours")
        && expect(path.getClosedContourCount() == 2, "even-odd test path should have two closed contours")
        && expect(path.contains(2.0f, 2.0f), "point inside outer contour should be contained")
        && expect(!path.contains(10.0f, 10.0f), "point inside inner contour hole should be excluded");
}

bool testPathStrokeContainsFindsSegments()
{
    Path line;
    line.moveTo(0.0f, 0.0f);
    line.lineTo(10.0f, 0.0f);

    return expect(line.strokeContains(5.0f, 0.4f, 1.0f), "strokeContains should hit points near the segment")
        && expect(!line.strokeContains(5.0f, 2.0f, 1.0f), "strokeContains should reject distant points");
}

bool testPathTrimAndReverseKeepExpectedGeometry()
{
    Path line;
    line.moveTo(0.0f, 0.0f);
    line.lineTo(10.0f, 0.0f);

    const Path trimmed = line.trim(2.0f, 6.0f);
    const auto &trimmedPoints = trimmed.getPoints();
    if (!expect(trimmedPoints.size() == 2, "trimmed line should emit a move and one line segment")) {
        return false;
    }

    const Path reversed = line.reversed();
    const auto &reversedPoints = reversed.getPoints();
    if (!expect(reversedPoints.size() == 2, "reversed line should keep two points")) {
        return false;
    }

    return expect(trimmedPoints[0].point.getX() == 2.0f, "trim should start at the requested start length")
        && expect(trimmedPoints[1].point.getX() == 6.0f, "trim should end at the requested end length")
        && expect(std::abs(trimmed.length() - 4.0f) <= 0.0001f, "trimmed line length should match the requested span")
        && expect(reversedPoints[0].point.getX() == 10.0f, "reversed path should start from the original end")
        && expect(reversedPoints[1].point.getX() == 0.0f, "reversed path should end at the original start");
}

} // namespace

int main()
{
    const bool ok = testSaveRestoreKeepsMatrixAndClipState()
        && testRestoreToCountClampsAtBaseState()
        && testSavedClipResourcesDoNotBleedAcrossStates()
        && testPathEvenOddContainsRespectsHole()
        && testPathStrokeContainsFindsSegments()
        && testPathTrimAndReverseKeepExpectedGeometry();
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}