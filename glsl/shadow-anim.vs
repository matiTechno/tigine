#version 330

layout(location = 0) in vec3 vertex;
layout(location = 5) in ivec4 boneIds;
layout(location = 6) in vec4 boneWeights;

uniform mat4 model;
uniform mat4 lightSpaceMatrix;
uniform mat4 bones[64];

void main()
{
    mat4 boneTransform = bones[boneIds.x] * boneWeights.x;
    boneTransform += bones[boneIds.y] * boneWeights.y;
    boneTransform += bones[boneIds.z] * boneWeights.z;
    boneTransform += bones[boneIds.w] * boneWeights.w;

    gl_Position = lightSpaceMatrix * model * boneTransform * vec4(vertex, 1.0);
}
