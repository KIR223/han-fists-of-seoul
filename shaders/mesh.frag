#version 450
layout(set = 0, binding = 0) uniform sampler2D uAlbedo;  // sRGB-формат: оборудование само переводит в линейный свет

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vColor;
layout(location = 2) in vec2 vUV;
layout(location = 0) out vec4 outColor;

void main() {
    const vec3 lightDir = normalize(vec3(0.4, 0.8, 0.3));  // направленный свет «солнца»
    float diffuse = max(dot(normalize(vNormal), lightDir), 0.0);
    vec3 albedo = texture(uAlbedo, vUV).rgb * vColor;
    outColor = vec4(albedo * (0.2 + 0.8 * diffuse), 1.0);  // запись в sRGB-формат кодирует результат
}
