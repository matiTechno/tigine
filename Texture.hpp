#pragma once

typedef unsigned int GLuint;

GLuint createDefaultTexture();
GLuint createTexture(const char* filename, bool srgb);
void bindTexture(GLuint texId, GLuint unit);
