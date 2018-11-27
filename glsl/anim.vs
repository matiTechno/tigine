#version 330

layout(location = 0) in vec3 vertex;
layout(location = 1) in vec2 texCoord;
layout(location = 2) in vec3 normal;
layout(location = 3) in vec3 tangent;
layout(location = 4) in vec3 bitangent;
layout(location = 5) in ivec4 boneIds;
layout(location = 6) in vec4 boneWeights;

uniform mat4 projection;
uniform mat4 view;
uniform mat4 model;
uniform mat4 bones[64];

out vec3 vFragPos;
out vec2 vTexCoord;
out mat3 vTBN;

void main()
{
    mat4 boneTransform = bones[boneIds.x] * boneWeights.x;
    boneTransform += bones[boneIds.y] * boneWeights.y;
    boneTransform += bones[boneIds.z] * boneWeights.z;
    boneTransform += bones[boneIds.w] * boneWeights.w;

    vec4 pos = model * boneTransform * vec4(vertex, 1.0);
    vFragPos = pos.xyz;
    gl_Position = projection * view * pos;
    vTexCoord = texCoord;
    vTexCoord.y = 1.0 - vTexCoord.y;

    mat3 model3 = mat3(model * boneTransform);

    vec3 N = normalize(model3 * normal);
    vec3 T = normalize(model3 * tangent);
    vec3 B = normalize(model3 * bitangent);
    vTBN = mat3(T, B, N);
}
