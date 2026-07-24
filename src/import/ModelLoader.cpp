#include "import/ModelLoader.h"

#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <assimp/Importer.hpp>
#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <unordered_map>

#include "core/Log.h"
#include "render/Texture.h"
#include "scene/Material.h"
#include "scene/Mesh.h"
#include "scene/Node.h"

namespace {

struct LoadContext {
    const aiScene* scene = nullptr;
    std::filesystem::path dir;
    std::unordered_map<std::string, std::shared_ptr<Texture>> textureCache;
    std::vector<std::shared_ptr<Material>> materials;
    std::vector<std::shared_ptr<Mesh>> meshes;
};

std::shared_ptr<Texture> loadTexture(LoadContext& ctx, const aiString& aiPath, bool srgb) {
    std::string key = aiPath.C_Str();
    if (key.empty()) return nullptr;
    auto it = ctx.textureCache.find(key);
    if (it != ctx.textureCache.end()) return it->second;

    std::shared_ptr<Texture> tex;
    if (const aiTexture* embedded = ctx.scene->GetEmbeddedTexture(key.c_str())) {
        if (embedded->mHeight == 0) {
            tex = Texture::loadFromMemory(reinterpret_cast<unsigned char*>(embedded->pcData),
                                          (int)embedded->mWidth, srgb, key);
        } else {
            Log::warn("Uncompressed embedded texture '%s' not supported", key.c_str());
        }
    } else {
        std::filesystem::path p = ctx.dir / key;
        if (!std::filesystem::exists(p)) {
            // Many exports store absolute paths; retry with just the filename.
            p = ctx.dir / std::filesystem::path(key).filename();
        }
        if (std::filesystem::exists(p)) tex = Texture::load2D(p.string(), srgb);
        else Log::warn("Texture not found: %s", key.c_str());
    }
    ctx.textureCache[key] = tex;
    return tex;
}

std::shared_ptr<Material> convertMaterial(LoadContext& ctx, const aiMaterial* src) {
    auto mat = std::make_shared<Material>();
    aiString name;
    if (src->Get(AI_MATKEY_NAME, name) == AI_SUCCESS) mat->name = name.C_Str();

    aiColor3D color;
    if (src->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS)
        mat->baseColor = {color.r, color.g, color.b};
    if (src->Get(AI_MATKEY_BASE_COLOR, color) == AI_SUCCESS)
        mat->baseColor = {color.r, color.g, color.b};

    float f;
    if (src->Get(AI_MATKEY_METALLIC_FACTOR, f) == AI_SUCCESS) mat->metallic = f;
    if (src->Get(AI_MATKEY_ROUGHNESS_FACTOR, f) == AI_SUCCESS) mat->roughness = f;
    if (src->Get(AI_MATKEY_COLOR_EMISSIVE, color) == AI_SUCCESS)
        mat->emissive = {color.r, color.g, color.b};

    int twoSided = 0;
    if (src->Get(AI_MATKEY_TWOSIDED, twoSided) == AI_SUCCESS) mat->doubleSided = twoSided != 0;

    aiString path;
    if (src->GetTexture(aiTextureType_BASE_COLOR, 0, &path) == AI_SUCCESS ||
        src->GetTexture(aiTextureType_DIFFUSE, 0, &path) == AI_SUCCESS)
        mat->albedoMap = loadTexture(ctx, path, true);
    if (src->GetTexture(aiTextureType_NORMALS, 0, &path) == AI_SUCCESS ||
        src->GetTexture(aiTextureType_HEIGHT, 0, &path) == AI_SUCCESS)
        mat->normalMap = loadTexture(ctx, path, false);
    if (src->GetTexture(aiTextureType_METALNESS, 0, &path) == AI_SUCCESS)
        mat->metallicMap = loadTexture(ctx, path, false);
    if (src->GetTexture(aiTextureType_DIFFUSE_ROUGHNESS, 0, &path) == AI_SUCCESS)
        mat->roughnessMap = loadTexture(ctx, path, false);
    if (src->GetTexture(aiTextureType_AMBIENT_OCCLUSION, 0, &path) == AI_SUCCESS ||
        src->GetTexture(aiTextureType_LIGHTMAP, 0, &path) == AI_SUCCESS)
        mat->aoMap = loadTexture(ctx, path, false);
    if (src->GetTexture(aiTextureType_EMISSIVE, 0, &path) == AI_SUCCESS)
        mat->emissiveMap = loadTexture(ctx, path, true);

    return mat;
}

std::shared_ptr<Mesh> convertMesh(const aiMesh* src) {
    std::vector<Vertex> verts(src->mNumVertices);
    for (unsigned i = 0; i < src->mNumVertices; ++i) {
        Vertex& v = verts[i];
        v.position = {src->mVertices[i].x, src->mVertices[i].y, src->mVertices[i].z};
        if (src->HasNormals())
            v.normal = {src->mNormals[i].x, src->mNormals[i].y, src->mNormals[i].z};
        if (src->HasTextureCoords(0))
            v.uv = {src->mTextureCoords[0][i].x, src->mTextureCoords[0][i].y};
        if (src->HasTangentsAndBitangents()) {
            glm::vec3 t = {src->mTangents[i].x, src->mTangents[i].y, src->mTangents[i].z};
            glm::vec3 b = {src->mBitangents[i].x, src->mBitangents[i].y, src->mBitangents[i].z};
            glm::vec3 n = verts[i].normal;
            float w = glm::dot(glm::cross(n, t), b) < 0.0f ? -1.0f : 1.0f;
            v.tangent = glm::vec4(t, w);
        }
    }
    std::vector<uint32_t> idx;
    idx.reserve(src->mNumFaces * 3);
    for (unsigned i = 0; i < src->mNumFaces; ++i) {
        const aiFace& face = src->mFaces[i];
        if (face.mNumIndices != 3) continue;
        idx.insert(idx.end(), {face.mIndices[0], face.mIndices[1], face.mIndices[2]});
    }
    return std::make_shared<Mesh>(verts, idx, src->mName.C_Str());
}

std::shared_ptr<Node> convertNode(LoadContext& ctx, const aiNode* src) {
    auto node = std::make_shared<Node>(src->mName.length ? src->mName.C_Str() : "Node");

    aiVector3D pos, scale, rotAxis;
    ai_real angle;
    src->mTransformation.Decompose(scale, rotAxis, angle, pos);
    node->position = {pos.x, pos.y, pos.z};
    node->scale = {scale.x, scale.y, scale.z};
    // Convert axis-angle to euler via glm
    if (std::abs(angle) > 1e-6f) {
        glm::mat4 r = glm::rotate(glm::mat4(1.0f), (float)angle,
                                  glm::normalize(glm::vec3(rotAxis.x, rotAxis.y, rotAxis.z)));
        glm::vec3 euler;
        glm::extractEulerAngleYXZ(r, euler.y, euler.x, euler.z);
        node->rotationEuler = glm::degrees(euler);
    }

    for (unsigned i = 0; i < src->mNumMeshes; ++i) {
        const aiMesh* m = ctx.scene->mMeshes[src->mMeshes[i]];
        auto child = node->addChild(m->mName.length ? m->mName.C_Str() : "Mesh");
        child->mesh = ctx.meshes[src->mMeshes[i]];
        child->material = ctx.materials[m->mMaterialIndex];
    }
    for (unsigned i = 0; i < src->mNumChildren; ++i)
        node->addChild(convertNode(ctx, src->mChildren[i]));
    return node;
}

}  // namespace

