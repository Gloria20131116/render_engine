#pragma once
#include <string>
#include <vector>

struct Material;

// Built-in material preset library.  Each preset is a factory that returns a
// fully configured Material (no textures - those are assigned by the user or
// loaded from a .mat file afterwards).
namespace MaterialPreset {

// Display names of all built-in presets, in UI order.
const std::vector<std::string>& names();

// Number of built-in presets.
int count();

// Create a Material from the preset at `index` (0..count()-1).
// Out-of-range indices return the "Default PBR" preset.
Material create(int index);

// Create a Material from a preset by display name.
// Unknown names return the "Default PBR" preset.
Material create(const std::string& name);

}  // namespace MaterialPreset
