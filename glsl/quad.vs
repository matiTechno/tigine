#version 330

layout(location = 0) in vec2 vertex;
out vec2 vTexCoord;

void main()
{
    gl_Position = vec4(vertex, 0.0, 1.0);
    vTexCoord = (vertex + 1.0) / 2.0;
}
