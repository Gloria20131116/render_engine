#include "render/Renderer.h"

#include <algorithm>
#include <chrono>
#include <glm/gtc/matrix_transform.hpp>
#include <string>

#include "material/MaterialCodeGen.h"
#include "material/MaterialGraph.h"
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
    graphCache_.init(shaders);
    whiteTex_ = Texture::solid(255, 255, 255, 255, "GraphWhite");

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

    static const auto start = std::chrono::steady_clock::now();
    time_ = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();

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

    ensureMsaaTarget();

    shadowPass(scene);
    mainPass(scene);
    outlinePass(scene);
    skyboxPass(scene);
    resolveMsaa();
    bloom_.render(sceneFbo_.colorTex(), debugger_);
    tonemapPass();

    Framebuffer::unbind();
}

void Renderer::ensureMsaaTarget() {
    if (!msaaActive()) return;
    if (msaaFbo_ && msaaW_ == vpWidth_ && msaaH_ == vpHeight_ &&
        msaaSamples_ == settings.msaaSamples)
        return;

    if (msaaFbo_) {
        glDeleteFramebuffers(1, &msaaFbo_);
        glDeleteTextures(1, &msaaColor_);
        glDeleteTextures(1, &msaaDepth_);
    }
    msaaW_ = vpWidth_;
    msaaH_ = vpHeight_;
    msaaSamples_ = settings.msaaSamples;

    glGenTextures(1, &msaaColor_);
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, msaaColor_);
    glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, msaaSamples_, GL_RGBA16F, msaaW_, msaaH_,
                            GL_TRUE);
    glGenTextures(1, &msaaDepth_);
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, msaaDepth_);
    glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, msaaSamples_, GL_DEPTH_COMPONENT32F,
                            msaaW_, msaaH_, GL_TRUE);

    glGenFramebuffers(1, &msaaFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE,
                           msaaColor_, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D_MULTISAMPLE,
                           msaaDepth_, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::bindSceneTarget() {
    if (msaaActive()) {
        glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo_);
        glViewport(0, 0, msaaW_, msaaH_);
    } else {
        sceneFbo_.bind();
    }
}

void Renderer::resolveMsaa() {
    if (!msaaActive()) return;
    PassRecord& rec = debugger_.beginPass(
        "MSAA Resolve", "Resolves the multisampled scene target (color + depth) into the "
                        "single-sample HDR texture consumed by bloom/tonemap.");
    glBindFramebuffer(GL_READ_FRAMEBUFFER, msaaFbo_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sceneFbo_.id());
    glBlitFramebuffer(0, 0, msaaW_, msaaH_, 0, 0, sceneFbo_.width(), sceneFbo_.height(),
                      GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    rec.outputs.push_back({"Scene Color (HDR)", sceneFbo_.colorTex(), GL_TEXTURE_2D,
                           sceneFbo_.width(), sceneFbo_.height()});
    debugger_.endPass("MSAA Resolve");
}

// Mirrored transforms (negative determinant, common for FBX instanced parts)
// flip triangle winding; without this the mirrored half is culled inside-out
// and gl_FrontFacing-based normal flips make its lighting look inverted.
static void setFrontFaceFor(const glm::mat4& world) {
    glFrontFace(glm::determinant(glm::mat3(world)) < 0.0f ? GL_CW : GL_CCW);
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
            setFrontFaceFor(world);
            sh->setMat4("uModel", world);
            node.mesh->draw();
            rec.drawCalls++;
        });
        glFrontFace(GL_CCW);
        glCullFace(GL_BACK);
    }

    rec.outputs.push_back({"Shadow Depth", shadowFbo_.depthTex(), GL_TEXTURE_2D,
                           shadowFbo_.width(), shadowFbo_.height(), true, true});
    debugger_.endPass("Shadow Map");
}

