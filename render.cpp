#include "Texture.hpp"
#include "Array.hpp"
#include "glad.h"
#include "api.hpp"
#include "math.hpp"
#include "imgui/imgui.h"
#include "Camera.hpp"
#include "Shader.hpp"

#include <assert.h>
#include <stdlib.h>
#include <time.h>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <vector>
#include <string>
#include <map>

static float randomFloat()
{
    return rand() / float(RAND_MAX);
}

enum
{
    MAX_WEIGHTS = 4,
    MAX_BONES = 64
};

struct PositionKey
{
    // transformation relative to parent bone
    vec3 position;
    float timestamp;
};

struct RotationKey
{
    vec4 rotation;
    float timestamp;
};

struct ScaleKey
{
    vec3 scale;
    float timestamp;
};

struct BoneKeys
{
    std::vector<PositionKey> positionKeys;
    std::vector<RotationKey> rotationKeys;
    std::vector<ScaleKey> scaleKeys;
};

struct Animation
{
    std::string name;
    float duration;
    std::map<int, BoneKeys> boneKeys;
};

struct Bone
{
    int idx = MAX_BONES; // MAX_BONES - does not affect any vertices
    std::vector<Bone> children;

    union
    {
        // when a bone is not animated it is used as replacement for interpolated transformation;
        // relative to parent bone
        mat4 transform;

        mat4 transformFromMeshToBoneSpace;
    };
};

struct Skeleton
{
    std::vector<Animation> animations;
    Bone rootBone;
};

static mat4 interpolateTranslation(const std::vector<PositionKey>& keys, float time)
{
    vec3 translation;

    if(keys.size() == 1)
        translation = keys.front().position;
    else
    {
        int currentKeyIdx = -1;

        for(int i = 0; i < keys.size() - 1; ++i)
        {
            if(time < keys[i + 1].timestamp)
            {
                currentKeyIdx = i;
                break;
            }
        }

        assert(currentKeyIdx != -1);

        PositionKey currentKey = keys[currentKeyIdx];
        PositionKey nextKey = keys[currentKeyIdx + 1];

        vec3 start = currentKey.position;
        vec3 end = nextKey.position;
        float transition = (time - currentKey.timestamp) / (nextKey.timestamp - currentKey.timestamp);
        translation = start + transition * (end - start);
    }

    return translate(translation);
}

// todo: replace aiQuaternion with own quaternion code
static mat4 interpolateRotation(const std::vector<RotationKey>& keys, float time)
{
    vec4 rotation;

    if(keys.size() == 1)
        rotation = keys.front().rotation;
    else
    {
        int currentKeyIdx = -1;

        for(int i = 0; i < keys.size() - 1; ++i)
        {
            if(time < keys[i + 1].timestamp)
            {
                currentKeyIdx = i;
                break;
            }
        }

        assert(currentKeyIdx != -1);

        RotationKey currentKey = keys[currentKeyIdx];
        RotationKey nextKey = keys[currentKeyIdx + 1];

        vec4 start = currentKey.rotation;
        vec4 end = nextKey.rotation;
        float transition = (time - currentKey.timestamp) / (nextKey.timestamp - currentKey.timestamp);
        aiQuaternion aistart = {start.w, start.x, start.y, start.z};
        aiQuaternion aiend = {end.w, end.x, end.y, end.z};
        aiQuaternion airesult;
        aiQuaternion::Interpolate(airesult, aistart, aiend, transition);
        rotation = {airesult.x, airesult.y, airesult.z, airesult.w};
    }

    aiQuaternion aiq = {rotation.w, rotation.x, rotation.y, rotation.z};
    aiMatrix4x4 aimat(aiq.GetMatrix());
    mat4 mat;
    memcpy(&mat[0][0], &aimat[0][0], sizeof(mat));
    return transpose(mat);
}

static mat4 interpolateScale(const std::vector<ScaleKey>& keys, float time)
{
    vec3 scale;

    if(keys.size() == 1)
        scale = keys.front().scale;
    else
    {
        int currentKeyIdx = -1;

        for(int i = 0; i < keys.size() - 1; ++i)
        {
            if(time < keys[i + 1].timestamp)
            {
                currentKeyIdx = i;
                break;
            }
        }

        assert(currentKeyIdx != -1);

        ScaleKey currentKey = keys[currentKeyIdx];
        ScaleKey nextKey = keys[currentKeyIdx + 1];

        vec3 start = currentKey.scale;
        vec3 end = nextKey.scale;
        float transition = (time - currentKey.timestamp) / (nextKey.timestamp - currentKey.timestamp);
        scale = start + transition * (end - start);
    }

    return ::scale(scale);
}

