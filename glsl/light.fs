#version 330

in vec2 vTexCoord;

out vec4 outputColor;

uniform vec3 lightDir;
uniform vec3 lightColor;
uniform vec3 cameraPos;
uniform mat4 lightSpaceMatrix;

uniform sampler2D samplerPosition;
uniform sampler2D samplerNormal;
uniform sampler2D samplerDiffuse;
uniform sampler2D samplerSpecular;

uniform sampler2D samplerShadowMap;

float calcShadow(vec3 lightSpacePosition)
{
    vec3 posWindowSpace = lightSpacePosition * 0.5 + 0.5;
    float fragDepth = posWindowSpace.z;
    float bias = 0.005;
    float shadow = 0.0;
    vec2 texelSize = vec2(1.0) / textureSize(samplerShadowMap, 0);

    for(int x = -1; x <= 1; ++x)
    {
        for(int y = -1; y <= 1; ++y)
        {
            float closestDepth = texture2D(samplerShadowMap, posWindowSpace.xy +
                vec2(x, y) * texelSize).r;

            shadow += fragDepth > closestDepth + bias ? 1.0 : 0.0;
        }
    }

    return shadow / 9.0;
}

void main()
{
    vec3 diffuseSample = texture2D(samplerDiffuse, vTexCoord).rgb;
    vec3 specularSample = texture2D(samplerSpecular, vTexCoord).rgb;
    vec3 normal = texture2D(samplerNormal, vTexCoord).rgb;
    vec3 position = texture2D(samplerPosition, vTexCoord).rgb;

    vec3 ambient = diffuseSample * lightColor * 0.01;

    vec3 diffuse = diffuseSample * lightColor * max(0.0, dot(-lightDir, normal));

    vec3 specular = specularSample;
    vec3 viewDir = normalize(position - cameraPos);
    vec3 h = normalize(-lightDir + -viewDir);
    specular *= pow( max( dot(h, normal), 0.0 ), 10.0 );

    float shadow = calcShadow( (lightSpaceMatrix * vec4(position, 1.0)).xyz );

    outputColor = vec4(ambient + (1.0 - shadow) *diffuse + (1.0 - shadow) * specular, 1.0);
}
