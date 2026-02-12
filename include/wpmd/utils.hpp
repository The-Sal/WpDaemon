/**
 * @file utils.hpp
 * @brief Utility functions for path expansion and filesystem operations
 * 
 * This header provides helper functions for:
 * - Expanding tilde (~) to user's home directory
 * - Path manipulation utilities
 * 
 * These utilities are used throughout the daemon for handling paths
 * in a cross-platform manner (macOS/Linux).
 */

#pragma once

#include <string>
#include <filesystem>
#include <cstdlib>
#include <iostream>

namespace wpmd {

    /**
     * @brief Expands a tilde (~) in a path to the user's home directory
     * 
     * This function takes a path string and if it starts with '~',
     * replaces it with the value of the HOME environment variable.
     * 
     * Examples:
     *   "~/.argus"       → "/Users/username/.argus"
     *   "~/config"       → "/Users/username/config"
     *   "/absolute/path" → "/absolute/path" (unchanged)
     *   "relative/path"  → "relative/path" (unchanged)
     * 
     * @param path The path string that may contain a tilde
     * @return std::filesystem::path The expanded path
     * 
     * @note If HOME environment variable is not set, returns the original path
     */
    inline std::filesystem::path expand_tilde(const std::string& path) {
        // If path is empty or doesn't start with tilde, return as-is
        if (path.empty() || path[0] != '~') {
            return path;
        }

        // Get HOME environment variable
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

    /**
     * @brief Gets the Argus cache directory path
     * 
     * Returns ~/.argus which is used for storing:
     * - WireProxy binary
     * - WireGuard configurations
     * - Server logs
     * 
     * @return std::filesystem::path Path to ~/.argus
     */
    inline std::filesystem::path get_argus_dir() {
        return expand_tilde("~/.argus");
    }

} // namespace wpmd
