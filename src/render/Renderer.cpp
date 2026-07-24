#include "render/Renderer.h"

#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <string>

#include "render/RenderUtil.h"
#include "render/ShaderLibrary.h"
#include "render/Texture.h"
#include "scene/Material.h"
#include "scene/Mesh.h"
#include "scene/Scene.h"

void Renderer::init(ShaderLibrary& shaders) {
    shaders_ = &shaders;

    shaders.load("pbr", "pbr.vert", "pbr.frag");
    shaders.load("shadow_depth", "shadow_depth.vert", "shadow_depth.frag");
    shaders.load("outline", "outline.vert", "outline.frag");
    shaders.load("skybox", "skybox.vert", "skybox.frag");
    shaders.load("tonemap", "fullscreen.vert", "tonemap.frag");
    shaders.load("debug_view", "fullscreen.vert", "debug_view.frag");

    shadowFbo_.create(settings.shadowMapSize, settings.shadowMapSize, {}, true, "Shadow Map");
    sceneFbo_.create(vpWidth_, vpHeight_, {{GL_RGBA16F, "Scene Color (HDR)"}}, true, "Scene");
    finalFbo_.create(vpWidth_, vpHeight_, {{GL_RGBA8, "Final (LDR)"}}, false, "Final");
    inspectFbo_.create(512, 512, {{GL_RGBA8, "Inspector"}}, false, "Inspector");

    ibl_.init(shaders);
    bloom_.init(shaders);
    bloom_.resize(vpWidth_, vpHeight_);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
}

void Renderer::resizeViewport(int w, int h) {
    if (w < 8 || h < 8) return;
    vpWidth_ = w;
    vpHeight_ = h;
    sceneFbo_.resize(w, h);
    finalFbo_.resize(w, h);
    bloom_.resize(w, h);
}

