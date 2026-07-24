#include "render/IBL.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "core/Log.h"
#include "render/RenderUtil.h"
#include "render/ShaderLibrary.h"
#include "scene/Scene.h"

static const glm::mat4 kCaptureProj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);

static glm::mat4 captureView(int face) {
    static const glm::mat4 views[6] = {
        glm::lookAt(glm::vec3(0), glm::vec3(1, 0, 0), glm::vec3(0, -1, 0)),
        glm::lookAt(glm::vec3(0), glm::vec3(-1, 0, 0), glm::vec3(0, -1, 0)),
        glm::lookAt(glm::vec3(0), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1)),
        glm::lookAt(glm::vec3(0), glm::vec3(0, -1, 0), glm::vec3(0, 0, -1)),
        glm::lookAt(glm::vec3(0), glm::vec3(0, 0, 1), glm::vec3(0, -1, 0)),
        glm::lookAt(glm::vec3(0), glm::vec3(0, 0, -1), glm::vec3(0, -1, 0)),
    };
    return views[face];
}

void IBL::init(ShaderLibrary& shaders) {
    shaders_ = &shaders;
    shaders.load("equirect_to_cube", "cube_capture.vert", "equirect_to_cube.frag");
    shaders.load("procedural_sky_capture", "cube_capture.vert", "procedural_sky.frag");
    shaders.load("irradiance", "cube_capture.vert", "irradiance.frag");
    shaders.load("prefilter", "cube_capture.vert", "prefilter.frag");
    shaders.load("brdf_lut", "fullscreen.vert", "brdf_lut.frag");
    bakeBRDFLUT();
}

void IBL::bake(const EnvironmentSettings& env) {
    // ---- 1. Source: equirect HDR or procedural sky ----
    bool useHdr = !env.hdrPath.empty();
    if (useHdr && env.hdrPath != loadedHdrPath_) {
        equirect_ = Texture::loadHDR(env.hdrPath);
        loadedHdrPath_ = equirect_ ? env.hdrPath : "";
    }
    if (!equirect_) useHdr = false;

    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    // ---- 2. Environment cubemap ----
    if (!envCubemap_)
        envCubemap_ = Texture::createCubemap(kEnvSize, GL_RGB16F, true, "IBL Environment");
    auto capture = shaders_->get(useHdr ? "equirect_to_cube" : "procedural_sky_capture");
    capture->bind();
    capture->setMat4("uProj", kCaptureProj);
    capture->setFloat("uRotation", glm::radians(env.rotationDeg));
    if (useHdr) {
        equirect_->bind(0);
        capture->setInt("uEquirect", 0);
    }
    glViewport(0, 0, kEnvSize, kEnvSize);
    for (int i = 0; i < 6; ++i) {
        capture->setMat4("uView", captureView(i));
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, envCubemap_->id(), 0);
        glClear(GL_COLOR_BUFFER_BIT);
        RenderUtil::drawCube();
    }
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap_->id());
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    // ---- 3. Diffuse irradiance ----
    if (!irradianceMap_)
        irradianceMap_ = Texture::createCubemap(kIrradianceSize, GL_RGB16F, false, "IBL Irradiance");
    auto irr = shaders_->get("irradiance");
    irr->bind();
    irr->setMat4("uProj", kCaptureProj);
    envCubemap_->bind(0);
    irr->setInt("uEnvMap", 0);
    glViewport(0, 0, kIrradianceSize, kIrradianceSize);
    for (int i = 0; i < 6; ++i) {
        irr->setMat4("uView", captureView(i));
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, irradianceMap_->id(), 0);
        glClear(GL_COLOR_BUFFER_BIT);
        RenderUtil::drawCube();
    }

    // ---- 4. Prefiltered specular (per-mip roughness) ----
    if (!prefilterMap_)
        prefilterMap_ = Texture::createCubemap(kPrefilterSize, GL_RGB16F, true, "IBL Prefiltered");
    auto pre = shaders_->get("prefilter");
    pre->bind();
    pre->setMat4("uProj", kCaptureProj);
    envCubemap_->bind(0);
    pre->setInt("uEnvMap", 0);
    pre->setFloat("uEnvResolution", (float)kEnvSize);
    for (int mip = 0; mip < kPrefilterMips; ++mip) {
        int size = kPrefilterSize >> mip;
        glViewport(0, 0, size, size);
        pre->setFloat("uRoughness", (float)mip / (kPrefilterMips - 1));
        for (int i = 0; i < 6; ++i) {
            pre->setMat4("uView", captureView(i));
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, prefilterMap_->id(), mip);
            glClear(GL_COLOR_BUFFER_BIT);
            RenderUtil::drawCube();
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    glEnable(GL_DEPTH_TEST);
    Log::info("IBL baked (%s)", useHdr ? loadedHdrPath_.c_str() : "procedural sky");
}

void IBL::bakeBRDFLUT() {
    brdfLUT_ = Texture::create2D(kLUTSize, kLUTSize, GL_RG16F, "BRDF LUT");
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, brdfLUT_->id(), 0);
    glViewport(0, 0, kLUTSize, kLUTSize);
    glDisable(GL_DEPTH_TEST);
    auto sh = shaders_->get("brdf_lut");
    sh->bind();
    RenderUtil::drawFullscreen();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    glEnable(GL_DEPTH_TEST);
}
