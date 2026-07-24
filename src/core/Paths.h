#pragma once
#include <filesystem>
#include <string>

// Locates the assets directory by walking up from the current working directory.
class Paths {
public:
    static const std::filesystem::path& assets() {
        static std::filesystem::path dir = findAssets();
        return dir;
    }

    static std::string shader(const std::string& name) {
        return (assets() / "shaders" / name).string();
    }

    static std::string asset(const std::string& rel) {
        return (assets() / rel).string();
    }

private:
    static std::filesystem::path findAssets() {
        namespace fs = std::filesystem;
        fs::path p = fs::current_path();
        for (int i = 0; i < 6; ++i) {
            if (fs::exists(p / "assets" / "shaders")) return p / "assets";
            if (!p.has_parent_path() || p.parent_path() == p) break;
            p = p.parent_path();
        }
        return fs::current_path() / "assets";
    }
};
