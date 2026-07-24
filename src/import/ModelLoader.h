#pragma once
#include <memory>
#include <string>

class Node;

// Imports a model file (FBX/OBJ/glTF/DAE/PMX) via Assimp into a Node subtree.
// Textures referenced by the file are loaded relative to the model directory;
// embedded textures (common in FBX) are supported.
class ModelLoader {
public:
    static std::shared_ptr<Node> load(const std::string& path);
};
