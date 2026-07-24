#pragma once
#include <memory>

class Scene;

// UE-style visual material graph editor panel ("Material Graph" window),
// built on imnodes. Operates on the selected node's material->graph.
class NodeEditor {
public:
    void draw(Scene& scene);

private:
    void drawGraphNodes(class MaterialGraph& graph);
    void handleInteractions(class MaterialGraph& graph);
    void addNodePopup(class MaterialGraph& graph);
};
