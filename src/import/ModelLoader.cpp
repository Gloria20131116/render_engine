#include "import/ModelLoader.h"

#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <assimp/Importer.hpp>
#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <unordered_map>
#include <utility>

#include "core/Log.h"
#include "render/Texture.h"
#include "scene/Material.h"
#include "scene/Mesh.h"
#include "scene/Node.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

// Assimp's DefaultIOSystem on Windows expects UTF-8 paths and converts to wchar_t.
// Normalize whatever we got (UTF-8 or the active ANSI code page) into UTF-8.
std::string toUtf8Path(const std::string& path) {
#ifdef _WIN32
    if (path.empty()) return path;
    // Already valid UTF-8?
    int need = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.c_str(), -1, nullptr, 0);
    if (need > 0) return path;
    // Fallback: treat as the system ANSI code page (what GetOpenFileNameA returns).
    need = MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, nullptr, 0);
    if (need <= 0) return path;
    std::wstring wide(need, L'\0');
    MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, wide.data(), need);
    while (!wide.empty() && wide.back() == L'\0') wide.pop_back();
    int out = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), nullptr, 0, nullptr,
                                  nullptr);
    if (out <= 0) return path;
    std::string utf8(out, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), utf8.data(), out, nullptr,
                        nullptr);
    return utf8;
#else
    return path;
#endif
}

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
        // Assimp may store "path\\tex.png", absolute paths, or "./tex.png".
        std::filesystem::path raw(key);
        std::filesystem::path p = raw.is_absolute() ? raw : (ctx.dir / raw);
        if (!std::filesystem::exists(p)) p = ctx.dir / raw.filename();
        if (!std::filesystem::exists(p)) {
            // Also try replacing backslashes / stripping leading "./"
            std::string cleaned = key;
            for (char& c : cleaned)
                if (c == '\\') c = '/';
            while (cleaned.rfind("./", 0) == 0) cleaned.erase(0, 2);
            p = ctx.dir / cleaned;
            if (!std::filesystem::exists(p)) p = ctx.dir / std::filesystem::path(cleaned).filename();
        }
        if (std::filesystem::exists(p)) tex = Texture::load2D(p.string(), srgb);
        else Log::warn("Texture not found: %s (looked next to %s)", key.c_str(), ctx.dir.string().c_str());
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
    if (idx.empty()) {
        Log::warn("Mesh '%s' has no triangles after conversion (%u faces)", src->mName.C_Str(),
                  src->mNumFaces);
    }

    // Left-handed exports (e.g. PMX -> OBJ) often store clockwise winding
    // while the authored normals point outward. With CCW front faces that
    // culls the real front surface and lights the model inside-out. Detect
    // the mismatch (winding-derived normal vs authored normal) and reverse
    // the winding — the normals themselves are correct, so don't touch them.
    if (src->HasNormals() && !idx.empty()) {
        size_t opposed = 0, total = 0;
        for (size_t i = 0; i + 2 < idx.size(); i += 3) {
            const glm::vec3& p0 = verts[idx[i]].position;
            const glm::vec3& p1 = verts[idx[i + 1]].position;
            const glm::vec3& p2 = verts[idx[i + 2]].position;
            glm::vec3 geoN = glm::cross(p1 - p0, p2 - p0);
            glm::vec3 shadeN =
                verts[idx[i]].normal + verts[idx[i + 1]].normal + verts[idx[i + 2]].normal;
            float d = glm::dot(geoN, shadeN);
            if (d < 0.0f) opposed++;
            if (d != 0.0f) total++;
        }
        if (total > 0 && opposed * 2 > total) {
            for (size_t i = 0; i + 2 < idx.size(); i += 3) std::swap(idx[i + 1], idx[i + 2]);
            Log::info("Mesh '%s': reversed triangle winding (%zu/%zu faces opposed the normals)",
                      src->mName.C_Str(), opposed, total);
        }
    }

    return std::make_shared<Mesh>(verts, idx, src->mName.C_Str());
}

glm::mat4 aiToGlm(const aiMatrix4x4& m) {
    // Assimp is row-major; glm is column-major — transpose on construction.
    return glm::transpose(glm::mat4(m.a1, m.a2, m.a3, m.a4, m.b1, m.b2, m.b3, m.b4, m.c1, m.c2,
                                    m.c3, m.c4, m.d1, m.d2, m.d3, m.d4));
}

void applyMatrixToNode(Node& node, const glm::mat4& local) {
    // Decompose into TRS for the editor. Keep full matrix semantics via position/rot/scale.
    node.position = glm::vec3(local[3]);
    glm::vec3 scale(glm::length(glm::vec3(local[0])), glm::length(glm::vec3(local[1])),
                    glm::length(glm::vec3(local[2])));
    // Guard against zero scale (singular matrices from some FBX exporters).
    if (scale.x < 1e-8f) scale.x = 1e-8f;
    if (scale.y < 1e-8f) scale.y = 1e-8f;
    if (scale.z < 1e-8f) scale.z = 1e-8f;
    node.scale = scale;

    glm::mat3 rotMat(glm::vec3(local[0]) / scale.x, glm::vec3(local[1]) / scale.y,
                     glm::vec3(local[2]) / scale.z);
    // Handle negative determinant (reflection) by flipping X scale.
    if (glm::determinant(rotMat) < 0.0f) {
        node.scale.x *= -1.0f;
        rotMat[0] = -rotMat[0];
    }
    glm::mat4 r(rotMat);
    glm::vec3 euler;
    glm::extractEulerAngleYXZ(r, euler.y, euler.x, euler.z);
    node.rotationEuler = glm::degrees(euler);
}

