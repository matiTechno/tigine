#include "Texture.hpp"
#include "Array.hpp"
#include "glad.h"
#include "api.hpp"
#include "math.hpp"
#include "imgui/imgui.h"
#include "Camera.hpp"
#include "Shader.hpp"

#include <assert.h>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

struct Mesh
{
    GLuint vao;
    GLuint bo;
    int elementCount;
    int indicesOffset = 0; // if 0 elementCount refers to vertices else to indices
    int idxMaterial = 0;
};

struct Model
{
    int idxStart;
    int meshCount = 0;
    mat4 transform;
    bool renderLightSource = false;
};

enum
{
    DIFFUSE,
    SPECULAR,
    NORMAL,
    ALPHA,
    SHADOW_MAP
};

struct Material
{
    // 0 - disables sampling
    int idxDiffuse_srgb = 0;
    int idxDiffuse = 0;
    int idxSpecular = 0;
    int idxNormal = 0;
    bool alphaTest = false;

    vec3 colorDiffuse = {1.f, 1.f, 1.f};
    vec3 colorSpecular = {1.f, 1.f, 1.f};
};

struct TexId
{
    char* filename;
    bool srgb;
};

static void loadModel(const char* filename, Array<Model>& models, Array<Mesh>& meshes,
        Array<Material>& materials, Array<GLuint>& textures, Array<TexId>& texIds);

static int addTexture(const char* filename, Array<GLuint>& textures,
        Array<TexId>& texIds, bool srgb)
{
    int idx = 0;
    for(TexId id: texIds)
    {
        if( (strcmp(id.filename, filename) == 0) && (id.srgb == srgb) )
            return idx;
        ++idx;
    }

    textures.pushBack(createTexture(filename, srgb));

    int size = strlen(filename) + 1;
    char* buf = (char*)malloc(size);
    memcpy(buf, filename, size);
    texIds.pushBack({buf, srgb});

    return textures.size() - 1;
}

