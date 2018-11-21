#version 330

in vec2 vTexCoord;

out vec4 outputColor;

uniform vec3 light_dir;
uniform vec3 light_color;
uniform vec3 cameraPos;
uniform mat4 lightSpaceMatrix;

uniform sampler2D samplerPosition;
uniform sampler2D samplerNormal;
uniform sampler2D samplerDiffuse;
uniform sampler2D samplerSpecular;
uniform sampler2D samplerShadowMap;
uniform sampler2D samplerSsao;

uniform int enableAmbient;
uniform int enableDiffuse;
uniform int enableSpecular;

float calcShadow(vec3 lightSpacePosition)
{
    // we don't have to do perspective division
    // light projection is orthographic (w = 1.0)

    vec3 posWindowSpace = lightSpacePosition * 0.5 + 0.5;
    float fragDepth = posWindowSpace.z;

    // don't show shadows outside of light projection
    if(fragDepth > 1.f)
        return 0.0;

    float bias = 0.005;
    float shadow = 0.0;
    vec2 texelSize = vec2(1.0) / textureSize(samplerShadowMap, 0);

    for(int x = -1; x <= 1; ++x)
    {
        for(int y = -1; y <= 1; ++y)
        {
            float closestDepth = texture(samplerShadowMap, posWindowSpace.xy +
                vec2(x, y) * texelSize).r;

            shadow += fragDepth > closestDepth + bias ? 1.0 : 0.0;
        }
    }

    return shadow / 9.0;
}

// conventions from 'Real-Time Rendering'

// E          light irradiance perpendicular to surface normal
// E_l        light irradiance perpendicular to light direction
// L          light radiance (irradiance coming from one direction)
// M          surface outgoing irradiance (exitance)
// L_o        surface outgoing radiance
// view_dir   direction from sample to sensor
// light_dir  direction from sample to light

void main()
{
    vec3 color_diffuse = texture(samplerDiffuse, vTexCoord).rgb;
    vec3 color_specular = texture(samplerSpecular, vTexCoord).rgb;
    vec3 normal = texture(samplerNormal, vTexCoord).rgb;
    vec3 position = texture(samplerPosition, vTexCoord).rgb;
    vec3 E_l = light_color;
    vec3 E = E_l * max(0.0, dot(light_dir, normal));

    vec3 L_o_ambient = color_diffuse * E_l * 0.01 * texture(samplerSsao, vTexCoord).r;

    vec3 L_o_diffuse = color_diffuse * E;

    vec3 L_o_specular;
    {
        vec3 M_specular = color_specular * E;

        vec3 view_dir = normalize(cameraPos - position);

        vec3 h = normalize(light_dir + view_dir);

        float m = 10.0; // surface smoothness parameter

        L_o_specular = M_specular * pow( max(0.0, dot(h, normal)), m );
    }

    float shadow = calcShadow( (lightSpaceMatrix * vec4(position, 1.0)).xyz );

    outputColor = vec4(
        enableAmbient  * L_o_ambient +
        enableDiffuse  * (1.0 - shadow) * L_o_diffuse +
        enableSpecular * (1.0 - shadow) * L_o_specular,
        1.0);
}
