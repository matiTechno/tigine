#version 330

in vec2 vTexCoord;
uniform sampler2D sampler;
out vec4 color;

void main()
{
    color = vec4(texture(sampler, vTexCoord).rrr, 1.0);
}
