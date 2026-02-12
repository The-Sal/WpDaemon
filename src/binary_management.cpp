//
// Created by Sal Faris on 11/02/2026.
//

#include <iostream>
#include <ostream>
#include "filesystem"


inline std::filesystem::path expand_tilde(const std::string& path) {
    if (path.empty() || path[0] != '~') {
        return path;
    }

    const char* home = std::getenv("HOME");
    if (!home) {
        std::cerr << "HOME environment variable not set" << std::endl;
        return path;
    }

    // Skip "~/" to avoid treating the rest as an absolute path
    std::string rest = path.substr(1);
    if (!rest.empty() && rest[0] == '/') {
        rest = rest.substr(1);
    }

    return std::filesystem::path(home) / rest;
}


class BinaryManager {
    std::filesystem::path binary_path = expand_tilde( "~/.argus/wireproxy/wireproxy");
public:

    BinaryManager() = default;

    [[nodiscard]] bool file_exists() const {
        return std::filesystem::exists(binary_path);
    }

    [[nodiscard]] auto copy_to_binary_path(const std::filesystem::path& source_path) const {
        auto error = std::error_code{};
        std::filesystem::copy(source_path, binary_path,
            std::filesystem::copy_options::overwrite_existing, error);

        if (error) {
            std::cerr << "Failed to copy binary to " << binary_path << ": " << error.message() << std::endl;
            return false;
        }

        return true;
    }

    [[nodiscard]] auto get_path() const { return binary_path; }
};