void renderExecuteFrame(const Frame& frame)
{
    static Array<Mesh> meshes;
    static Array<Model> models;
    static Array<GLuint> textures;
    static Array<TexId> texIds;
    static Array<Material> materials;
    static Camera3d camera;
    static Shader shader;

    struct
    {
        bool ambient = true;
        bool diffuse = true;
        bool specular = true;
        bool normalMaps = true;
        bool debugUvs = false;
        bool debugNormals = false;
        bool blinnPhong = true;
        bool toneMapping = true;
        bool srgbDiffuseTextures = true;
        bool srgbOutput = true;
        bool doLighting = true;
        bool shadows = true;
        bool debugShadowMap = false;
    } static config;

    struct
    {
        vec3 pos; // direction == normalize(-pos)
        int idxModel;
        float scale = 100.f;
    } static light;

    struct
    {
        GLuint vao;
        GLuint bo;
    } static quad;

    struct
    {
        Shader shaderToneMapping;
        GLuint framebuffer;
        GLuint texture;
        GLuint depthBuffer;
    } static hdr;

    struct ShadowMap
    {
        enum {SIZE = 4096};
        GLuint depthBuffer;
        GLuint framebuffer;
        Shader shader;
        Shader shaderDebug;
    } static shadowMap;

    static bool init = true;
    if(init)
    {
        init = false;

        glDepthFunc(GL_LESS);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);
        glDisable(GL_BLEND);

        addTexture("data/uv.png", textures, texIds, false);
        materials.pushBack({});

        // light
        {
            light.pos = vec3(0.6f, 2.f, -0.3f) * 3000.f;

            loadModel("data/sphere.obj", models, meshes, materials, textures,
                    texIds);
            
            light.idxModel = models.size() - 1;

            models[light.idxModel].transform = translate(light.pos) *
                scale(vec3(light.scale));

            models[light.idxModel].renderLightSource = true;
            assert(materials.size() == 2); // assimp loads dummy material
            materials.back().colorDiffuse = vec3(3.5f);
        }

        loadModel("data/sponza/sponza.obj", models, meshes, materials, textures, texIds);

        log("number of meshes:    %d", meshes.size());
        log("number of textures:  %d", textures.size());
        log("number of materials: %d", materials.size());

        shader = createShader("glsl/test.vs", "glsl/test.fs");

        camera.speed = 500.f;

        // quad
        {
            float vertices[] =
            {
                -1.f, -1.f,
                1.f, -1.f,
                1.f, 1.f,
                1.f, 1.f,
                -1.f, 1.f,
                -1.f, -1.f
            };

            glGenVertexArrays(1, &quad.vao);
            glGenBuffers(1, &quad.bo);

            glBindBuffer(GL_ARRAY_BUFFER, quad.bo);
            glBufferData(GL_ARRAY_BUFFER, sizeof vertices, vertices, GL_STATIC_DRAW);
            
            glBindVertexArray(quad.vao);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, nullptr);
            glEnableVertexAttribArray(0);
        }

        // hdr
        {
            hdr.shaderToneMapping = createShader("glsl/quad.vs", "glsl/tone-mapping.fs");

            glGenTextures(1, &hdr.texture);
            glBindTexture(GL_TEXTURE_2D, hdr.texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

            glGenTextures(1, &hdr.depthBuffer);
            glBindTexture(GL_TEXTURE_2D, hdr.depthBuffer);

            glGenFramebuffers(1, &hdr.framebuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, hdr.framebuffer);
            GLenum buf = GL_COLOR_ATTACHMENT0;
            glDrawBuffers(1, &buf);
            glReadBuffer(GL_NONE); // just to be sure...

            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                    GL_TEXTURE_2D, hdr.texture, 0);

            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                    GL_TEXTURE_2D, hdr.depthBuffer, 0);
        }

        // shadow map
        {
            shadowMap.shader = createShader("glsl/shadow.vs", "glsl/shadow.fs");
            shadowMap.shaderDebug = createShader("glsl/quad.vs", "glsl/shadowDebug.fs");

            glGenTextures(1, &shadowMap.depthBuffer);
            glBindTexture(GL_TEXTURE_2D, shadowMap.depthBuffer);

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

            // scene outside of shadow map will be not covered by shadow
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
            const float borderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
            glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, ShadowMap::SIZE,
                    ShadowMap::SIZE, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);

            glGenFramebuffers(1, &shadowMap.framebuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, shadowMap.framebuffer);
            glDrawBuffer(GL_NONE);
            glReadBuffer(GL_NONE);

            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                    shadowMap.depthBuffer, 0);
        }
    }

    if(frame.quit)
    {
        for(const Mesh& mesh: meshes)
        {
            glDeleteVertexArrays(1, &mesh.vao);
            glDeleteBuffers(1, &mesh.bo);
        }

        for(GLuint& texture: textures)
            glDeleteTextures(1, &texture);

        for(TexId id: texIds)
            free(id.filename);

        deleteShader(shader);

        glDeleteVertexArrays(1, &quad.vao);
        glDeleteBuffers(1, &quad.bo);

        deleteShader(hdr.shaderToneMapping);
        glDeleteFramebuffers(1, &hdr.framebuffer);
        glDeleteTextures(1, &hdr.texture);
        glDeleteTextures(1, &hdr.depthBuffer);

        deleteShader(shadowMap.shader);
        deleteShader(shadowMap.shaderDebug);
        glDeleteFramebuffers(1, &shadowMap.framebuffer);
        glDeleteTextures(1, &shadowMap.depthBuffer);

        return;
    }

    for(const WinEvent& e: frame.winEvents)
        camera.processEvent(e);

    camera.update(frame.dt);

    // update HDR texture and depth buffer sizes
    {
        const ivec2 size = frame.bufferSize;

        glBindTexture(GL_TEXTURE_2D, hdr.texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, size.x, size.y, 0, GL_RGB, GL_FLOAT,
                nullptr);

        glBindTexture(GL_TEXTURE_2D, hdr.depthBuffer);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, size.x, size.y, 0,
                GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    }

    mat4 lightSpaceMatrix;
    {
        // this is hardcoded for sponza...
        const float size = 1900.f;

        // if target == up then lookAt() failes; this is a workaround
        const vec3 lpos = light.pos + vec3(0.01f, 0.f, 0.f);

        lightSpaceMatrix = orthographic(-size, size, -size, size, 0.01f, size * 1.5f) *
            lookAt(normalize(lpos) * size, vec3(0.f), vec3(0.f, 1.f, 0.f));
    }

    // render shadow map
    glBindFramebuffer(GL_FRAMEBUFFER, shadowMap.framebuffer);
    glClear(GL_DEPTH_BUFFER_BIT);

    if(config.shadows)
    {
        glViewport(0, 0, ShadowMap::SIZE, ShadowMap::SIZE);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);

        glUseProgram(shadowMap.shader.programId);
        shadowMap.shader.uniformMat4("lightSpaceMatrix", lightSpaceMatrix);

        for(const Model& model: models)
        {
            shadowMap.shader.uniformMat4("model", model.transform);

            for(int i = 0; i < model.meshCount; ++i)
            {
                const Mesh& mesh = meshes[model.idxStart + i];

                glBindVertexArray(mesh.vao);

                if(mesh.indicesOffset)
                {
                    glDrawElements(GL_TRIANGLES, mesh.elementCount, GL_UNSIGNED_INT,
                            reinterpret_cast<const void*>(mesh.indicesOffset));
                }
                else
                    glDrawArrays(GL_TRIANGLES, 0, mesh.elementCount);
            }
        }
    }

    // render models to the HDR buffer
    glBindFramebuffer(GL_FRAMEBUFFER, hdr.framebuffer);
    glViewport(0, 0, frame.bufferSize.x, frame.bufferSize.y);
    glClearColor(0.1f, 0.1f, 1.f, 1.f); // sky color
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    glUseProgram(shader.programId);

    shader.uniformMat4("view", camera.view);
    shader.uniform3f("cameraPos", camera.pos);

    shader.uniformMat4("projection", perspective(45.f, (float)frame.bufferSize.x /
                frame.bufferSize.y, 0.1f, 100000.f) );

    shader.uniform3f("lightDir", normalize(-light.pos));

    shader.uniform3f("lightColor", materials[
            meshes[ models[light.idxModel].idxStart ].idxMaterial ].colorDiffuse);

    shader.uniformMat4("lightSpaceMatrix", lightSpaceMatrix);
    shader.uniform1i("samplerShadowMap", SHADOW_MAP);
    bindTexture(shadowMap.depthBuffer, SHADOW_MAP);

    shader.uniform1i("debugNormals", config.debugNormals);
    shader.uniform1i("blinnPhong", config.blinnPhong);
    shader.uniform1i("enableAmbient", config.ambient);
    shader.uniform1i("enableDiffuse", config.diffuse);
    shader.uniform1i("enableSpecular", config.specular);
    shader.uniform1i("doLighting", config.doLighting);

    for(const Model& model: models)
    {
        shader.uniformMat4("model", model.transform);
        shader.uniform1i("renderLightSource", model.renderLightSource);

        for(int i = 0; i < model.meshCount; ++i)
        {
            const Mesh& mesh = meshes[model.idxStart + i];
            glBindVertexArray(mesh.vao);

            const Material& material = materials[mesh.idxMaterial];

            shader.uniform3f("colorDiffuse", material.colorDiffuse);
            shader.uniform3f("colorSpecular", material.colorSpecular);

            shader.uniform1i("mapDiffuse", material.idxDiffuse);
            shader.uniform1i("mapSpecular", material.idxSpecular);
            shader.uniform1i("mapNormal", material.idxNormal && config.normalMaps);
            shader.uniform1i("alphaTest", material.alphaTest);

            if(material.idxDiffuse)
            {
                shader.uniform1i("samplerDiffuse", DIFFUSE);

                if(config.debugUvs)
                    bindTexture(textures[0], DIFFUSE);

                else if(config.srgbDiffuseTextures)
                    bindTexture(textures[material.idxDiffuse_srgb], DIFFUSE);
                else
                    bindTexture(textures[material.idxDiffuse], DIFFUSE);
            }

            if(material.idxSpecular)
            {
                shader.uniform1i("samplerSpecular", SPECULAR);
                bindTexture(textures[material.idxSpecular], SPECULAR);
            }

            if(material.idxNormal)
            {
                shader.uniform1i("samplerNormal", NORMAL);
                bindTexture(textures[material.idxNormal], NORMAL);
            }

            if(mesh.indicesOffset)
            {
                glDrawElements(GL_TRIANGLES, mesh.elementCount, GL_UNSIGNED_INT,
                        reinterpret_cast<const void*>(mesh.indicesOffset));
            }
            else
                glDrawArrays(GL_TRIANGLES, 0, mesh.elementCount);
        }
    }

    // render the HDR image to the default framebuffer;
    // convert colors from linear to sRGB space
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, frame.bufferSize.x, frame.bufferSize.y);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    glUseProgram(hdr.shaderToneMapping.programId);
    hdr.shaderToneMapping.uniform1i("sampler", DIFFUSE);
    hdr.shaderToneMapping.uniform1i("toneMapping", config.toneMapping &&
            !config.debugNormals);

    bindTexture(hdr.texture, DIFFUSE);
    glBindVertexArray(quad.vao);

    if(config.srgbOutput) glEnable(GL_FRAMEBUFFER_SRGB);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisable(GL_FRAMEBUFFER_SRGB);

    // render debug shadow map
    if(config.debugShadowMap)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        const int size = min( min(frame.bufferSize.x, frame.bufferSize.y),
                int(ShadowMap::SIZE) );
        glViewport(0, 0, size, size);

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);

        glUseProgram(shadowMap.shaderDebug.programId);
        hdr.shaderToneMapping.uniform1i("sampler", DIFFUSE);
        bindTexture(shadowMap.depthBuffer, DIFFUSE);
        glBindVertexArray(quad.vao);

        glEnable(GL_FRAMEBUFFER_SRGB);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glDisable(GL_FRAMEBUFFER_SRGB);
    }

    if(!frame.showGui)
        return;

    ImGui::Begin("main");
    camera.imgui();
    ImGui::Checkbox("ambient", &config.ambient);
    ImGui::Checkbox("diffuse", &config.diffuse);
    ImGui::Checkbox("specular", &config.specular);
    ImGui::Checkbox("normal maps", &config.normalMaps);
    ImGui::Checkbox("debug normals", &config.debugNormals);
    ImGui::Checkbox("debug uvs", &config.debugUvs);
    ImGui::Checkbox("Blinn-Phong specular", &config.blinnPhong);
    ImGui::Checkbox("tone mapping", &config.toneMapping);
    ImGui::Checkbox("sRGB diffuse textures", &config.srgbDiffuseTextures);
    ImGui::Checkbox("sRGB output", &config.srgbOutput);
    ImGui::Checkbox("do lighting", &config.doLighting);
    ImGui::Checkbox("shadows", &config.shadows);
    ImGui::Checkbox("debug shadow map", &config.debugShadowMap);
    ImGui::End();
}