static void updateBones(const Animation& animation, float time, const Bone& bone, mat4 parentTransform, mat4* boneTransformations)
{
    mat4 transform = bone.transform;

    auto it = animation.boneKeys.find(bone.idx);
    if(it != animation.boneKeys.end())
    {
        mat4 scale = interpolateScale(it->second.scaleKeys, time);
        mat4 rotation = interpolateRotation(it->second.rotationKeys, time);
        mat4 translation = interpolateTranslation(it->second.positionKeys, time);

        transform = translation * rotation * scale;
    }

    mat4 globalTransform = parentTransform * transform;

    if(bone.idx < MAX_BONES)
        boneTransformations[bone.idx] = globalTransform * bone.transformFromMeshToBoneSpace;

    for(const Bone& child: bone.children)
        updateBones(animation, time, child, globalTransform, boneTransformations);
}

struct Mesh
{
    BoundingBox bbox;
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

    int idxSkeleton = 0;
    float animationTime = 0.f;
    int currentAnimation = 0;
    std::vector<mat4> boneTransformations;
};

enum
{
    UNIT_DEFAULT,
    UNIT_DIFFUSE,
    UNIT_SPECULAR,
    UNIT_NORMAL,
    UNIT_ALPHA,
    UNIT_SHADOW_MAP,
    UNIT_POSITION,
    UNIT_SSAO,
    UNIT_SSAO_NOISE
};

enum
{
    VIEW_DEPTH,
    VIEW_POSITIONS,
    VIEW_NORMALS,
    VIEW_COLOR_DIFFUSE,
    VIEW_COLOR_SPECULAR,
    VIEW_WIREFRAME,
    VIEW_SHADOWMAP,
    VIEW_SSAO,
    VIEW_FINAL,
    VIEW_COUNT
};

