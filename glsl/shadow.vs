#version 330

layout(location = 0) in vec3 pos;

uniform mat4 model;
uniform mat4 lightSpaceMatrix;

void main()
{
    gl_Position = lightSpaceMatrix * model * vec4(pos, 1.0);
}