std::shared_ptr<Node> ModelLoader::load(const std::string& path) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        path, aiProcess_Triangulate | aiProcess_CalcTangentSpace | aiProcess_GenSmoothNormals |
                  aiProcess_JoinIdenticalVertices | aiProcess_LimitBoneWeights |
                  aiProcess_SortByPType | aiProcess_FlipUVs | aiProcess_GlobalScale);
    if (!scene || !scene->mRootNode || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)) {
        Log::error("Model import failed: %s (%s)", path.c_str(), importer.GetErrorString());
        return nullptr;
    }

    LoadContext ctx;
    ctx.scene = scene;
    ctx.dir = std::filesystem::path(path).parent_path();

    ctx.materials.reserve(scene->mNumMaterials);
    for (unsigned i = 0; i < scene->mNumMaterials; ++i)
        ctx.materials.push_back(convertMaterial(ctx, scene->mMaterials[i]));

    ctx.meshes.reserve(scene->mNumMeshes);
    for (unsigned i = 0; i < scene->mNumMeshes; ++i)
        ctx.meshes.push_back(convertMesh(scene->mMeshes[i]));

    auto root = convertNode(ctx, scene->mRootNode);
    root->name = std::filesystem::path(path).stem().string();

    unsigned tris = 0;
    for (unsigned i = 0; i < scene->mNumMeshes; ++i) tris += scene->mMeshes[i]->mNumFaces;
    Log::info("Imported '%s': %u meshes, %u materials, %u triangles", root->name.c_str(),
              scene->mNumMeshes, scene->mNumMaterials, tris);
    return root;
}