enum
{
    DEBUG_CAMERA_OFF,
    DEBUG_CAMERA,
    DEBUG_CAMERA_WITH_CONTROL
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

static void loadModel(const char* filename, std::vector<Model>& models, Array<Mesh>& meshes,
                      std::vector<Skeleton>& skeletons, Array<Material>& materials,
                      Array<GLuint>& textures, Array<TexId>& texIds);

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
    static Model cameraModel;
    static std::vector<Model> models;
    static std::vector<Model> testModels;
    static Array<Mesh> meshes;
    static std::vector<Skeleton> skeletons;
    static Array<Material> materials;
    static Array<GLuint> textures;
    static Array<TexId> texIds;
    static Camera3d camera;
    static Camera3d cameraDebug;
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
        bool toneMapping = true;
        bool srgbDiffuseTextures = true;
        bool srgbOutput = true;
        bool shadows = true;
        bool ssao = true;
        bool debugUvs = false;
        bool frustumCulling = true;
        int debugCamera = DEBUG_CAMERA_OFF;
        bool testScene;
    } static config;

    struct
    {
        vec3 pos = vec3(0.6f, 2.f, -0.3f) * 3000.f; // light_dir == normalize(pos)
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
        Shader shaderAnim;
    } static gbuffer;

    struct ShadowMap
    {
        enum {SIZE = 4096};
        GLuint depthBuffer;
        GLuint framebuffer;
        Shader shader;
        Shader shaderAnim;
    } static shadowMap;

    struct
    {
        Shader shaderToneMapping;
        Shader shaderLightPass;
        GLuint framebuffer;
        GLuint texture;
    } static hdr;

    struct
    {
        Shader shader;
        GLuint texture;
        GLuint framebuffer;
        GLuint textureNoise;
        float radius = 25.f;
    } static ssao;

    struct
    {
        Shader shader;
        GLuint framebuffer;
        GLuint texture;
    } static ssaoBlur;

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
        skeletons.push_back({});

        loadModel("data/sphere.obj", models, meshes, skeletons, materials, textures, texIds);

        if(models.empty())
        {
            printf("missing data/sphere.obj, run application from top source directory\n");
            assert(false);
        }

        // hack
        sphereModel = models.front();
        models.pop_back();

        loadModel("data/camera.obj", models, meshes, skeletons, materials, textures, texIds);
        cameraModel = models.front();
        models.pop_back();

        loadModel("data/plane.obj", testModels, meshes, skeletons, materials, textures, texIds);
        testModels.back().transform = translate({0.f, -300.f, 0.f}) * scale(vec3(5000.f));
        loadModel("data/cyborg/cyborg.obj", testModels, meshes, skeletons, materials, textures, texIds);
        testModels.back().transform = scale(vec3(50.f));
        loadModel("data/goblin.dae", testModels, meshes, skeletons, materials, textures, texIds);
        {
            // this must not be a reference (pointer invalidation)
            const Model prototype1 = testModels.back();
            const Model prototype2 = *(&testModels.back() - 1);

            for(int i = 0; i < 97; ++i)
            {
                Model model = randomFloat() > 0.5f ? prototype1 : prototype2;

                model.transform =
                        translate(vec3(randomFloat() * 2.f - 1.f, randomFloat() * 0.3f, randomFloat() * 2.f - 1.f) * 2000.f) *
                        rotateY(randomFloat() * 360.f) *
                        rotateX(randomFloat() * 360.f) *
                        model.transform;

                testModels.push_back(model);
            }
        }

        loadModel("data/sponza/sponza.obj", models, meshes, skeletons, materials, textures, texIds);

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
            shadowMap.shaderAnim = createShader("glsl/shadow-anim.vs", "glsl/shadow.fs");

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
            glReadBuffer(GL_NONE); // just to be sure...

            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                    shadowMap.depthBuffer, 0);
        }

        // gbuffer
        {
            gbuffer.shader = createShader("glsl/gbuffer.vs", "glsl/gbuffer.fs");
            gbuffer.shaderAnim = createShader("glsl/anim.vs", "glsl/gbuffer.fs");

            {
                Shader* shaders[] = {&gbuffer.shader, &gbuffer.shaderAnim};

                for(Shader* shader: shaders)
                {
                    shader->bind();
                    shader->uniform1i("samplerDiffuse", UNIT_DIFFUSE);
                    shader->uniform1i("samplerSpecular", UNIT_SPECULAR);
                    shader->uniform1i("samplerNormal", UNIT_NORMAL);
                }
            }

            glGenTextures(1, &gbuffer.depthBuffer);
            glBindTexture(GL_TEXTURE_2D, gbuffer.depthBuffer);
            // to enable preview
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

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
            glReadBuffer(GL_NONE);

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
            hdr.shaderLightPass.uniform1i("samplerSsao", UNIT_SSAO);

            glGenTextures(1, &hdr.texture);
            glBindTexture(GL_TEXTURE_2D, hdr.texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

            glGenFramebuffers(1, &hdr.framebuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, hdr.framebuffer);
            glDrawBuffer(GL_COLOR_ATTACHMENT0);
            glReadBuffer(GL_NONE);

            // needed for forward rendering pass;
            // reuse the depth buffer from gbuffer
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                    GL_TEXTURE_2D, gbuffer.depthBuffer, 0);

            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                    GL_TEXTURE_2D, hdr.texture, 0);
        }

        // ssao
        {
            glGenTextures(1, &ssao.texture);
            glBindTexture(GL_TEXTURE_2D, ssao.texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

            glGenFramebuffers(1, &ssao.framebuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, ssao.framebuffer);
            glDrawBuffer(GL_COLOR_ATTACHMENT0);
            glReadBuffer(GL_NONE);

            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                    GL_TEXTURE_2D, ssao.texture, 0);

            // todo: way to define constants in a shader, in this case number of samples
            vec3 samples[24];

            srand(time(nullptr));

            for(int i = 0; i < getSize(samples);)
            {
                vec3& sample = samples[i];

                sample.x = randomFloat() * 2.f - 1.f;
                sample.y = randomFloat() * 2.f - 1.f;
                sample.z = randomFloat();

                if(length(sample) > 1.f)
                    continue;

                float scale = float(i) / getSize(samples);
                scale = lerp(0.1f, 1.f, scale * scale);
                sample *= scale;
                ++i;
            }

            ssao.shader = createShader("glsl/quad.vs", "glsl/ssao.fs");
            ssao.shader.bind();
            ssao.shader.uniform3fv("samples", getSize(samples), samples);
            ssao.shader.uniform1i("samplerPosition", UNIT_POSITION);
            ssao.shader.uniform1i("samplerNormal", UNIT_NORMAL);
            ssao.shader.uniform1i("samplerNoise", UNIT_SSAO_NOISE);

            glGenTextures(1, &ssao.textureNoise);
            glBindTexture(GL_TEXTURE_2D, ssao.textureNoise);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

            constexpr int size = 4;
            vec3 noise[size * size];

            for(vec3& v: noise)
            {
                v.x = randomFloat() * 2.f - 1.f;
                v.y = randomFloat() * 2.f - 1.f;
                v.z = 0.f;
            }

            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, size, size, 0, GL_RGB, GL_FLOAT, noise);

            ssao.shader.uniform1i("noiseTextureSize", size);
        }

        // ssao blur
        {
            ssaoBlur.shader = createShader("glsl/quad.vs", "glsl/ssao-blur.fs");
            ssaoBlur.shader.bind();
            ssaoBlur.shader.uniform1i("samplerSsao", UNIT_SSAO);

            glGenTextures(1, &ssaoBlur.texture);
            glBindTexture(GL_TEXTURE_2D, ssaoBlur.texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

            glGenFramebuffers(1, &ssaoBlur.framebuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, ssaoBlur.framebuffer);
            glDrawBuffer(GL_COLOR_ATTACHMENT0);
            glReadBuffer(GL_NONE);

            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                    GL_TEXTURE_2D, ssaoBlur.texture, 0);
        }
    }

    if(frame.quit)
    {
        // do the cleanup...
        return;
    }

    for(const WinEvent& e: frame.winEvents)
    {
        if(config.debugCamera == DEBUG_CAMERA_WITH_CONTROL)
            cameraDebug.processEvent(e);
        else
            camera.processEvent(e);
    }

    camera.update(frame.dt);
    cameraDebug.update(frame.dt);

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
        // ssao
        {
            glBindTexture(GL_TEXTURE_2D, ssao.texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, size.x, size.y, 0, GL_RED, GL_UNSIGNED_BYTE,
                    nullptr);
        }
        // ssao blur
        {
            glBindTexture(GL_TEXTURE_2D, ssaoBlur.texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, size.x, size.y, 0, GL_RED, GL_UNSIGNED_BYTE,
                    nullptr);
        }
    }

    struct
    {
        const float fovy = 45.f;
        float aspect;
        const float near = 0.1f;
        const float far = 20000.f;

        mat4 matrix;
    } projection;

    projection.aspect = (float)frame.bufferSize.x / frame.bufferSize.y;
    projection.matrix = perspective(projection.fovy, projection.aspect, projection.near, projection.far);

    const Frustum frustum = createFrustum(camera.pos, camera.up, camera.dir,
                                          projection.fovy, projection.aspect, projection.near, projection.far);

    const Camera3d& activeCamera = config.debugCamera ? cameraDebug : camera;

    mat4 lightSpaceMatrix;
    {
        // this is hardcoded for sponza...
        const float size = 1900.f;

        // if target == up then lookAt() failes; this is a workaround
        const vec3 lpos = light.pos + vec3(0.01f, 0.f, 0.f);

        lightSpaceMatrix = orthographic(-size, size, -size, size, 0.01f, size * 1.5f) *
            lookAt(normalize(lpos) * size, vec3(0.f), vec3(0.f, 1.f, 0.f));
    }

    std::vector<Model>& activeModels = config.testScene ? testModels : models;

    for(Model& model: activeModels)
    {
        if(!model.idxSkeleton)
            continue;

        const Skeleton& skeleton = skeletons[model.idxSkeleton];
        const Animation& animation = skeleton.animations[model.currentAnimation];
        model.animationTime += frame.dt;
        model.animationTime = fmod(model.animationTime, animation.duration);
        updateBones(animation, model.animationTime, skeleton.rootBone, mat4(), model.boneTransformations.data());
    }

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

            Shader* shaders[] = {&shadowMap.shader, &shadowMap.shaderAnim};

            for(Shader* shader: shaders)
            {
                shader->bind();
                shader->uniformMat4("lightSpaceMatrix", lightSpaceMatrix);
            }

            for(const Model& model: activeModels)
            {
                Shader& shader = model.idxSkeleton ? shadowMap.shaderAnim : shadowMap.shader;
                shader.bind();

                if(model.idxSkeleton)
                    shader.uniformMat4v("bones", model.boneTransformations.size(), model.boneTransformations.data());

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
    int numMesh = 0;
    int maxMesh = 0;
    {
        glBindFramebuffer(GL_FRAMEBUFFER, gbuffer.framebuffer);
        glViewport(0, 0, frame.bufferSize.x, frame.bufferSize.y);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);

        Shader* shaders[] = {&gbuffer.shader, &gbuffer.shaderAnim};

        for(Shader* shader: shaders)
        {
            shader->bind();
            shader->uniformMat4("view", activeCamera.view);
            shader->uniformMat4("projection", projection.matrix);
        }

        for(Model& model: activeModels)
        {
            Shader& shader = model.idxSkeleton ? gbuffer.shaderAnim : gbuffer.shader;
            shader.bind();

            if(model.idxSkeleton)
                shader.uniformMat4v("bones", model.boneTransformations.size(), model.boneTransformations.data());

            shader.uniformMat4("model", model.transform);

            for(int i = 0; i < model.meshCount; ++i)
            {
                const Mesh& mesh = meshes[model.idxMesh + i];
                ++maxMesh;

                if(config.frustumCulling && cull(frustum, mesh.bbox, model.transform))
                    continue;

                ++numMesh;

                const Material& material = materials[mesh.idxMaterial];

                shader.uniform3f("colorDiffuse", outputView == VIEW_WIREFRAME ? vec3(1.f) : material.colorDiffuse);
                shader.uniform3f("colorSpecular", material.colorSpecular);
                shader.uniform1i("mapDiffuse", material.idxDiffuse && outputView != VIEW_WIREFRAME);
                shader.uniform1i("mapSpecular", material.idxSpecular);
                shader.uniform1i("mapNormal", material.idxNormal && config.normalMaps);
                shader.uniform1i("alphaTest", material.alphaTest);

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
    }

    // render ssao
    if(config.ssao)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, ssao.framebuffer);
        glViewport(0, 0, frame.bufferSize.x, frame.bufferSize.y);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        ssao.shader.bind();
        ssao.shader.uniformMat4("view", activeCamera.view);
        ssao.shader.uniformMat4("projection", projection.matrix);
        ssao.shader.uniform1f("radius", ssao.radius);
        bindTexture(gbuffer.positions, UNIT_POSITION);
        bindTexture(gbuffer.normals, UNIT_NORMAL);
        bindTexture(ssao.textureNoise, UNIT_SSAO_NOISE);
        glBindVertexArray(quad.vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        glBindFramebuffer(GL_FRAMEBUFFER, ssaoBlur.framebuffer);
        ssaoBlur.shader.bind();
        bindTexture(ssao.texture, UNIT_SSAO);
        glBindVertexArray(quad.vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
    else
    {
        glBindFramebuffer(GL_FRAMEBUFFER, ssaoBlur.framebuffer);
        glClearColor(1.f, 1.f, 1.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        glClearColor(0.f, 0.f, 0.f, 0.f);
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
        hdr.shaderLightPass.uniform3f("light_dir", normalize(light.pos));
        hdr.shaderLightPass.uniform3f("light_color", light.color);
        hdr.shaderLightPass.uniform3f("cameraPos", activeCamera.pos);
        hdr.shaderLightPass.uniform1i("enableAmbient", config.ambient);
        hdr.shaderLightPass.uniform1i("enableDiffuse", config.diffuse);
        hdr.shaderLightPass.uniform1i("enableSpecular", config.specular);

        bindTexture(gbuffer.positions, UNIT_POSITION);
        bindTexture(gbuffer.normals, UNIT_NORMAL);
        bindTexture(gbuffer.colorDiffuse, UNIT_DIFFUSE);
        bindTexture(gbuffer.colorSpecular, UNIT_SPECULAR);
        bindTexture(shadowMap.depthBuffer, UNIT_SHADOW_MAP);
        bindTexture(ssaoBlur.texture, UNIT_SSAO);

        glBindVertexArray(quad.vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // forward rendering; light, sky, camera, frustum planes
        glEnable(GL_DEPTH_TEST);
        shaderPlainColor.bind();
        shaderPlainColor.uniformMat4("view", activeCamera.view);
        shaderPlainColor.uniformMat4("projection", projection.matrix);

        {
            const Mesh& mesh = meshes[sphereModel.idxMesh];
            glBindVertexArray(mesh.vao);

            shaderPlainColor.uniformMat4("model", translate(light.pos) * scale(vec3(light.scale)));
            shaderPlainColor.uniform3f("color", light.color);

            glEnable(GL_CULL_FACE);
            glDrawElements(GL_TRIANGLES, mesh.numIndices, GL_UNSIGNED_INT,
                           reinterpret_cast<const void*>(mesh.indicesOffset));

            if(!config.testScene)
            {
                shaderPlainColor.uniformMat4("model", scale(vec3(10000)));
                shaderPlainColor.uniform3f("color", vec3(0.1f, 0.1f, 1.f));

                glDisable(GL_CULL_FACE); // we are rendering the inside of a sphere
                glDrawElements(GL_TRIANGLES, mesh.numIndices, GL_UNSIGNED_INT,
                               reinterpret_cast<const void*>(mesh.indicesOffset));
            }
        }

        if(config.debugCamera)
        {
            glEnable(GL_CULL_FACE);

            shaderPlainColor.uniformMat4("model", translate(-camera.dir * 100.f) * translate(camera.pos) * scale(vec3(200.f)) *
                                         rotateY(camera.yaw + 180.f) * rotateX(-camera.pitch));

            shaderPlainColor.uniform3f("color", vec3(1.f, 0.f, 0.f));
            const Mesh& mesh = meshes[cameraModel.idxMesh];
            glBindVertexArray(mesh.vao);
            glDrawElements(GL_TRIANGLES, mesh.numIndices, GL_UNSIGNED_INT,
                           reinterpret_cast<const void*>(mesh.indicesOffset));

            // frustum planes
            vec3 vertices[] = {
                camera.pos, frustum.farLeftTop, frustum.farLeftBot,
                camera.pos, frustum.farRightTop, frustum.farRightBot,
                camera.pos, frustum.farLeftBot, frustum.farRightBot,
                camera.pos, frustum.farLeftTop, frustum.farRightTop
            };

            GLuint vao;
            GLuint bo;

            glGenVertexArrays(1, &vao);
            glGenBuffers(1, &bo);

            glBindBuffer(GL_ARRAY_BUFFER, bo);
            glBufferData(GL_ARRAY_BUFFER, sizeof vertices, vertices, GL_STATIC_DRAW);

            glBindVertexArray(vao);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
            glEnableVertexAttribArray(0);

            glDisable(GL_CULL_FACE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
            shaderPlainColor.uniformMat4("model", mat4());
            shaderPlainColor.uniform3f("color", vec3(0.f, 1.f, 0.f));
            glDrawArrays(GL_TRIANGLES, 0, 12);

            glDisable(GL_BLEND);

            for(int i = 0; i < 4; ++i)
                glDrawArrays(GL_LINE_LOOP, i * 3, 3);

            glDepthMask(GL_TRUE);

            glDeleteVertexArrays(1, &vao);
            glDeleteBuffers(1, &bo);
        }
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
        shaderDepth.uniform1i("linearize", true);
        shaderDepth.uniform1f("near", projection.near);
        shaderDepth.uniform1f("far", projection.far);
        texture = gbuffer.depthBuffer;
        break;

    case VIEW_POSITIONS:
        texture = gbuffer.positions;
        break;

    case VIEW_NORMALS:
        texture = gbuffer.normals;
        break;

    case VIEW_COLOR_DIFFUSE:
    case VIEW_WIREFRAME:
        texture = gbuffer.colorDiffuse;
        break;

    case VIEW_COLOR_SPECULAR:
        texture = gbuffer.colorSpecular;
        break;

    case VIEW_SHADOWMAP:
    {
        const int size = min( min(frame.bufferSize.x, frame.bufferSize.y), int(ShadowMap::SIZE) );
        glViewport(0, 0, size, size);
        // if the scene is dynamic then swapping between n and n + 1 frame (double buffering) produces
        // annoying movement, that's why we clear
        glClear(GL_COLOR_BUFFER_BIT);
        shaderDepth.bind();
        shaderDepth.uniform1i("linearize", false);
        texture = shadowMap.depthBuffer;
        break;
    }

    case VIEW_SSAO:
        shaderDepth.bind();
        shaderDepth.uniform1i("linearize", false);
        texture = ssaoBlur.texture;
        break;

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
    ImGui::Checkbox("tone mapping (final image output)", &config.toneMapping);
    ImGui::Checkbox("sRGB diffuse textures", &config.srgbDiffuseTextures);
    ImGui::Checkbox("sRGB output", &config.srgbOutput);
    ImGui::Checkbox("shadows", &config.shadows);
    ImGui::Checkbox("ssao", &config.ssao);
    ImGui::SliderFloat("ssao radius", &ssao.radius, 0.f, 50.f);
    ImGui::Checkbox("debug UV diffuse texture", &config.debugUvs);
    ImGui::Checkbox("frustum culling", &config.frustumCulling);
    ImGui::Checkbox("test scene", &config.testScene);
    ImGui::TextColored({1.f, 0.5f, 0.f, 1.f}, "rendered %d out of %d meshes", numMesh, maxMesh);

    const char* cameraItems[] = {
        "off",
        "on",
        "on with control"
    };

    ImGui::Spacing();
    ImGui::ListBox("frustum debug camera", &config.debugCamera, cameraItems,
                   getSize(cameraItems), getSize(cameraItems) + 1);

    const char* items[VIEW_COUNT] = {
        "depth",
        "world space positions",
        "world space normals",
        "diffuse color",
        "specular color",
        "wireframe",
        "shadowmap",
        "ssao",
        "final image",
    };

    ImGui::Spacing();
    // without + 1 imgui adds unnecessary scrollbar
    ImGui::ListBox("output", &outputView, items, VIEW_COUNT, VIEW_COUNT + 1);
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

struct BoneLoadData
{
    int idx;
    mat4 transformFromMeshToBoneSpace;
};

void createBones(Bone& bone, const aiNode& ainode, const std::map<std::string, BoneLoadData>& boneLoadData)
{
    auto it = boneLoadData.find(ainode.mName.C_Str());

    // if ainode is animated
    if(it != boneLoadData.end())
    {
        bone.idx = it->second.idx;
        bone.transformFromMeshToBoneSpace = it->second.transformFromMeshToBoneSpace;
    }
    else
    {
        assert(sizeof(ainode.mTransformation) == sizeof(bone.transform));
        memcpy(&bone.transform[0][0], &ainode.mTransformation[0][0], sizeof(bone.transform));
        bone.transform = transpose(bone.transform);
    }

    for(int i = 0; i < ainode.mNumChildren; ++i)
    {
        bone.children.push_back({});
        createBones(bone.children.back(), *ainode.mChildren[i], boneLoadData);
    }
}

static void loadModel(const char* filename, std::vector<Model>& models, Array<Mesh>& meshes,
                      std::vector<Skeleton>& skeletons, Array<Material>& materials,
                      Array<GLuint>& textures, Array<TexId>& texIds)
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

    const aiScene* const scene = importer.ReadFile(filename, aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
                                                   aiProcess_CalcTangentSpace | aiProcess_LimitBoneWeights);

    if(!scene)
    {
        log("Assimp::Importer::ReadFile() failed, %s", filename);
        return;
    }

    assert(scene->HasMeshes());

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

    Model model;

    std::map<std::string, BoneLoadData> boneLoadData;

    if(scene->HasAnimations())
    {
        model.idxSkeleton = skeletons.size();
        skeletons.push_back({});
        Skeleton& skeleton = skeletons.back();

        int boneCount = 0;
        for(unsigned idxMesh = 0; idxMesh < scene->mNumMeshes; ++idxMesh)
        {
            const aiMesh& aimesh = *scene->mMeshes[idxMesh];

            for(int idxBone = 0; idxBone < aimesh.mNumBones; ++idxBone)
            {
                aiBone& aiBone = *aimesh.mBones[idxBone];

                if(boneLoadData.find(aiBone.mName.C_Str()) == boneLoadData.end())
                {
                    assert(boneCount < MAX_BONES);
                    boneLoadData[aiBone.mName.C_Str()].idx = boneCount;
                    assert(sizeof(mat4) == sizeof(aiMatrix4x4));
                    mat4& dest = boneLoadData[aiBone.mName.C_Str()].transformFromMeshToBoneSpace;
                    memcpy(&dest[0][0], &aiBone.mOffsetMatrix[0][0], sizeof(mat4));
                    dest = transpose(dest); // aiMatrix4x4 is row-major
                    ++boneCount;
                }
            }
        }

        model.boneTransformations.resize(boneCount);

        createBones(skeleton.rootBone, *(scene->mRootNode), boneLoadData);

        for(unsigned idxAnim = 0; idxAnim < scene->mNumAnimations; ++idxAnim)
        {
            aiAnimation& aianimation = *(scene->mAnimations[idxAnim]);

            Animation animation;
            animation.duration = aianimation.mDuration * 1.f / (aianimation.mTicksPerSecond ? aianimation.mTicksPerSecond : 1.f);
            animation.name = aianimation.mName.C_Str();

            for(unsigned idxChannel = 0; idxChannel < aianimation.mNumChannels; ++idxChannel)
            {
                const aiNodeAnim& ainodeanim = *(aianimation.mChannels[idxChannel]);

                // todo: animation node with no corresponding bones?
                if(boneLoadData.find(ainodeanim.mNodeName.C_Str()) == boneLoadData.end())
                    continue;

                BoneKeys boneKeys;

                for(unsigned idxKey = 0; idxKey < ainodeanim.mNumPositionKeys; ++idxKey)
                {
                    PositionKey key;
                    key.timestamp = ainodeanim.mPositionKeys[idxKey].mTime;
                    aiVector3D aipos = ainodeanim.mPositionKeys[idxKey].mValue;
                    key.position = {aipos.x, aipos.y, aipos.z};
                    boneKeys.positionKeys.push_back(key);
                }

                for(unsigned idxKey = 0; idxKey < ainodeanim.mNumRotationKeys; ++idxKey)
                {
                    RotationKey key;
                    key.timestamp = ainodeanim.mRotationKeys[idxKey].mTime;
                    aiQuaternion airotation = ainodeanim.mRotationKeys[idxKey].mValue;
                    key.rotation = {airotation.x, airotation.y, airotation.z, airotation.w};
                    boneKeys.rotationKeys.push_back(key);
                }

                for(unsigned idxKey = 0; idxKey < ainodeanim.mNumScalingKeys; ++idxKey)
                {
                    ScaleKey key;
                    key.timestamp = ainodeanim.mScalingKeys[idxKey].mTime;
                    aiVector3D aiscale = ainodeanim.mScalingKeys[idxKey].mValue;
                    key.scale = {aiscale.x, aiscale.y, aiscale.z};
                    boneKeys.scaleKeys.push_back(key);
                }

                int id = boneLoadData.at(ainodeanim.mNodeName.C_Str()).idx;
                animation.boneKeys[id] = std::move(boneKeys);
            }

            skeleton.animations.push_back(std::move(animation));
        }
    }

    model.idxMesh = meshes.size();

    Array<float> vertexData;
    Array<unsigned> indices;

    for(unsigned idxMesh = 0; idxMesh < scene->mNumMeshes; ++idxMesh)
    {
        const aiMesh& aimesh = *scene->mMeshes[idxMesh];

        assert(aimesh.HasFaces());
        assert(aimesh.mFaces[0].mNumIndices == 3);
        assert(aimesh.HasPositions());

        const bool hasTexCoords = aimesh.HasTextureCoords(0);
        const bool hasNormals = aimesh.HasNormals();
        const bool hasTangents = aimesh.HasTangentsAndBitangents();
        const bool hasBones = aimesh.HasBones();

        const int floatsPerVertex = 3 + hasTexCoords * 2 + hasNormals * 3
            + hasTangents * 6 + hasBones * 8;

        vertexData.clear();
        vertexData.reserve(aimesh.mNumVertices * floatsPerVertex);

        float xmin = 0.f, xmax = 0.f, ymin = 0.f, ymax = 0.f, zmin = 0.f, zmax = 0.f;

        for(unsigned idxVert = 0; idxVert < aimesh.mNumVertices; ++idxVert)
        {
            const aiVector3D v = aimesh.mVertices[idxVert];

            {
                aiVector3D tv = v;

                // this should fix bounding boxes for animated models
                if(hasBones)
                    tv = scene->mRootNode->mTransformation * v;

                xmin = min(xmin, tv.x);
                xmax = max(xmax, tv.x);

                ymin = min(ymin, tv.y);
                ymax = max(ymax, tv.y);

                zmin = min(zmin, tv.z);
                zmax = max(zmax, tv.z);
            }

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

            if(hasBones)
            {
                int count = 0;
                int bonesIdx[MAX_WEIGHTS];
                float weights[MAX_WEIGHTS] = {};

                for(int idxBone = 0; idxBone < aimesh.mNumBones; ++idxBone)
                {
                    aiBone& aiBone = *aimesh.mBones[idxBone];

                    for(int idxWeight = 0; idxWeight < aiBone.mNumWeights; ++idxWeight)
                    {

                        aiVertexWeight& aivw = aiBone.mWeights[idxWeight];

                        if(aivw.mVertexId == idxVert)
                        {
                            assert(count < MAX_WEIGHTS);
                            bonesIdx[count] = boneLoadData.at(aiBone.mName.C_Str()).idx;
                            weights[count] = aivw.mWeight;
                            ++count;
                            break;
                        }
                    }
                }

                assert(sizeof(int) == sizeof(float));

                for(int idx: bonesIdx)
                {
                    vertexData.pushBack({});
                    int* back = (int*)&vertexData.back();
                    *back = idx;
                }

                for(float weight: weights)
                    vertexData.pushBack(weight);
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

        mesh.bbox = {{ {xmin, ymin, zmin}, {xmax, ymin, zmin}, {xmin, ymax, zmin}, {xmax, ymax, zmin},
                       {xmin, ymin, zmax}, {xmax, ymin, zmax}, {xmin, ymax, zmax}, {xmax, ymax, zmax} }};

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

        if(hasBones)
        {
            glVertexAttribIPointer( 5, 4, GL_INT, stride,
                    reinterpret_cast<const void*>(offset) );

            glEnableVertexAttribArray(5);
            offset += 4 * sizeof(float);

            glVertexAttribPointer( 6, 4, GL_FLOAT, GL_FALSE, stride,
                    reinterpret_cast<const void*>(offset) );

            glEnableVertexAttribArray(6);
            offset += 4 * sizeof(float);
        }

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.bo);
    }

    models.push_back(model);
}