void Renderer::bindLighting(Shader& sh, Scene& scene, int iblBaseUnit) {
    sh.setMat4("uView", view_);
    sh.setMat4("uProj", proj_);
    sh.setMat4("uLightMatrix", lightMatrix_);
    sh.setVec3("uCameraPos", cameraPos_);

    sh.setVec3("uSunDirection", scene.sun.direction());
    sh.setVec3("uSunColor", scene.sun.color * scene.sun.intensity);
    sh.setInt("uSunCastShadows", scene.sun.castShadows ? 1 : 0);
    sh.setFloat("uShadowBias", scene.sun.shadowBias);

    int count = 0;
    for (const auto& l : scene.pointLights) {
        if (!l.enabled) continue;
        std::string base = "uPointLights[" + std::to_string(count) + "].";
        sh.setVec3((base + "position").c_str(), l.position);
        sh.setVec3((base + "color").c_str(), l.color * l.intensity);
        sh.setFloat((base + "radius").c_str(), l.radius);
        count++;
        if (count >= kMaxPointLights) break;
    }
    sh.setInt("uNumPointLights", count);

    sh.setFloat("uEnvIntensity", scene.environment.intensity);
    sh.setInt("uPrefilterMips", ibl_.prefilterMipCount());
    int u = iblBaseUnit;
    if (ibl_.irradiance()) { ibl_.irradiance()->bind(u); sh.setInt("uIrradianceMap", u); }
    u++;
    if (ibl_.prefiltered()) { ibl_.prefiltered()->bind(u); sh.setInt("uPrefilterMap", u); }
    u++;
    if (ibl_.brdfLUT()) { ibl_.brdfLUT()->bind(u); sh.setInt("uBRDFLUT", u); }
    u++;
    glActiveTexture(GL_TEXTURE0 + u);
    glBindTexture(GL_TEXTURE_2D, shadowFbo_.depthTex());
    sh.setInt("uShadowMap", u);
}

void Renderer::bindGraphMaterial(Shader& sh, const Material& mat) {
    const MaterialGraph& g = *mat.graph;
    for (const auto& n : g.nodes) {
        const NodeTypeDef* def = MaterialGraph::typeDef(n.type);
        if (def && def->hasValue)
            sh.setVec4(("uN" + std::to_string(n.id)).c_str(), n.value);
    }
    auto texOrder = MaterialCodeGen::textureNodeOrder(g);
    for (size_t i = 0; i < texOrder.size() && i < 8; ++i) {
        const GraphNode* n = g.findNode(texOrder[i]);
        sh.setInt(("uGraphTex" + std::to_string(i)).c_str(), (int)i);
        if (n && n->texture) n->texture->bind((int)i);
        else whiteTex_->bind((int)i);
    }
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
    sh.setInt("uFlipNormals", mat.flipNormals ? 1 : 0);

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
        "Inputs: shadow map + IBL maps. Renders into the MSAA target when enabled.");

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

    bindSceneTarget();
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glPolygonMode(GL_FRONT_AND_BACK, settings.wireframe ? GL_LINE : GL_FILL);

    auto sh = shaders_->get("pbr");
    sh->bind();
    bindLighting(*sh, scene, 7);  // material slots use 0..6

    scene.root->traverse([&](Node& node, const glm::mat4& world) {
        if (!node.mesh || !node.material) return;
        setFrontFaceFor(world);
        Material& mat = *node.material;

        // ---- Node-graph material path (UE-style material editor) ----
        if (mat.graph) {
            auto gsh = graphCache_.ensure(*mat.graph);
            if (gsh && gsh->valid()) {
                gsh->bind();
                bindLighting(*gsh, scene, 8);  // graph samplers use 0..7
                gsh->setMat4("uModel", world);
                gsh->setInt("uShadingModel", (int)mat.model);
                gsh->setInt("uFlipNormals", mat.flipNormals ? 1 : 0);
                gsh->setFloat("uSpecularF0", mat.specularF0);
                gsh->setFloat("uIBLIntensity", mat.iblIntensity);
                gsh->setFloat("uTime", time_);
                gsh->setVec2("uViewportSize", {(float)vpWidth_, (float)vpHeight_});
                bindGraphMaterial(*gsh, mat);
                if (mat.doubleSided) glDisable(GL_CULL_FACE);
                else glEnable(GL_CULL_FACE);
                node.mesh->draw();
                rec.drawCalls++;
                sh->bind();  // restore the unified shader for following nodes
                return;
            }
        }

        sh->setMat4("uModel", world);
        bindMaterial(*sh, mat);
        node.mesh->draw();
        rec.drawCalls++;
    });
    glFrontFace(GL_CCW);

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    rec.outputs.push_back({"Scene Color (HDR)", sceneFbo_.colorTex(), GL_TEXTURE_2D,
                           sceneFbo_.width(), sceneFbo_.height()});
    rec.outputs.push_back({"Scene Depth", sceneFbo_.depthTex(), GL_TEXTURE_2D, sceneFbo_.width(),
                           sceneFbo_.height(), true, true});
    debugger_.endPass("Main (PBR/Toon)");
}

