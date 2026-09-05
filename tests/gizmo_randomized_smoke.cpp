#include "vortex/editor/gizmo.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <numbers>
#include <random>

namespace {

[[nodiscard]] bool near(const float a, const float b, const float epsilon = 3.0e-4F) {
    return std::abs(a - b) <= epsilon;
}

[[nodiscard]] float dot(const vortex::Vec3 a, const vortex::Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] float length(const vortex::Vec3 value) {
    return std::sqrt(dot(value, value));
}

[[nodiscard]] vortex::Vec3 subtract(const vortex::Vec3 a, const vortex::Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

[[nodiscard]] bool nearMatrix(
    const vortex::TransformMatrix& a,
    const vortex::TransformMatrix& b,
    const float epsilon = 4.0e-4F) {
    for (std::size_t index = 0; index < a.values.size(); ++index) {
        if (!near(a.values[index], b.values[index], epsilon)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] float signedScale(std::mt19937& random, std::uniform_real_distribution<float>& magnitude) {
    return (random() & 1U) == 0U ? magnitude(random) : -magnitude(random);
}

} // namespace

int main() {
    std::mt19937 random{0x56334432U};
    std::uniform_real_distribution<float> angle{-std::numbers::pi_v<float>, std::numbers::pi_v<float>};
    std::uniform_real_distribution<float> component{-8.0F, 8.0F};
    std::uniform_real_distribution<float> scaleMagnitude{0.25F, 4.0F};
    std::uniform_real_distribution<float> factorMagnitude{0.1F, 2.5F};

    constexpr std::size_t iterations = 2'000U;
    for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
        const auto parentRotation = vortex::quaternionFromEulerRadians({angle(random), angle(random), angle(random)});
        const auto startRotation = vortex::quaternionFromEulerRadians({angle(random), angle(random), angle(random)});
        const auto localDelta = vortex::quaternionFromAxisAngle(
            {component(random), component(random), component(random)},
            angle(random));
        const auto worldDelta = vortex::quaternionFromAxisAngle(
            {component(random), component(random), component(random)},
            angle(random));
        assert(parentRotation && startRotation && localDelta && worldDelta);

        vortex::ObjectTransform parent{};
        parent.translation = {component(random), component(random), component(random)};
        parent.rotation = *parentRotation;
        parent.scale = {
            signedScale(random, scaleMagnitude),
            signedScale(random, scaleMagnitude),
            signedScale(random, scaleMagnitude),
        };
        const vortex::TransformMatrix parentMatrix = vortex::objectTransformMatrix(parent);
        const vortex::TransformComposeContext context{parentMatrix, *parentRotation};

        vortex::ObjectTransform start{};
        start.translation = {component(random), component(random), component(random)};
        start.rotation = *startRotation;
        start.scale = {
            signedScale(random, scaleMagnitude),
            signedScale(random, scaleMagnitude),
            signedScale(random, scaleMagnitude),
        };

        // A world-space translation must round-trip through arbitrary parent rotation,
        // non-uniform scale and reflection without leaking parent-space distortion.
        const vortex::Vec3 requestedWorldDelta{component(random), component(random), component(random)};
        const auto translated = vortex::composeTransformDelta(
            start,
            vortex::TranslateDelta{requestedWorldDelta},
            context);
        assert(translated);
        const vortex::Vec3 authoredDelta = subtract(translated->translation, start.translation);
        const vortex::Vec3 rebuiltWorldDelta = vortex::transformVector(parentMatrix, authoredDelta);
        assert(near(rebuiltWorldDelta.x, requestedWorldDelta.x));
        assert(near(rebuiltWorldDelta.y, requestedWorldDelta.y));
        assert(near(rebuiltWorldDelta.z, requestedWorldDelta.z));

        // Local rotation stays a post-multiply of the frozen authored orientation.
        const auto localRotated = vortex::composeTransformDelta(
            start,
            vortex::RotateDelta{*localDelta, vortex::RotationComposeSpace::Local},
            context);
        const auto expectedLocal = vortex::multiplyQuaternions(*startRotation, *localDelta);
        assert(localRotated && expectedLocal);
        assert(nearMatrix(
            vortex::rotationTransformMatrix(localRotated->rotation),
            vortex::rotationTransformMatrix(*expectedLocal)));

        // World rotation remains world-space even when the parent has non-uniform or negative scale.
        const auto worldRotated = vortex::composeTransformDelta(
            start,
            vortex::RotateDelta{*worldDelta, vortex::RotationComposeSpace::World},
            context);
        assert(worldRotated);
        const auto actualWorld = vortex::multiplyQuaternions(*parentRotation, worldRotated->rotation);
        const auto initialWorld = vortex::multiplyQuaternions(*parentRotation, *startRotation);
        assert(actualWorld && initialWorld);
        const auto expectedWorld = vortex::multiplyQuaternions(*worldDelta, *initialWorld);
        assert(expectedWorld);
        assert(nearMatrix(
            vortex::rotationTransformMatrix(*actualWorld),
            vortex::rotationTransformMatrix(*expectedWorld)));

        // Reflections and zero-crossings are intentionally representable in authored scale.
        const vortex::Vec3 factor{
            signedScale(random, factorMagnitude),
            signedScale(random, factorMagnitude),
            signedScale(random, factorMagnitude),
        };
        const auto scaled = vortex::composeTransformDelta(start, vortex::ScaleDelta{factor}, context);
        assert(scaled);
        assert(near(scaled->scale.x, start.scale.x * factor.x));
        assert(near(scaled->scale.y, start.scale.y * factor.y));
        assert(near(scaled->scale.z, start.scale.z * factor.z));

        // Random orientations must continue to expose an orthonormal gizmo basis.
        const vortex::GizmoFrame frame{{component(random), component(random), component(random)}, *startRotation};
        const auto x = vortex::gizmoAxisDirection(frame, vortex::GizmoAxis::X);
        const auto y = vortex::gizmoAxisDirection(frame, vortex::GizmoAxis::Y);
        const auto z = vortex::gizmoAxisDirection(frame, vortex::GizmoAxis::Z);
        assert(x && y && z);
        assert(near(length(*x), 1.0F));
        assert(near(length(*y), 1.0F));
        assert(near(length(*z), 1.0F));
        assert(std::abs(dot(*x, *y)) < 4.0e-4F);
        assert(std::abs(dot(*x, *z)) < 4.0e-4F);
        assert(std::abs(dot(*y, *z)) < 4.0e-4F);
    }

    // Singular parent linear transforms are rejected deterministically rather than producing
    // infinities during a translation drag.
    vortex::ObjectTransform singularParent{};
    singularParent.scale = {1.0F, 0.0F, -2.0F};
    vortex::ObjectTransform start{};
    const auto rejected = vortex::composeTransformDelta(
        start,
        vortex::TranslateDelta{{1.0F, 2.0F, 3.0F}},
        {vortex::objectTransformMatrix(singularParent), singularParent.rotation});
    assert(!rejected);

    std::cout << "Vortex3D randomized gizmo transform hardening passed\n";
    return 0;
}
