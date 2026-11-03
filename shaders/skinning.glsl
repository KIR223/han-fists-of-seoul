// Палитра отдельная для каждого кадра в полёте. firstInstance задаёт её начало.
layout(set = 1, binding = 2, std430) readonly buffer SkinPalettes { mat4 bones[]; } skin;
layout(location = 5) in uvec4 inJoints;
layout(location = 6) in vec4 inWeights;
mat4 skinMatrix() {
    if (gl_InstanceIndex == 0 || dot(inWeights, vec4(1.0)) < 0.0001) return mat4(1.0);
    uint base = uint(gl_InstanceIndex);
    return skin.bones[base + inJoints.x] * inWeights.x
         + skin.bones[base + inJoints.y] * inWeights.y
         + skin.bones[base + inJoints.z] * inWeights.z
         + skin.bones[base + inJoints.w] * inWeights.w;
}
