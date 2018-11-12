#version 330

in vec3 vFragPos;
in vec3 vFragPosLightSpace;
in vec2 vTexCoord;
in mat3 vTBN;

uniform mat4 view;
uniform vec3 lightDir;
uniform vec3 lightColor;
uniform bool renderLightSource;
uniform vec3 cameraPos;

uniform bool alphaTest;

uniform sampler2D samplerDiffuse;
uniform vec3 colorDiffuse;
uniform bool mapDiffuse;

uniform sampler2D samplerSpecular;
uniform vec3 colorSpecular;
uniform bool mapSpecular;

uniform sampler2D samplerNormal;
uniform bool mapNormal;

uniform sampler2D samplerShadowMap;

uniform bool debugNormals;
uniform bool blinnPhong;
uniform bool enableAmbient;
uniform bool enableDiffuse;
uniform bool enableSpecular;
uniform bool doLighting;

out vec4 outputColor;

float calcShadow()
{
    vec3 posWindowSpace = vFragPosLightSpace * 0.5 + 0.5;
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
    if(renderLightSource)
    {
        outputColor = vec4(colorDiffuse, 1.0);
        return;
    }

    vec4 diffuseSample = texture2D(samplerDiffuse, vTexCoord).rgba;

    // this impacts performace; create second shader for this case
    if(alphaTest)
    {
        if(diffuseSample.a < 0.5)
            discard;
    }

    vec3 diffuse = colorDiffuse;

    if(mapDiffuse)
        diffuse *= diffuseSample.rgb;

    vec3 ambient = 0.01 * lightColor * diffuse;
        
    vec3 normal = normalize(vTBN[2]);

    if(mapNormal)
    {
        vec3 sample = texture2D(samplerNormal, vTexCoord).rgb;
        vec3 tangentNormal = normalize(sample * 2.0 - 1.0);
        normal = normalize(vTBN * tangentNormal);
    }

    if(debugNormals)
    {
        outputColor = vec4(normal, 1.0);
        return;
    }

    if(doLighting)
        diffuse *= lightColor * max(0.0, dot(-lightDir, normal));

    vec3 specular = colorSpecular;
    
    if(mapSpecular)
        specular *= texture2D(samplerSpecular, vTexCoord).rgb;

    vec3 viewDir = normalize(vFragPos - cameraPos);

    if(doLighting && blinnPhong)
    {
        vec3 h = normalize(-lightDir + -viewDir);
        specular *= pow( max( dot(h, normal), 0.0 ), 10.0 );
    }
    else if(doLighting)
    {
        vec3 reflectDir = reflect(lightDir, normal);
        specular *= pow( max( dot(-viewDir, reflectDir), 0.0 ), 32.0 );
    }

    float shadow = calcShadow();

    outputColor = vec4(vec3(enableAmbient)  * ambient +
                       vec3(enableDiffuse)  * (1.0 - shadow) * diffuse +
                       vec3(enableSpecular) * (1.0 - shadow) * specular, 1.0);
}
