#version 330

in vec2 vTexCoord;

uniform sampler2D sampler;
uniform bool toneMapping;

out vec4 outputColor;

void main()
{
    vec4 hdrColor = texture(sampler, vTexCoord) ;

    if(!toneMapping)
    {
        outputColor = hdrColor;
        return;
    }

    outputColor = vec4(1.0) - exp(-hdrColor);
}
