#version 450
layout(set = 0, binding = 0) uniform sampler2D uAlbedo;
layout(push_constant) uniform ShadowMaterial { layout(offset = 68) float alphaCutoff; } pc;
layout(location = 0) in vec2 vUV;
void main() {
    if (texture(uAlbedo, vUV).a < pc.alphaCutoff) discard;
}
