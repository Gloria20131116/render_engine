#pragma once
#include <filesystem>
#include <string>

#include <nlohmann/json_fwd.hpp>

struct Material;

namespace MaterialAsset {

// Serialize a Material to a JSON .mat file.
// Texture slots are stored as relative paths (relative to the .mat file directory).
// Returns true on success.
bool save(const Material& mat, const std::string& path);

// Deserialize a .mat JSON file into an existing Material.
// Texture slots are loaded from the paths stored in the file (resolved relative
// to the .mat file directory).  Returns true on success.
bool load(const std::string& path, Material& outMat);

// JSON building blocks (also used by the scene project file, which embeds
// materials directly).  `baseDir` is the directory texture paths are stored
// relative to / resolved against.
void toJson(const Material& mat, nlohmann::json& j, const std::filesystem::path& baseDir);
void fromJson(const nlohmann::json& j, Material& outMat, const std::filesystem::path& baseDir);

}  // namespace MaterialAsset