glm::mat4 Renderer::sunLightMatrix(const Scene& scene) const {
    float s = scene.sun.shadowOrthoSize;
    glm::vec3 dir = scene.sun.direction();
    glm::vec3 center = scene.camera.target;
    glm::vec3 eye = center - dir * s * 2.0f;
    glm::vec3 up = std::abs(dir.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
    glm::mat4 view = glm::lookAt(eye, center, up);
    glm::mat4 proj = glm::ortho(-s, s, -s, s, 0.05f, s * 6.0f);
    return proj * view;
}

void Renderer::render(Scene& scene) {
    if (scene.environment.dirty) {
        ibl_.bake(scene.environment);
        scene.environment.dirty = false;
    }

    view_ = scene.camera.view();
    proj_ = scene.camera.projection((float)vpWidth_ / (float)vpHeight_);
    cameraPos_ = scene.camera.position();
    lightMatrix_ = sunLightMatrix(scene);

    debugger_.beginFrame();

    // Report baked IBL resources as a virtual pass so they show up in the debugger.
    {
        PassRecord& rec = debugger_.beginPass("IBL (baked)",
                                              "Environment capture, irradiance & prefiltered "
                                              "specular convolutions, BRDF integration LUT. "
                                              "Re-baked when the environment changes.");
        if (ibl_.sourceEquirect())
            rec.inputs.push_back({"Equirect HDR", ibl_.sourceEquirect()->id(), GL_TEXTURE_2D,
                                  ibl_.sourceEquirect()->width(), ibl_.sourceEquirect()->height()});
        auto addCube = [&rec](const std::shared_ptr<Texture>& t) {
            if (t) rec.outputs.push_back({t->name(), t->id(), GL_TEXTURE_CUBE_MAP, t->width(), t->height()});
        };
        addCube(ibl_.environment());
        addCube(ibl_.irradiance());
        addCube(ibl_.prefiltered());
        if (ibl_.brdfLUT())
            rec.outputs.push_back({"BRDF LUT", ibl_.brdfLUT()->id(), GL_TEXTURE_2D,
                                   ibl_.brdfLUT()->width(), ibl_.brdfLUT()->height()});
        debugger_.endPass("IBL (baked)");
    }

    shadowPass(scene);
    mainPass(scene);
    skyboxPass(scene);
    bloom_.render(sceneFbo_.colorTex(), debugger_);
    tonemapPass();

    Framebuffer::unbind();
}

void Renderer::shadowPass(Scene& scene) {
    PassRecord& rec = debugger_.beginPass("Shadow Map",
                                          "Sun directional shadow, depth-only ortho render");

    shadowFbo_.bind();
    glClear(GL_DEPTH_BUFFER_BIT);

    if (scene.sun.castShadows) {
        auto sh = shaders_->get("shadow_depth");
        sh->bind();
        sh->setMat4("uLightMatrix", lightMatrix_);
        glEnable(GL_DEPTH_TEST);
        glCullFace(GL_FRONT);  // reduce peter-panning
        glEnable(GL_CULL_FACE);
        scene.root->traverse([&](Node& node, const glm::mat4& world) {
            if (!node.mesh) return;
            sh->setMat4("uModel", world);
            node.mesh->draw();
            rec.drawCalls++;
        });
        glCullFace(GL_BACK);
    }

    rec.outputs.push_back({"Shadow Depth", shadowFbo_.depthTex(), GL_TEXTURE_2D,
                           shadowFbo_.width(), shadowFbo_.height(), true, true});
    debugger_.endPass("Shadow Map");
}

void Renderer::bindMaterial(Shader& sh, const Material& mat) {
    sh.setInt("uShadingModel", (int)mat.model);
    sh.setVec3("uBaseColor", mat.baseColor);
    sh.setFloat("uMetallic", mat.metallic);
    sh.setFloat("uRoughness", mat.roughness);
    sh.setFloat("uAO", mat.ao);
    sh.setVec3("uEmissive", mat.emissive * mat.emissiveIntensity);
    sh.setFloat("uNormalStrength", mat.normalStrength);
    sh.setFloat("uSpecularF0", mat.specularF0);
    sh.setFloat("uIBLIntensity", mat.iblIntensity);
    sh.setFloat("uAlphaCutoff", mat.alphaCutoff);

    sh.setInt("uNDFType", (int)mat.ndf);
    sh.setInt("uGeomType", (int)mat.geom);
    sh.setInt("uFresnelType", (int)mat.fresnel);
    sh.setFloat("uSpecularTint", mat.specularTint);
    sh.setFloat("uEnergyCompensation", mat.energyCompensation);

    sh.setVec3("uShadowColor", mat.shadowColor);
    sh.setFloat("uShadowThreshold", mat.shadowThreshold);
    sh.setFloat("uShadowSoftness", mat.shadowSoftness);
    sh.setFloat("uRampShift", mat.rampShift);
    sh.setVec3("uRimColor", mat.rimColor);
    sh.setFloat("uRimWidth", mat.rimWidth);
    sh.setFloat("uRimIntensity", mat.rimIntensity);
    sh.setFloat("uToonSpecSize", mat.toonSpecSize);
    sh.setFloat("uToonSpecIntensity", mat.toonSpecIntensity);
    sh.setVec3("uToonSpecColor", mat.toonSpecColor);

    auto bindTex = [&sh](const char* flag, const char* sampler, int unit,
                         const std::shared_ptr<Texture>& tex) {
        sh.setInt(flag, tex ? 1 : 0);
        sh.setInt(sampler, unit);
        if (tex) tex->bind(unit);
    };
    bindTex("uHasAlbedoMap", "uAlbedoMap", 0, mat.albedoMap);
    bindTex("uHasNormalMap", "uNormalMap", 1, mat.normalMap);
    bindTex("uHasMetallicMap", "uMetallicMap", 2, mat.metallicMap);
    bindTex("uHasRoughnessMap", "uRoughnessMap", 3, mat.roughnessMap);
    bindTex("uHasAOMap", "uAOMap", 4, mat.aoMap);
    bindTex("uHasEmissiveMap", "uEmissiveMap", 5, mat.emissiveMap);
    bindTex("uHasRampMap", "uRampMap", 6, mat.rampMap);

    if (mat.doubleSided) glDisable(GL_CULL_FACE);
    else glEnable(GL_CULL_FACE);
}

void Renderer::mainPass(Scene& scene) {
    PassRecord& rec = debugger_.beginPass(
        "Main (PBR/Toon)",
        "Forward lighting: Cook-Torrance direct + IBL ambient, or NPR toon shading. "
        "Inputs: shadow map + IBL maps. Followed by inverted-hull outlines.");

    rec.inputs.push_back({"Shadow Depth", shadowFbo_.depthTex(), GL_TEXTURE_2D,
                          shadowFbo_.width(), shadowFbo_.height(), true, true});
    if (ibl_.irradiance())
        rec.inputs.push_back({"IBL Irradiance", ibl_.irradiance()->id(), GL_TEXTURE_CUBE_MAP,
                              ibl_.irradiance()->width(), ibl_.irradiance()->height()});
    if (ibl_.prefiltered())
        rec.inputs.push_back({"IBL Prefiltered", ibl_.prefiltered()->id(), GL_TEXTURE_CUBE_MAP,
                              ibl_.prefiltered()->width(), ibl_.prefiltered()->height()});
    if (ibl_.brdfLUT())
        rec.inputs.push_back({"BRDF LUT", ibl_.brdfLUT()->id(), GL_TEXTURE_2D,
                              ibl_.brdfLUT()->width(), ibl_.brdfLUT()->height()});

    sceneFbo_.bind();
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glPolygonMode(GL_FRONT_AND_BACK, settings.wireframe ? GL_LINE : GL_FILL);

    auto sh = shaders_->get("pbr");
    sh->bind();
    sh->setMat4("uView", view_);
    sh->setMat4("uProj", proj_);
    sh->setMat4("uLightMatrix", lightMatrix_);
    sh->setVec3("uCameraPos", cameraPos_);

    // Sun
    sh->setVec3("uSunDirection", scene.sun.direction());
    sh->setVec3("uSunColor", scene.sun.color * scene.sun.intensity);
    sh->setInt("uSunCastShadows", scene.sun.castShadows ? 1 : 0);
    sh->setFloat("uShadowBias", scene.sun.shadowBias);

    // Point lights
    int count = 0;
    for (const auto& l : scene.pointLights) {
        if (!l.enabled) continue;
        std::string base = "uPointLights[" + std::to_string(count) + "].";
        sh->setVec3((base + "position").c_str(), l.position);
        sh->setVec3((base + "color").c_str(), l.color * l.intensity);
        sh->setFloat((base + "radius").c_str(), l.radius);
        count++;
        if (count >= kMaxPointLights) break;
    }
    sh->setInt("uNumPointLights", count);

    // IBL + shadow textures (units 7..10, material slots use 0..6)
    sh->setFloat("uEnvIntensity", scene.environment.intensity);
    sh->setInt("uPrefilterMips", ibl_.prefilterMipCount());
    if (ibl_.irradiance()) { ibl_.irradiance()->bind(7); sh->setInt("uIrradianceMap", 7); }
    if (ibl_.prefiltered()) { ibl_.prefiltered()->bind(8); sh->setInt("uPrefilterMap", 8); }
    if (ibl_.brdfLUT()) { ibl_.brdfLUT()->bind(9); sh->setInt("uBRDFLUT", 9); }
    glActiveTexture(GL_TEXTURE10);
    glBindTexture(GL_TEXTURE_2D, shadowFbo_.depthTex());
    sh->setInt("uShadowMap", 10);

    scene.root->traverse([&](Node& node, const glm::mat4& world) {
        if (!node.mesh || !node.material) return;
        sh->setMat4("uModel", world);
        bindMaterial(*sh, *node.material);
        node.mesh->draw();
        rec.drawCalls++;
    });

    // ---- Inverted hull outlines (toon) ----
    auto outline = shaders_->get("outline");
    outline->bind();
    outline->setMat4("uView", view_);
    outline->setMat4("uProj", proj_);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    scene.root->traverse([&](Node& node, const glm::mat4& world) {
        if (!node.mesh || !node.material || !node.material->outline) return;
        outline->setMat4("uModel", world);
        outline->setFloat("uWidth", node.material->outlineWidth);
        outline->setVec3("uColor", node.material->outlineColor);
        node.mesh->draw();
        rec.drawCalls++;
    });
    glCullFace(GL_BACK);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    rec.outputs.push_back({"Scene Color (HDR)", sceneFbo_.colorTex(), GL_TEXTURE_2D,
                           sceneFbo_.width(), sceneFbo_.height()});
    rec.outputs.push_back({"Scene Depth", sceneFbo_.depthTex(), GL_TEXTURE_2D, sceneFbo_.width(),
                           sceneFbo_.height(), true, true});
    debugger_.endPass("Main (PBR/Toon)");
}

void Renderer::skyboxPass(Scene& scene) {
    if (!ibl_.environment()) return;
    PassRecord& rec = debugger_.beginPass("Skybox", "Environment cubemap background");
    rec.inputs.push_back({"Environment Cubemap", ibl_.environment()->id(), GL_TEXTURE_CUBE_MAP,
                          ibl_.environment()->width(), ibl_.environment()->height()});

    sceneFbo_.bind();
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);
    auto sh = shaders_->get("skybox");
    sh->bind();
    sh->setMat4("uView", glm::mat4(glm::mat3(view_)));  // strip translation
    sh->setMat4("uProj", proj_);
    sh->setFloat("uLod", scene.environment.backgroundLod);
    sh->setFloat("uIntensity", scene.environment.intensity);
    ibl_.environment()->bind(0);
    sh->setInt("uEnvMap", 0);
    RenderUtil::drawCube();
    rec.drawCalls++;
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);

    rec.outputs.push_back({"Scene Color (HDR)", sceneFbo_.colorTex(), GL_TEXTURE_2D,
                           sceneFbo_.width(), sceneFbo_.height()});
    debugger_.endPass("Skybox");
}

