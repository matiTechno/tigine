#version 330

in vec2 vTexCoord;

uniform sampler2D sampler;
uniform bool linearize;
uniform float near;
uniform float far;

out vec4 color;

void main()
{
    // [0, 1]
    float windowSpaceDepth = texture(sampler, vTexCoord).r;

    if(!linearize)
    {
        color = vec4(vec3(windowSpaceDepth), 1.0);
        return;
    }

    // [-1, 1]
    float ndcDepth = windowSpaceDepth * 2.0 - 1.0;

    // [-near, -far]
    // -far is always less than -near but ndc is a right-handed coordinate system so the order
    // is reversed here

    float eyeSpaceDepth = (2.0 * far * near) / (ndcDepth * (far - near) - (far + near));

    // [0, 1]
    float mapped = (eyeSpaceDepth * -1.0 - near) / (far - near);

    color = vec4(vec3(mapped), 1.0);
}
