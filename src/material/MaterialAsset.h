#pragma once
#include <string>

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

}  // namespace MaterialAsset
