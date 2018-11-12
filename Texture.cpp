#include "glad.h"
#include "Texture.hpp"
#include "api.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

void bindTexture(GLuint texId, GLuint unit)
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, texId);
}

GLuint createDefaultTexture()
{
    GLuint id;
    glGenTextures(1, &id);
    bindTexture(id, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    const unsigned char color[] = {0, 255, 0, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, color);
    return id;
}

GLuint createTexture(const char* filename, bool srgb)
{
    int width, height;
    //stbi_set_flip_vertically_on_load(true);
    unsigned char* const data = stbi_load(filename, &width, &height, nullptr, 4);

    if(!data)
    {
        log("stbi_load() failed: %s", filename);
        return createDefaultTexture();
    }

    GLuint id;
    glGenTextures(1, &id);
    bindTexture(id, 0);

    glTexImage2D(GL_TEXTURE_2D, 0, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, width, height, 0,
            GL_RGBA, GL_UNSIGNED_BYTE, data);

    stbi_image_free(data);

    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

    return id;
}