static const char* concatenate(const char* l, const char* r)
{
    static char buf[1024];
    const int llen = strlen(l);
    const int rlen = strlen(r);
    assert(llen + rlen < int(sizeof buf));
    memcpy(buf, l, llen);
    memcpy(buf + llen, r, rlen);
    buf[llen + rlen] = '\0';
    return buf;
}

static void loadModel(const char* filename, Array<Model>& models, Array<Mesh>& meshes,
        Array<Material>& materials, Array<GLuint>& textures, Array<TexId>& texIds)
{
    char dirpath[256];
    {
        // this will not work on windows
        int len = strlen(filename);
        assert(len < int(sizeof dirpath));
        int i;

        for(i = len; i >= 0; --i)
        {
            if(filename[i] == '/')
            {
                memcpy(dirpath, filename, i + 1);
                dirpath[i + 1] = '\0';
                break;
            }
        }

        if(i == -1)
        {
            memcpy(dirpath, filename, len);
            dirpath[len - 1] = '/';
            dirpath[len] = '\0';
        }
    }

    Assimp::Importer importer;

    const aiScene* const scene = importer.ReadFile(filename, aiProcess_Triangulate |
            aiProcess_JoinIdenticalVertices | aiProcess_CalcTangentSpace);

    if(!scene)
    {
        log("Assimp::Importer::ReadFile() failed, %s", filename);
        return;
    }

    const int materialOffset = materials.size();

    aiString textureFilename;
    for(unsigned idxMat = 0; idxMat < scene->mNumMaterials; ++idxMat)
    {
        Material material;
        
        aiColor3D color;

        scene->mMaterials[idxMat]->Get(AI_MATKEY_COLOR_DIFFUSE, color);
        material.colorDiffuse = {color.r, color.g, color.b};

        scene->mMaterials[idxMat]->Get(AI_MATKEY_COLOR_SPECULAR, color);
        material.colorSpecular = {color.r, color.g, color.b};

        if(scene->mMaterials[idxMat]->GetTextureCount(aiTextureType_DIFFUSE) > 0)
        {
            scene->mMaterials[idxMat]->GetTexture(aiTextureType_DIFFUSE, 0,
                    &textureFilename);

            material.idxDiffuse_srgb = addTexture(concatenate(dirpath,
                    textureFilename.C_Str()), textures, texIds, true);

            material.idxDiffuse = addTexture(concatenate(dirpath,
                    textureFilename.C_Str()), textures, texIds, false);

            // note: we are overwriting the color
            material.colorDiffuse = vec3(1.f);
        }

        if(scene->mMaterials[idxMat]->GetTextureCount(aiTextureType_SPECULAR) > 0)
        {
            scene->mMaterials[idxMat]->GetTexture(aiTextureType_SPECULAR, 0,
                    &textureFilename);

            material.idxSpecular = addTexture(concatenate(dirpath,
                        textureFilename.C_Str()), textures, texIds, false);

            // same
            material.colorSpecular = vec3(1.f);
        }

        if(scene->mMaterials[idxMat]->GetTextureCount(aiTextureType_NORMALS) > 0)
        {
            scene->mMaterials[idxMat]->GetTexture(aiTextureType_NORMALS, 0,
                    &textureFilename);

            material.idxNormal = addTexture(concatenate(dirpath,
                        textureFilename.C_Str()), textures, texIds, false);
        }

        if(scene->mMaterials[idxMat]->GetTextureCount(aiTextureType_OPACITY) > 0)
        {
            material.alphaTest = true;
        }

        materials.pushBack(material);
    }

    assert(scene->HasMeshes());
    Array<float> vertexData;
    Array<unsigned> indices;

    Model model;
    model.idxStart = meshes.size();

    for(unsigned idxMesh = 0; idxMesh < scene->mNumMeshes; ++idxMesh)
    {
        const aiMesh& aimesh = *scene->mMeshes[idxMesh];

        assert(aimesh.HasFaces());
        assert(aimesh.mFaces[0].mNumIndices == 3);
        assert(aimesh.HasPositions());

        const bool hasTexCoords = aimesh.HasTextureCoords(0);
        const bool hasNormals = aimesh.HasNormals();
        const bool hasTangents = aimesh.HasTangentsAndBitangents();
        const int floatsPerVertex = 3 + hasTexCoords * 2 + hasNormals * 3
            + hasTangents * 6;

        vertexData.clear();
        vertexData.reserve(aimesh.mNumVertices * floatsPerVertex);

        for(unsigned idxVert = 0; idxVert < aimesh.mNumVertices; ++idxVert)
        {
            const aiVector3D v = aimesh.mVertices[idxVert];
            vertexData.pushBack(v.x);
            vertexData.pushBack(v.y);
            vertexData.pushBack(v.z);

            if(hasTexCoords)
            {
                const aiVector3D t = aimesh.mTextureCoords[0][idxVert];
                vertexData.pushBack(t.x);
                vertexData.pushBack(t.y);
            }

            if(hasNormals)
            {
                const aiVector3D n = aimesh.mNormals[idxVert];
                vertexData.pushBack(n.x);
                vertexData.pushBack(n.y);
                vertexData.pushBack(n.z);
            }

            if(hasTangents)
            {
                const aiVector3D t = aimesh.mTangents[idxVert];
                vertexData.pushBack(t.x);
                vertexData.pushBack(t.y);
                vertexData.pushBack(t.z);

                // we don't want to calculate bitangents in a vertex shader
                // because it will not work correctly with flipped UVs;
                // at least this is my reasoning after days of debugging...
                const aiVector3D b = aimesh.mBitangents[idxVert];
                vertexData.pushBack(b.x);
                vertexData.pushBack(b.y);
                vertexData.pushBack(b.z);
            }
        }

        indices.clear();
        indices.reserve(aimesh.mNumFaces * 3);

        for(unsigned idxFace = 0; idxFace < aimesh.mNumFaces; ++idxFace)
        {
            for(int  i = 0; i < 3; ++i)
                indices.pushBack(aimesh.mFaces[idxFace].mIndices[i]);
        }

        meshes.pushBack({});
        Mesh& mesh = meshes.back();
        ++model.meshCount;
        mesh.elementCount = indices.size();
        mesh.idxMaterial = aimesh.mMaterialIndex + materialOffset;
        
        const GLsizeiptr verticesBytes = sizeof(float) * vertexData.size();
        const GLsizeiptr indicesBytes = sizeof(unsigned) * indices.size();

        mesh.indicesOffset = verticesBytes;

        glGenVertexArrays(1, &mesh.vao);
        glGenBuffers(1, &mesh.bo);

        glBindBuffer(GL_ARRAY_BUFFER, mesh.bo);
        glBufferData(GL_ARRAY_BUFFER, verticesBytes + indicesBytes, nullptr,
                GL_STATIC_DRAW);

        glBufferSubData(GL_ARRAY_BUFFER, 0, verticesBytes, vertexData.data());
        glBufferSubData(GL_ARRAY_BUFFER, mesh.indicesOffset, indicesBytes,
                indices.data());

        glBindVertexArray(mesh.vao);

        const GLsizei stride = floatsPerVertex * sizeof(float);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
        glEnableVertexAttribArray(0);

        int offset = 3 * sizeof(float);

        if(hasTexCoords)
        {
            glVertexAttribPointer( 1, 2, GL_FLOAT, GL_FALSE, stride,
                    reinterpret_cast<const void*>(offset) );

            glEnableVertexAttribArray(1);
            offset += 2 * sizeof(float);
        }

        if(hasNormals)
        {
            glVertexAttribPointer( 2, 3, GL_FLOAT, GL_FALSE, stride,
                    reinterpret_cast<const void*>(offset) );

            glEnableVertexAttribArray(2);
            offset += 3 * sizeof(float);
        }

        if(hasTangents)
        {
            glVertexAttribPointer( 3, 3, GL_FLOAT, GL_FALSE, stride,
                    reinterpret_cast<const void*>(offset) );

            glEnableVertexAttribArray(3);
            offset += 3 * sizeof(float);

            glVertexAttribPointer( 4, 3, GL_FLOAT, GL_FALSE, stride,
                    reinterpret_cast<const void*>(offset) );

            glEnableVertexAttribArray(4);
            offset += 3 * sizeof(float);
        }

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.bo);
    }

    models.pushBack(model);
}