// ZZZ-style inverted hull outlines as a dedicated pass: the mesh is drawn a
// second time with front faces culled and vertices extruded along smoothed
// normals in clip space (pixel-constant width). This is NOT a post-process
// convolution: per-material width/color, no false edges from textures, and
// the pass shows up separately in the frame debugger with its own GPU timing.
void Renderer::outlinePass(Scene& scene) {
    if (!settings.outlineEnabled) return;
    PassRecord& rec = debugger_.beginPass(
        "Outline (Inverted Hull)",
        "ZZZ-style backface expansion: smoothed normals, screen-pixel constant width "
        "with world-space clamp, depth offset. Color can derive from the base color/albedo.");

    bindSceneTarget();
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);

    auto sh = shaders_->get("outline");
    sh->bind();
    sh->setMat4("uView", view_);
    sh->setMat4("uProj", proj_);
    sh->setVec2("uViewport", {(float)vpWidth_, (float)vpHeight_});

    scene.root->traverse([&](Node& node, const glm::mat4& world) {
        if (!node.mesh || !node.material || !node.material->outline) return;
        const Material& m = *node.material;
        setFrontFaceFor(world);
        sh->setMat4("uModel", world);
        sh->setInt("uFlipNormals", m.flipNormals ? 1 : 0);
        sh->setFloat("uWidthPx", m.outlineWidthPx);
        sh->setFloat("uMaxWorldWidth", m.outlineMaxWorldWidth);
        sh->setFloat("uZOffset", m.outlineZOffset);
        sh->setVec3("uColor", m.outlineColor);
        sh->setInt("uFromBaseColor", m.outlineFromBaseColor ? 1 : 0);
        sh->setFloat("uColorScale", m.outlineColorScale);
        sh->setVec3("uBaseColor", m.baseColor);
        sh->setInt("uHasAlbedoMap", m.albedoMap ? 1 : 0);
        sh->setInt("uAlbedoMap", 0);
        if (m.albedoMap) m.albedoMap->bind(0);
        node.mesh->draw();
        rec.drawCalls++;
    });
    glFrontFace(GL_CCW);
    glCullFace(GL_BACK);

    rec.outputs.push_back({"Scene Color (HDR)", sceneFbo_.colorTex(), GL_TEXTURE_2D,
                           sceneFbo_.width(), sceneFbo_.height()});
    debugger_.endPass("Outline (Inverted Hull)");
}

void Renderer::skyboxPass(Scene& scene) {
    if (!ibl_.environment()) return;
    PassRecord& rec = debugger_.beginPass("Skybox", "Environment cubemap background");
    rec.inputs.push_back({"Environment Cubemap", ibl_.environment()->id(), GL_TEXTURE_CUBE_MAP,
                          ibl_.environment()->width(), ibl_.environment()->height()});

    bindSceneTarget();
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
