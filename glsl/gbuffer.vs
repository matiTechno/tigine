#version 330

layout(location = 0) in vec3 vertex;
layout(location = 1) in vec2 texCoord;
layout(location = 2) in vec3 normal;
layout(location = 3) in vec3 tangent;
layout(location = 4) in vec3 bitangent;

uniform mat4 projection;
uniform mat4 view;
uniform mat4 model;

out vec3 vFragPos;
out vec2 vTexCoord;
out mat3 vTBN;

void main()
{
    vec4 pos = model * vec4(vertex, 1.0);
    vFragPos = pos.xyz;
    gl_Position = projection * view * pos;
    vTexCoord = texCoord;
    vTexCoord.y = 1.0 - vTexCoord.y;

    // we are not doing any non-uniform scaling so we don't need
    // transpose(inverse(model))
    // + according to real-time rendering book tangents should be simply multiplied by
    // model matrix without translation

    mat3 model3 = mat3(model);

    vec3 N = normalize(model3 * normal);
    vec3 T = normalize(model3 * tangent);
    vec3 B = normalize(model3 * bitangent);
    vTBN = mat3(T, B, N);
}