std::shared_ptr<Node> convertNode(LoadContext& ctx, const aiNode* src) {
    auto node = std::make_shared<Node>(src->mName.length ? src->mName.C_Str() : "Node");
    applyMatrixToNode(*node, aiToGlm(src->mTransformation));

    for (unsigned i = 0; i < src->mNumMeshes; ++i) {
        unsigned meshIndex = src->mMeshes[i];
        if (meshIndex >= ctx.meshes.size()) continue;
        const aiMesh* m = ctx.scene->mMeshes[meshIndex];
        auto child = node->addChild(m->mName.length ? m->mName.C_Str() : "Mesh");
        child->mesh = ctx.meshes[meshIndex];
        unsigned matIndex = m->mMaterialIndex;
        child->material = matIndex < ctx.materials.size() ? ctx.materials[matIndex]
                                                         : std::make_shared<Material>();
        // Skip empty meshes so Hierarchy isn't littered with invisible nodes.
        if (child->mesh->indexCount() == 0) {
            node->children.pop_back();
        }
    }
    for (unsigned i = 0; i < src->mNumChildren; ++i)
        node->addChild(convertNode(ctx, src->mChildren[i]));
    return node;
}

void accumulateBounds(const Node& node, const glm::mat4& parent, glm::vec3& bmin, glm::vec3& bmax,
                      bool& any) {
    glm::mat4 world = parent * node.localMatrix();
    if (node.mesh && node.mesh->indexCount() > 0) {
        // Approximate with origin of the mesh (exact AABB would need vertex access).
        glm::vec3 p = glm::vec3(world[3]);
        if (!any) {
            bmin = bmax = p;
            any = true;
        } else {
            bmin = glm::min(bmin, p);
            bmax = glm::max(bmax, p);
        }
    }
    for (const auto& c : node.children) accumulateBounds(*c, world, bmin, bmax, any);
}

}  // namespace

std::shared_ptr<Node> ModelLoader::load(const std::string& path) {
    const std::string utf8Path = toUtf8Path(path);
    Log::info("Importing model: %s", utf8Path.c_str());

    Assimp::Importer importer;
    // PreTransformVertices bakes the FBX/OBJ node graph into mesh vertices so
    // lookDev import doesn't depend on lossy Euler decomposition of pivots.
    // Keep the hierarchy readable by NOT using PreTransformVertices — instead we
    // convert aiMatrix4x4 carefully. For stubborn FBX pivots we still triangulate
    // and drop lines/points.
    const unsigned flags =
        aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace |
        aiProcess_JoinIdenticalVertices | aiProcess_ImproveCacheLocality |
        aiProcess_SortByPType | aiProcess_FindInvalidData | aiProcess_GenUVCoords |
        aiProcess_TransformUVCoords | aiProcess_FlipUVs | aiProcess_EmbedTextures |
        aiProcess_GlobalScale;  // applies FBX UnitScaleFactor (cm -> m etc.)

    const aiScene* scene = importer.ReadFile(utf8Path, flags);
    if (!scene || !scene->mRootNode) {
        Log::error("Model import failed: %s (%s)", utf8Path.c_str(), importer.GetErrorString());
        return nullptr;
    }
    // Assimp sometimes sets INCOMPLETE (e.g. missing anim bones) even when meshes are fine.
    if (scene->mNumMeshes == 0) {
        Log::error("Model import produced 0 meshes: %s (%s)%s", utf8Path.c_str(),
                   importer.GetErrorString(),
                   (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) ? " [INCOMPLETE]" : "");
        return nullptr;
    }
    if (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)
        Log::warn("Assimp marked scene incomplete, importing meshes anyway: %s", utf8Path.c_str());

    LoadContext ctx;
    ctx.scene = scene;
    ctx.dir = std::filesystem::path(utf8Path).parent_path();

    ctx.materials.reserve(scene->mNumMaterials);
    for (unsigned i = 0; i < scene->mNumMaterials; ++i)
        ctx.materials.push_back(convertMaterial(ctx, scene->mMaterials[i]));
    if (ctx.materials.empty()) ctx.materials.push_back(std::make_shared<Material>());

    ctx.meshes.reserve(scene->mNumMeshes);
    for (unsigned i = 0; i < scene->mNumMeshes; ++i)
        ctx.meshes.push_back(convertMesh(scene->mMeshes[i]));

    auto root = convertNode(ctx, scene->mRootNode);
    root->name = std::filesystem::path(utf8Path).stem().string();

    unsigned tris = 0, keptMeshes = 0;
    for (unsigned i = 0; i < scene->mNumMeshes; ++i) {
        tris += ctx.meshes[i]->indexCount() / 3;
        if (ctx.meshes[i]->indexCount() > 0) keptMeshes++;
    }
    if (keptMeshes == 0) {
        Log::error("Model '%s' had meshes but no triangles after conversion", root->name.c_str());
        return nullptr;
    }

    Log::info("Imported '%s': %u meshes (%u with tris), %u materials, %u triangles",
              root->name.c_str(), scene->mNumMeshes, keptMeshes, scene->mNumMaterials, tris);
    return root;
}
