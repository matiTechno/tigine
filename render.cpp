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
    int numIndices;
    int indicesOffset = 0;
    int idxMaterial = 0;
};

struct Model
{
    int idxMesh;
    int meshCount = 0;
    mat4 transform;
};

enum
{
    UNIT_DEFAULT,
    UNIT_DIFFUSE,
    UNIT_SPECULAR,
    UNIT_NORMAL,
    UNIT_ALPHA,
    UNIT_SHADOW_MAP,
    UNIT_POSITION
};

enum
{
    VIEW_DEPTH,
    VIEW_POSITIONS,
    VIEW_NORMALS,
    VIEW_TEXTURE_DIFFUSE,
    VIEW_TEXTURE_SPECULAR,
    VIEW_WIREFRAME,
    VIEW_SHADOWMAP,
    VIEW_FINAL,
    VIEW_COUNT
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
    static Model sphereModel;
    static Array<Model> models;
    static Array<Mesh> meshes;
    static Array<Material> materials;
    static Array<GLuint> textures;
    static Array<TexId> texIds;
    static Camera3d camera;
    static Shader shaderPlainColor;
    static Shader shaderDepth;
    static ivec2 prevFramebufferSize = ivec2(-1);
    static int outputView = VIEW_FINAL;

    struct
    {
        bool ambient = true;
        bool diffuse = true;
        bool specular = true;
        bool normalMaps = true;
        bool blinnPhong = true;
        bool toneMapping = true;
        bool srgbDiffuseTextures = true;
        bool srgbOutput = true;
        bool shadows = true;
        bool debugUvs = false;
    } static config;

    struct
    {
        vec3 pos = vec3(0.6f, 2.f, -0.3f) * 3000.f; // direction == normalize(-pos)
        int idxModel;
        float scale = 100.f;
        vec3 color = vec3(5.f);
    } static light;

    struct
    {
        GLuint vao;
        GLuint bo;
    } static quad;

    struct
    {
        GLuint framebuffer;
        GLuint depthBuffer;
        GLuint positions;
        GLuint normals;
        GLuint colorDiffuse;
        GLuint colorSpecular;
        Shader shader;
    } static gbuffer;

    struct ShadowMap
    {
        enum {SIZE = 4096};
        GLuint depthBuffer;
        GLuint framebuffer;
        Shader shader;
    } static shadowMap;

    struct
    {
        Shader shaderToneMapping;
        Shader shaderLightPass;
        GLuint framebuffer;
        GLuint texture;
    } static hdr;

    static bool init = true;
    if(init)
    {
        init = false;

        glDepthFunc(GL_LESS);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);
        glDisable(GL_BLEND);

        shaderPlainColor = createShader("glsl/plain-color.vs", "glsl/plain-color.fs");

        shaderDepth = createShader("glsl/quad.vs", "glsl/depth.fs");
        shaderDepth.bind();
        shaderDepth.uniform1i("sampler", UNIT_DEFAULT);

        addTexture("data/uv.png", textures, texIds, false);
        materials.pushBack({});

        loadModel("data/sphere.obj", models, meshes, materials, textures, texIds);

        if(models.empty())
        {
            printf("missing data/sphere.obj, run application from top source directory\n");
            assert(false);
        }

        // hack
        sphereModel = models.front();
        models.popBack();

        loadModel("data/sponza/sponza.obj", models, meshes, materials, textures, texIds);

        log("number of meshes:    %d", meshes.size());
        log("number of textures:  %d", textures.size());
        log("number of materials: %d", materials.size());

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

