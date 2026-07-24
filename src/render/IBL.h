#pragma once
#include <memory>
#include <string>

#include "render/Texture.h"

class ShaderLibrary;
struct EnvironmentSettings;

// Bakes image-based lighting from an equirectangular HDR (or a procedural sky):
//   environment cubemap -> diffuse irradiance map + prefiltered specular map,
//   plus the split-sum BRDF integration LUT.
class IBL {
public:
    void init(ShaderLibrary& shaders);
    // Re-bakes everything. Called when the environment settings change.
    void bake(const EnvironmentSettings& env);

    const std::shared_ptr<Texture>& environment() const { return envCubemap_; }
    const std::shared_ptr<Texture>& irradiance() const { return irradianceMap_; }
    const std::shared_ptr<Texture>& prefiltered() const { return prefilterMap_; }
    const std::shared_ptr<Texture>& brdfLUT() const { return brdfLUT_; }
    const std::shared_ptr<Texture>& sourceEquirect() const { return equirect_; }
    int prefilterMipCount() const { return kPrefilterMips; }

private:
    void bakeBRDFLUT();

    static constexpr int kEnvSize = 512;
    static constexpr int kIrradianceSize = 32;
    static constexpr int kPrefilterSize = 128;
    static constexpr int kPrefilterMips = 5;
    static constexpr int kLUTSize = 512;

    ShaderLibrary* shaders_ = nullptr;
    std::shared_ptr<Texture> equirect_;
    std::shared_ptr<Texture> envCubemap_;
    std::shared_ptr<Texture> irradianceMap_;
    std::shared_ptr<Texture> prefilterMap_;
    std::shared_ptr<Texture> brdfLUT_;
    std::string loadedHdrPath_;
};