void Renderer::tonemapPass() {
    PassRecord& rec = debugger_.beginPass("Tonemap",
                                          "ACES filmic tonemapping + exposure + bloom composite, "
                                          "linear -> sRGB");
    rec.inputs.push_back({"Scene Color (HDR)", sceneFbo_.colorTex(), GL_TEXTURE_2D,
                          sceneFbo_.width(), sceneFbo_.height()});
    if (bloom_.settings.enabled && bloom_.result())
        rec.inputs.push_back({"Bloom", bloom_.result(), GL_TEXTURE_2D, vpWidth_ / 2, vpHeight_ / 2});

    finalFbo_.bind();
    glDisable(GL_DEPTH_TEST);
    auto sh = shaders_->get("tonemap");
    sh->bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneFbo_.colorTex());
    sh->setInt("uScene", 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bloom_.result());
    sh->setInt("uBloom", 1);
    sh->setInt("uBloomEnabled", bloom_.settings.enabled && bloom_.result() ? 1 : 0);
    sh->setFloat("uBloomIntensity", bloom_.settings.intensity);
    sh->setFloat("uExposure", settings.exposure);
    sh->setInt("uTonemapMode", (int)settings.tonemap);
    RenderUtil::drawFullscreen();
    rec.drawCalls++;
    glEnable(GL_DEPTH_TEST);

    rec.outputs.push_back({"Final (LDR)", finalFbo_.colorTex(), GL_TEXTURE_2D, finalFbo_.width(),
                           finalFbo_.height()});
    debugger_.endPass("Tonemap");
}

