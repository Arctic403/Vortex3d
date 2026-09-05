#include "vortex/editor/gizmo.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <numbers>

namespace {

[[nodiscard]] vortex::Vec3 normalized(const vortex::Vec3 v) {
    const float length = std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
    assert(length > 0.0F);
    return {v.x/length, v.y/length, v.z/length};
}

[[nodiscard]] bool near(const float a, const float b, const float epsilon = 1.0e-4F) {
    return std::abs(a-b) <= epsilon;
}

} // namespace

int main() {
    const vortex::GizmoFrame identityFrame{};
    const vortex::GizmoCameraFrame camera{
        {0.0F, 0.0F, -1.0F},
        {1.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F},
    };

    // True ray/axis closest-point solve: the ray passes exactly through X=3 on the axis.
    const vortex::Vec3 rayOrigin{0.0F, 2.0F, 5.0F};
    const vortex::Vec3 target{3.0F, 0.0F, 0.0F};
    const vortex::PointerRay axisRay{
        rayOrigin,
        normalized({target.x-rayOrigin.x, target.y-rayOrigin.y, target.z-rayOrigin.z}),
    };
    const auto axis = vortex::sampleAxisConstraint(identityFrame, vortex::GizmoAxis::X, axisRay);
    assert(axis.has_value());
    assert(near(axis->parameter, 3.0F));

    // Plane sampling returns stable coordinates in the gizmo frame.
    const vortex::PointerRay planeRay{{2.0F, 3.0F, 5.0F}, {0.0F, 0.0F, -1.0F}};
    const auto plane = vortex::samplePlaneConstraint(
        identityFrame, vortex::GizmoPlane::XY, planeRay, camera);
    assert(plane.has_value());
    assert(near(plane->u, 2.0F));
    assert(near(plane->v, 3.0F));
    assert(near(plane->pointWorld.z, 0.0F));

    // Z ring phase at +Y is +pi/2 in the frozen ring frame.
    const vortex::PointerRay rotateRay{
        {0.0F, 0.0F, 5.0F},
        normalized({0.0F, 1.0F, -5.0F}),
    };
    const auto rotation = vortex::sampleRotationConstraint(
        identityFrame, vortex::GizmoHandle::AxisZ, rotateRay, camera);
    assert(rotation.has_value());
    assert(near(rotation->phaseRadians, std::numbers::pi_v<float> * 0.5F));

    // View ring uses camera right/up directly, independent from object orientation.
    const auto viewRotation = vortex::sampleRotationConstraint(
        identityFrame, vortex::GizmoHandle::ViewRing, rotateRay, camera);
    assert(viewRotation.has_value());
    assert(near(viewRotation->phaseRadians, std::numbers::pi_v<float> * 0.5F));

    // Camera-aligned axis is explicitly low-conditioned rather than receiving a different
    // hidden transform interpretation. A screen-perpendicular axis is fully conditioned.
    const vortex::PointerRay centerRay{{0.0F, 0.0F, 5.0F}, {0.0F, 0.0F, -1.0F}};
    assert(vortex::axisConstraintConditioning(
               identityFrame, vortex::GizmoAxis::Z, centerRay) < 1.0e-4F);
    assert(vortex::axisConstraintConditioning(
               identityFrame, vortex::GizmoAxis::X, centerRay) > 0.999F);

    const auto mixed = vortex::quaternionFromEulerRadians({0.6F, -0.4F, 0.9F});
    assert(mixed.has_value());
    const vortex::GizmoFrame mixedFrame{{1.0F, -2.0F, 0.5F}, *mixed};
    const auto x = vortex::gizmoAxisDirection(mixedFrame, vortex::GizmoAxis::X);
    const auto y = vortex::gizmoAxisDirection(mixedFrame, vortex::GizmoAxis::Y);
    const auto z = vortex::gizmoAxisDirection(mixedFrame, vortex::GizmoAxis::Z);
    assert(x && y && z);
    const auto dot = [](const vortex::Vec3 a, const vortex::Vec3 b) {
        return a.x*b.x + a.y*b.y + a.z*b.z;
    };
    assert(std::abs(dot(*x,*y)) < 1.0e-5F);
    assert(std::abs(dot(*x,*z)) < 1.0e-5F);
    assert(std::abs(dot(*y,*z)) < 1.0e-5F);

    assert(vortex::gizmoHandleDescriptors(vortex::TransformToolMode::Move).size() == 7U);
    assert(vortex::gizmoHandleDescriptors(vortex::TransformToolMode::Rotate).size() == 4U);
    assert(vortex::gizmoHandleDescriptors(vortex::TransformToolMode::Scale).size() == 7U);

    // Constraint-parameter snapping is pure and disabled by a non-positive step.
    assert(near(vortex::snapConstraintValue(1.24F, 0.25F), 1.25F));
    assert(near(vortex::snapConstraintValue(-0.62F, 0.25F), -0.5F));
    assert(near(vortex::snapConstraintValue(1.24F, 0.0F), 1.24F));

    // Translation deltas are world-space, then converted through the exact parent linear
    // transform. This keeps authored local translation correct under parent rotation + scale.
    vortex::ObjectTransform parent{};
    const auto parentRotation = vortex::quaternionFromAxisAngle(
        {0.0F, 0.0F, 1.0F}, std::numbers::pi_v<float> * 0.5F);
    assert(parentRotation);
    parent.rotation = *parentRotation;
    parent.scale = {2.0F, 3.0F, 4.0F};
    const vortex::TransformMatrix parentMatrix = vortex::objectTransformMatrix(parent);

    vortex::ObjectTransform start{};
    const vortex::Vec3 requestedWorldDelta{1.5F, -2.0F, 0.75F};
    const vortex::TransformComposeContext composeContext{parentMatrix, *parentRotation};
    const auto translated = vortex::composeTransformDelta(
        start, vortex::TranslateDelta{requestedWorldDelta}, composeContext);
    assert(translated);
    const vortex::Vec3 rebuiltWorldDelta = vortex::transformVector(
        parentMatrix, translated->translation);
    assert(near(rebuiltWorldDelta.x, requestedWorldDelta.x));
    assert(near(rebuiltWorldDelta.y, requestedWorldDelta.y));
    assert(near(rebuiltWorldDelta.z, requestedWorldDelta.z));

    // Local rotation post-multiplies the frozen start quaternion.
    const auto startRotation = vortex::quaternionFromEulerRadians({0.4F, -0.3F, 0.2F});
    const auto localDelta = vortex::quaternionFromAxisAngle(
        {0.0F, 0.0F, 1.0F}, 0.7F);
    assert(startRotation && localDelta);
    start.rotation = *startRotation;
    const auto localRotated = vortex::composeTransformDelta(
        start,
        vortex::RotateDelta{*localDelta, vortex::RotationComposeSpace::Local},
        composeContext);
    const auto expectedLocal = vortex::multiplyQuaternions(*startRotation, *localDelta);
    assert(localRotated && expectedLocal);
    const vortex::TransformMatrix localActualMatrix =
        vortex::rotationTransformMatrix(localRotated->rotation);
    const vortex::TransformMatrix localExpectedMatrix =
        vortex::rotationTransformMatrix(*expectedLocal);
    for (std::size_t i = 0U; i < localActualMatrix.values.size(); ++i) {
        assert(near(localActualMatrix.values[i], localExpectedMatrix.values[i]));
    }

    // World-space rotation pre-multiplies the world orientation and maps it back through
    // the parent orientation without involving parent scale.
    const auto worldDelta = vortex::quaternionFromAxisAngle({1.0F, 0.0F, 0.0F}, -0.45F);
    assert(worldDelta);
    const auto worldRotated = vortex::composeTransformDelta(
        start,
        vortex::RotateDelta{*worldDelta, vortex::RotationComposeSpace::World},
        composeContext);
    assert(worldRotated);
    const auto actualWorldRotation = vortex::multiplyQuaternions(
        *parentRotation, worldRotated->rotation);
    const auto initialWorldRotation = vortex::multiplyQuaternions(
        *parentRotation, *startRotation);
    assert(actualWorldRotation && initialWorldRotation);
    const auto expectedWorldRotation = vortex::multiplyQuaternions(
        *worldDelta, *initialWorldRotation);
    assert(expectedWorldRotation);
    const vortex::TransformMatrix worldActualMatrix =
        vortex::rotationTransformMatrix(*actualWorldRotation);
    const vortex::TransformMatrix worldExpectedMatrix =
        vortex::rotationTransformMatrix(*expectedWorldRotation);
    for (std::size_t i = 0U; i < worldActualMatrix.values.size(); ++i) {
        assert(near(worldActualMatrix.values[i], worldExpectedMatrix.values[i]));
    }

    // Signed scale factors preserve intentional reflection instead of clamping away zero-crossing.
    start.scale = {2.0F, 3.0F, 4.0F};
    const auto scaled = vortex::composeTransformDelta(
        start, vortex::ScaleDelta{{-0.5F, 2.0F, 1.0F}}, composeContext);
    assert(scaled);
    assert(near(scaled->scale.x, -1.0F));
    assert(near(scaled->scale.y, 6.0F));
    assert(near(scaled->scale.z, 4.0F));

    std::cout << "Vortex3D portable gizmo constraint solver smoke passed\n";
    return 0;
}
