#version 330

in vec2 vTexCoord;

uniform sampler2D samplerPosition;
uniform sampler2D samplerNormal;
uniform sampler2D samplerNoise;

uniform mat4 view;
uniform mat4 projection;
uniform int noiseTextureSize;
uniform float radius;

#define SAMPLES_SIZE 24

uniform vec3 samples[SAMPLES_SIZE];

out float color;

void main()
{
    // we are doing calculations in view space

    vec3 normal = mat3(view) * texture(samplerNormal, vTexCoord).xyz;

    vec3 tangent;
    {
        vec2 scale = textureSize(samplerPosition, 0) / float(noiseTextureSize);
        vec3 noise = texture(samplerNoise, vTexCoord * scale).xyz;
        tangent = normalize(noise - normal * dot(noise, normal));
    }

    vec3 bitangent = cross(normal, tangent);

    mat3 TBN = mat3(tangent, bitangent, normal);

    vec3 hemispherePos = vec3( view * vec4(texture(samplerPosition, vTexCoord).xyz, 1.0) );

    float occlusion = 0.0;

    for(int i = 0; i < SAMPLES_SIZE; ++i)
    {
        vec3 samplePos = hemispherePos + TBN * samples[i] * radius;

        // clip space
        vec4 posClip = projection * vec4(samplePos, 1.0);

        // ndc
        vec3 posNdc = posClip.xyz / posClip.w;

        // window space
        vec3 posWin = posNdc * 0.5 + 0.5;

        vec3 posWorld = texture(samplerPosition, posWin.xy).xyz;

        float depth = (view * vec4(posWorld, 1.0)).z;

        float inRange = abs(hemispherePos.z - depth) <= radius ? 1.0 : 0.0;

        occlusion += (samplePos.z < depth ? 1.0 : 0.0) * inRange;
    }

    color = 1.0 - occlusion / SAMPLES_SIZE;
}
