#version 330

in vec2 vTexCoord;

uniform sampler2D samplerSsao;

out float color;

const int blurSize = 5;

void main()
{
    color = 0.0;
    vec2 texelSize = 1.0 / textureSize(samplerSsao, 0);
    vec2 start = vec2(-blurSize) * 0.5 + 0.5;

    for(int i = 0; i < blurSize; ++i)
    {
        for(int j = 0; j < blurSize; ++j)
        {
            vec2 offset = (start + vec2(i, j)) * texelSize;
            color += texture(samplerSsao, vTexCoord + offset).r;
        }
    }

    color = color / (blurSize * blurSize);
}
