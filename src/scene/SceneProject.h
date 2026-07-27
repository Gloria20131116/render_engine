#pragma once
#include <string>

class Scene;
class Renderer;

// Whole-scene project file (.reproj, JSON): node tree (imported model paths,
// primitives, transforms), embedded materials (incl. node graphs, texture
// paths relative to the project file), lights, environment, camera and
// post-processing settings.
namespace SceneProject {

// Saves the scene to `path` and records it as the last opened project.
bool save(Scene& scene, Renderer& renderer, const std::string& path);

// Clears the scene and loads `path`. Imported models are re-loaded from their
// recorded source files, then saved per-node overrides are re-applied.
bool load(const std::string& path, Scene& scene, Renderer& renderer);

// Last-project bookkeeping (used to restore the previous session on startup).
std::string readLastProjectPath();
void clearLastProjectPath();

}  // namespace SceneProject
