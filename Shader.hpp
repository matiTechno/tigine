#pragma once

#include "glad.h"
#include "math.hpp"
#include "api.hpp"

struct UniformL
{
    char* name;
    GLint location;
};

GLint getUniformLocation(const char* shaderId, const char* uname,
        const UniformL* uniform);

struct Shader
{
    GLuint programId;
    UniformL uniforms[32];
    char* id;

    void bind()
    {
        glUseProgram(programId);
    }

    void uniform1i(const char* uname, int v)
    {
        if(programId)
            glUniform1i(getUniformLocation(id, uname, uniforms), v);
    }

    void uniform1f(const char* uname, float v)
    {
        if(programId)
            glUniform1f(getUniformLocation(id, uname, uniforms), v);
    }

    void uniform3fv(const char* uname, int count, const vec3* v)
    {
        if(programId)
        {
            // there is some confusion with glGetActiveUniform for array uniforms
            const int location = glGetUniformLocation(programId, uname);
            if(location == -1)
            {
                log("shader '%s': uniform '%s' is inactive", id, uname);
                return;
            }

            glUniform3fv(location, count, &v->x);
        }
    }

    void uniform3f(const char* uname, vec3 v)
    {
        if(programId)
            glUniform3f(getUniformLocation(id, uname, uniforms), v.x, v.y, v.z);
    }
    
    void uniformMat4(const char* uname, const mat4& m)
    {
        if(programId)
            glUniformMatrix4fv(getUniformLocation(id, uname, uniforms), 1, false,
                    &m[0][0]);
    }
};

Shader createShader(const char* vs, const char* fs);

void deleteShader(Shader& shader);
