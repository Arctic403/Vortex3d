#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 0) out vec3 fragColor;

layout(push_constant) uniform CameraPush {
    float aspect;
} camera;

void main() {
    const float cy = 0.8191520443;
    const float sy = 0.5735764364;
    const float cx = 0.9396926208;
    const float sx = -0.3420201433;

    vec3 p = inPosition;
    p = vec3(cy * p.x + sy * p.z, p.y, -sy * p.x + cy * p.z);
    p = vec3(p.x, cx * p.y - sx * p.z, sx * p.y + cx * p.z);
    p.z += 4.5;

    const float f = 1.7320508076;
    const float nearPlane = 0.1;
    const float farPlane = 100.0;
    float safeAspect = max(camera.aspect, 0.001);

    gl_Position = vec4(
        (f / safeAspect) * p.x,
        -f * p.y,
        (farPlane / (farPlane - nearPlane)) * p.z -
            ((farPlane * nearPlane) / (farPlane - nearPlane)),
        p.z
    );
    fragColor = inColor;
}