        // shadow map
        {
            shadowMap.shader = createShader("glsl/shadow.vs", "glsl/shadow.fs");

            glGenTextures(1, &shadowMap.depthBuffer);
            glBindTexture(GL_TEXTURE_2D, shadowMap.depthBuffer);

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

            // scene outside of a shadow map will be not covered by shadow
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

        // gbuffer
        {
            gbuffer.shader = createShader("glsl/gbuffer.vs", "glsl/gbuffer.fs");
            gbuffer.shader.bind();
            gbuffer.shader.uniform1i("samplerDiffuse", UNIT_DIFFUSE);
            gbuffer.shader.uniform1i("samplerSpecular", UNIT_SPECULAR);
            gbuffer.shader.uniform1i("samplerNormal", UNIT_NORMAL);

            glGenTextures(1, &gbuffer.depthBuffer);
            glBindTexture(GL_TEXTURE_2D, gbuffer.depthBuffer);

            glGenTextures(1, &gbuffer.positions);
            glBindTexture(GL_TEXTURE_2D, gbuffer.positions);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

            glGenTextures(1, &gbuffer.normals);
            glBindTexture(GL_TEXTURE_2D, gbuffer.normals);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

            glGenTextures(1, &gbuffer.colorDiffuse);
            glBindTexture(GL_TEXTURE_2D, gbuffer.colorDiffuse);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

            glGenTextures(1, &gbuffer.colorSpecular);
            glBindTexture(GL_TEXTURE_2D, gbuffer.colorSpecular);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

            glGenFramebuffers(1, &gbuffer.framebuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, gbuffer.framebuffer);

            GLenum bufs[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3};

            glDrawBuffers(getSize(bufs), bufs);
            glReadBuffer(GL_NONE); // just to be sure...

            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                    GL_TEXTURE_2D, gbuffer.depthBuffer, 0);

            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                    GL_TEXTURE_2D, gbuffer.positions, 0);

            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1,
                    GL_TEXTURE_2D, gbuffer.normals, 0);

            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2,
                    GL_TEXTURE_2D, gbuffer.colorDiffuse, 0);

            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3,
                    GL_TEXTURE_2D, gbuffer.colorSpecular, 0);
        }

        // hdr
        {
            hdr.shaderToneMapping = createShader("glsl/quad.vs", "glsl/tone-mapping.fs");
            hdr.shaderToneMapping.bind();
            hdr.shaderToneMapping.uniform1i("sampler", UNIT_DEFAULT);

            hdr.shaderLightPass = createShader("glsl/quad.vs", "glsl/light.fs");
            hdr.shaderLightPass.bind();
            hdr.shaderLightPass.uniform1i("samplerPosition", UNIT_POSITION);
            hdr.shaderLightPass.uniform1i("samplerNormal", UNIT_NORMAL);
            hdr.shaderLightPass.uniform1i("samplerDiffuse", UNIT_DIFFUSE);
            hdr.shaderLightPass.uniform1i("samplerSpecular", UNIT_SPECULAR);
            hdr.shaderLightPass.uniform1i("samplerShadowMap", UNIT_SHADOW_MAP);

            glGenTextures(1, &hdr.texture);
            glBindTexture(GL_TEXTURE_2D, hdr.texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

            glGenFramebuffers(1, &hdr.framebuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, hdr.framebuffer);
            GLenum buf = GL_COLOR_ATTACHMENT0;
            glDrawBuffers(1, &buf);
            glReadBuffer(GL_NONE); // just to be sure...

            // needed for forward rendering pass;
            // reuse the depth buffer from gbuffer
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                    GL_TEXTURE_2D, gbuffer.depthBuffer, 0);

            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                    GL_TEXTURE_2D, hdr.texture, 0);
        }
    }

    if(frame.quit)
    {
        // do the cleanup...
        return;
    }

    for(const WinEvent& e: frame.winEvents)
        camera.processEvent(e);

    camera.update(frame.dt);

    if(prevFramebufferSize != frame.bufferSize)
    {
        const ivec2 size = frame.bufferSize;
        prevFramebufferSize = size;

        // gbuffer
        {
            glBindTexture(GL_TEXTURE_2D, gbuffer.depthBuffer);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, size.x, size.y, 0,
                    GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);

            glBindTexture(GL_TEXTURE_2D, gbuffer.positions);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, size.x, size.y, 0, GL_RGB, GL_FLOAT,
                    nullptr);

            glBindTexture(GL_TEXTURE_2D, gbuffer.normals);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, size.x, size.y, 0, GL_RGB, GL_FLOAT,
                    nullptr);

            glBindTexture(GL_TEXTURE_2D, gbuffer.colorDiffuse);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, size.x, size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                    nullptr);

            glBindTexture(GL_TEXTURE_2D, gbuffer.colorSpecular);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, size.x, size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                    nullptr);
        }
        // hdr
        {
            glBindTexture(GL_TEXTURE_2D, hdr.texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, size.x, size.y, 0, GL_RGB, GL_FLOAT,
                    nullptr);
        }
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

    const mat4 projectionMatrix = perspective(45.f, (float)frame.bufferSize.x /
                                              frame.bufferSize.y, 0.1f, 20000.f);

    // render shadow map
    if(outputView == VIEW_FINAL || outputView == VIEW_SHADOWMAP)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, shadowMap.framebuffer);
        glClear(GL_DEPTH_BUFFER_BIT);

        if(config.shadows)
        {
            glViewport(0, 0, ShadowMap::SIZE, ShadowMap::SIZE);
            glEnable(GL_DEPTH_TEST);
            glEnable(GL_CULL_FACE);

            shadowMap.shader.bind();
            shadowMap.shader.uniformMat4("lightSpaceMatrix", lightSpaceMatrix);

            for(const Model& model: models)
            {
                shadowMap.shader.uniformMat4("model", model.transform);

                for(int i = 0; i < model.meshCount; ++i)
                {
                    const Mesh& mesh = meshes[model.idxMesh + i];
                    assert(mesh.indicesOffset);
                    glBindVertexArray(mesh.vao);

                    glDrawElements(GL_TRIANGLES, mesh.numIndices, GL_UNSIGNED_INT,
                            reinterpret_cast<const void*>(mesh.indicesOffset));
                }
            }
        }
    }

    // render gbuffer
    glBindFramebuffer(GL_FRAMEBUFFER, gbuffer.framebuffer);
    glViewport(0, 0, frame.bufferSize.x, frame.bufferSize.y);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    gbuffer.shader.bind();

    gbuffer.shader.uniformMat4("view", camera.view);
    gbuffer.shader.uniformMat4("projection", projectionMatrix);

    for(const Model& model: models)
    {
        gbuffer.shader.uniformMat4("model", model.transform);

        for(int i = 0; i < model.meshCount; ++i)
        {
            const Mesh& mesh = meshes[model.idxMesh + i];
            const Material& material = materials[mesh.idxMaterial];

            gbuffer.shader.uniform3f("colorDiffuse", outputView == VIEW_WIREFRAME ? vec3(1.f) : material.colorDiffuse);
            gbuffer.shader.uniform3f("colorSpecular", material.colorSpecular);

            gbuffer.shader.uniform1i("mapDiffuse", material.idxDiffuse && outputView != VIEW_WIREFRAME);
            gbuffer.shader.uniform1i("mapSpecular", material.idxSpecular);
            gbuffer.shader.uniform1i("mapNormal", material.idxNormal && config.normalMaps);
            gbuffer.shader.uniform1i("alphaTest", material.alphaTest);

            if(material.idxDiffuse)
            {
                if(config.debugUvs)
                    bindTexture(textures[0], UNIT_DIFFUSE);

                else if(config.srgbDiffuseTextures)
                    bindTexture(textures[material.idxDiffuse_srgb], UNIT_DIFFUSE);

                else
                    bindTexture(textures[material.idxDiffuse], UNIT_DIFFUSE);
            }

            if(material.idxSpecular)
                bindTexture(textures[material.idxSpecular], UNIT_SPECULAR);

            if(material.idxNormal)
                bindTexture(textures[material.idxNormal], UNIT_NORMAL);

            glBindVertexArray(mesh.vao);
            glDrawElements(outputView == VIEW_WIREFRAME ? GL_LINES : GL_TRIANGLES, mesh.numIndices,
                           GL_UNSIGNED_INT, reinterpret_cast<const void*>(mesh.indicesOffset));
        }
    }

    // render light to a hdr texture
    if(outputView == VIEW_FINAL)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, hdr.framebuffer);
        glViewport(0, 0, frame.bufferSize.x, frame.bufferSize.y);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);

        hdr.shaderLightPass.bind();

        hdr.shaderLightPass.uniformMat4("lightSpaceMatrix", lightSpaceMatrix);
        hdr.shaderLightPass.uniform3f("uLightDir", normalize(-light.pos));
        hdr.shaderLightPass.uniform3f("lightColor", light.color);
        hdr.shaderLightPass.uniform3f("cameraPos", camera.pos);
        hdr.shaderLightPass.uniform1i("enableAmbient", config.ambient);
        hdr.shaderLightPass.uniform1i("enableDiffuse", config.diffuse);
        hdr.shaderLightPass.uniform1i("enableSpecular", config.specular);
        hdr.shaderLightPass.uniform1i("blinnPhong", config.blinnPhong);

        bindTexture(gbuffer.positions, UNIT_POSITION);
        bindTexture(gbuffer.normals, UNIT_NORMAL);
        bindTexture(gbuffer.colorDiffuse, UNIT_DIFFUSE);
        bindTexture(gbuffer.colorSpecular, UNIT_SPECULAR);
        bindTexture(shadowMap.depthBuffer, UNIT_SHADOW_MAP);

        glBindVertexArray(quad.vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // forward rendering; sky and light sources
        glEnable(GL_DEPTH_TEST);

        shaderPlainColor.bind();

        shaderPlainColor.uniformMat4("view", camera.view);
        shaderPlainColor.uniformMat4("projection", projectionMatrix);

        const Mesh& mesh = meshes[sphereModel.idxMesh];
        glBindVertexArray(mesh.vao);

        shaderPlainColor.uniformMat4("model", translate(light.pos) * scale(vec3(light.scale)));
        shaderPlainColor.uniform3f("color", light.color);

        glEnable(GL_CULL_FACE);
        glDrawElements(GL_TRIANGLES, mesh.numIndices, GL_UNSIGNED_INT,
                       reinterpret_cast<const void*>(mesh.indicesOffset));

        shaderPlainColor.uniformMat4("model", scale(vec3(10000)));
        shaderPlainColor.uniform3f("color", vec3(0.1f, 0.1f, 1.f));

        glDisable(GL_CULL_FACE); // we are rendering the inside of a sphere
        glDrawElements(GL_TRIANGLES, mesh.numIndices, GL_UNSIGNED_INT,
                       reinterpret_cast<const void*>(mesh.indicesOffset));
    }

    // render to a default framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    if(config.srgbOutput)
        glEnable(GL_FRAMEBUFFER_SRGB);

    // defaults
    glViewport(0, 0, frame.bufferSize.x, frame.bufferSize.y);
    hdr.shaderToneMapping.bind();
    hdr.shaderToneMapping.uniform1i("toneMapping", false);
    GLuint texture = textures.front();

    switch(outputView)
    {
    case VIEW_DEPTH:
        shaderDepth.bind();
        texture = gbuffer.depthBuffer;
        break;

    case VIEW_POSITIONS:
        texture = gbuffer.positions;
        break;

    case VIEW_NORMALS:
        texture = gbuffer.normals;
        break;

    case VIEW_TEXTURE_DIFFUSE:
    case VIEW_WIREFRAME:
        texture = gbuffer.colorDiffuse;
        break;

    case VIEW_TEXTURE_SPECULAR:
        texture = gbuffer.colorSpecular;
        break;

    case VIEW_SHADOWMAP:
    {
        const int size = min( min(frame.bufferSize.x, frame.bufferSize.y), int(ShadowMap::SIZE) );
        glViewport(0, 0, size, size);
        shaderDepth.bind();
        texture = shadowMap.depthBuffer;
        break;
    }

    case VIEW_FINAL:
        hdr.shaderToneMapping.uniform1i("toneMapping", config.toneMapping);
        texture = hdr.texture;
        break;

    default: assert(false);
    }

    bindTexture(texture, UNIT_DEFAULT);
    glBindVertexArray(quad.vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisable(GL_FRAMEBUFFER_SRGB);

    if(!frame.showGui)
        return;

    ImGui::Begin("main");
    camera.imgui();
    ImGui::Checkbox("ambient  light", &config.ambient);
    ImGui::Checkbox("diffuse  light", &config.diffuse);
    ImGui::Checkbox("specular light", &config.specular);
    ImGui::Checkbox("normal maps", &config.normalMaps);
    ImGui::Checkbox("Blinn-Phong specular", &config.blinnPhong);
    ImGui::Checkbox("tone mapping (final image output)", &config.toneMapping);
    ImGui::Checkbox("sRGB diffuse textures", &config.srgbDiffuseTextures);
    ImGui::Checkbox("sRGB output", &config.srgbOutput);
    ImGui::Checkbox("shadows", &config.shadows);
    ImGui::Checkbox("debug UV diffuse texture", &config.debugUvs);

    const char* items[VIEW_COUNT] = {
        "depth",
        "world space positions",
        "world space normals",
        "diffuse texture",
        "specular texture",
        "wireframe",
        "shadowmap",
        "final image",
    };

    ImGui::Spacing();
    ImGui::Combo("output", &outputView, items, VIEW_COUNT);
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
    model.idxMesh = meshes.size();

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
        mesh.numIndices = indices.size();
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
