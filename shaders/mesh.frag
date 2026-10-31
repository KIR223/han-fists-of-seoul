#version 450
layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vColor;
layout(location = 0) out vec4 outColor;

void main() {
    const vec3 lightDir = normalize(vec3(0.4, 0.8, 0.3));  // направленный свет «солнца»
    float diffuse = max(dot(normalize(vNormal), lightDir), 0.0);
    outColor = vec4(vColor * (0.2 + 0.8 * diffuse), 1.0);  // выход в sRGB-формат переводит оборудование
}