GLuint Renderer::debugView(const DebugResource& res, const DebugViewParams& params) {
    int w = std::min(res.width, 2048), h = std::min(res.height, 2048);
    if (w < 1 || h < 1) return 0;
    inspectFbo_.resize(w, h);
    inspectFbo_.bind();
    glDisable(GL_DEPTH_TEST);

    auto sh = shaders_->get("debug_view");
    sh->bind();
    bool isCube = res.target == GL_TEXTURE_CUBE_MAP;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, isCube ? 0 : res.tex);
    sh->setInt("uTex2D", 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_CUBE_MAP, isCube ? res.tex : 0);
    sh->setInt("uTexCube", 1);
    sh->setInt("uIsCube", isCube ? 1 : 0);
    sh->setInt("uFace", params.cubeFace);
    sh->setFloat("uMip", params.mip);
    sh->setInt("uChannel", params.channel);
    sh->setVec2("uRange", {params.rangeMin, params.rangeMax});
    sh->setInt("uGamma", params.gamma ? 1 : 0);
    sh->setInt("uFlipY", params.flipY ? 1 : 0);
    sh->setInt("uIsDepth", res.isDepth ? 1 : 0);
    RenderUtil::drawFullscreen();

    glEnable(GL_DEPTH_TEST);
    Framebuffer::unbind();
    return inspectFbo_.colorTex();
}
