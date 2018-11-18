#include "Shader.hpp"
#include "Array.hpp"

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

void deleteShader(Shader& shader)
{
    if(!shader.programId)
        return;
    
    glDeleteProgram(shader.programId);

    UniformL* it = shader.uniforms;
    while(it->name)
    {
        free(it->name);
        ++it;
    }

    free(shader.id);
}

GLint getUniformLocation(const char* shaderId, const char* uname,
        const UniformL* uniform)
{
    for(;;)
    {
        if(uniform->name == nullptr) // delimiter
        {
            log("shader '%s': uniform '%s' is inactive", shaderId, uname);
            return 666; // just to be sure it is not in range of valid uniforms
        }

        if(strcmp(uniform->name, uname) == 0)
            return uniform->location;

        ++uniform;
    }
}

static bool isCompileError(const GLuint shader)
{
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    
    if(success == GL_TRUE)
        return false;
    else
    {
        char buffer[512];
        glGetShaderInfoLog(shader, sizeof(buffer), nullptr, buffer);
        log("glCompileShader() error:\n%s", buffer);
        return true;
    }
}

static void loadFile(const char* filename, Array<char>& buf)
{
    FILE* file = fopen(filename, "r");

    if(!file)
    {
        log("could not open '%s'", filename);
        return;
    }

    if(fseek(file, 0, SEEK_END) != 0)
    {
        log("fseek(): %s", strerror(errno));
        fclose(file);
        return;
    }

    const int size = ftell(file);

    if(size == EOF)
    {
        log("ftell(): %s", strerror(errno));
        fclose(file);
        return;
    }

    rewind(file);
    buf.resize(size);
    fread(buf.data(), 1, size, file);
    fclose(file);
    buf.pushBack('\0');
}

Shader createShader(const char* vs, const char* fs)
{
    Shader shader;

    // just use std::string...
    {
        const int size = strlen(fs) + 1;
        shader.id = (char*)malloc(size);
        memcpy(shader.id, fs, size);
    }

    shader.programId = 0;

    Array<char> vsBuf, fsBuf;

    loadFile(vs, vsBuf);
    loadFile(fs, fsBuf);

    if(vsBuf.empty() || fsBuf.empty())
        return shader;

    const GLuint vertex = glCreateShader(GL_VERTEX_SHADER);
    {
        const char* buf = vsBuf.data();
        glShaderSource(vertex, 1, &buf, nullptr);
    }

    glCompileShader(vertex);
    const bool vertexError = isCompileError(vertex);
    if(vertexError) log("vertex shader compilation failed: %s", vs);

    const GLuint fragment = glCreateShader(GL_FRAGMENT_SHADER);
    {
        const char* buf = fsBuf.data();
        glShaderSource(fragment, 1, &buf, nullptr);
    }

    glCompileShader(fragment);
    const bool fragmentError = isCompileError(fragment);
    if(fragmentError) log("fragment shader compilation failed: %s", fs);

    if(vertexError || fragmentError)
    {
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        return shader;
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDetachShader(program, vertex);
    glDetachShader(program, fragment);
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    
    if(success == GL_TRUE)
    {
        GLint numUniforms;
        glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &numUniforms);
        assert(numUniforms < getSize(shader.uniforms));

        for(int i = 0; i < numUniforms; ++i)
        {
            char name[256];
            GLint dum1;
            GLenum dum2;

            glGetActiveUniform(program, i, getSize(name), nullptr, &dum1, &dum2, name);

            const int size = strlen(name) + 1;
            shader.uniforms[i].name = (char*)malloc(size);
            memcpy(shader.uniforms[i].name, name, size);

            shader.uniforms[i].location = glGetUniformLocation(program, name);
        }

        shader.uniforms[numUniforms].name = nullptr;
        shader.programId = program;
        return shader;
    }
    else
    {
        char buffer[512];
        glGetProgramInfoLog(program, sizeof(buffer), nullptr, buffer);
        log("%s: glLinkProgram() error:\n%s", shader.id, buffer);
        glDeleteProgram(program);
        return shader;
    }
}
