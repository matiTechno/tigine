#version 330

in vec3 vFragPos;
in vec2 vTexCoord;
in mat3 vTBN;

uniform bool alphaTest;

uniform sampler2D samplerDiffuse;
uniform vec3 colorDiffuse;
uniform bool mapDiffuse;

uniform sampler2D samplerSpecular;
uniform vec3 colorSpecular;
uniform bool mapSpecular;

uniform sampler2D samplerNormal;
uniform bool mapNormal;

layout(location = 0) out vec3 outputPosition;
layout(location = 1) out vec3 outputNormal;
layout(location = 2) out vec3 outputDiffuse;
layout(location = 3) out vec3 outputSpecular;

void main()
{
    outputPosition = vFragPos;

    vec4 diffuseSample = texture(samplerDiffuse, vTexCoord).rgba;

    if(alphaTest)
    {
        if(diffuseSample.a < 0.5)
            discard;
    }

    outputDiffuse = colorDiffuse;

    if(mapDiffuse)
        outputDiffuse *= diffuseSample.rgb;

    outputSpecular = colorSpecular;

    if(mapSpecular)
        outputSpecular *= texture(samplerSpecular, vTexCoord).rgb;

    outputNormal = normalize(vTBN[2]);

    if(mapNormal)
    {
        vec3 sample = texture(samplerNormal, vTexCoord).rgb;
        vec3 tangentNormal = normalize(sample * 2.0 - 1.0);
        outputNormal = normalize(vTBN * tangentNormal);
    }
}
