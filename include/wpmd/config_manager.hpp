/**
 * @file config_manager.hpp
 * @brief Manages WireGuard configuration files
 * 
 * This header provides functionality to:
 * - List available WireGuard configurations from ~/.argus/wireproxy_confs/
 * - Validate configuration file existence
 * - Normalize configuration names (add .conf extension if missing)
 * - Get full paths to configuration files
 * 
 * Configuration files are expected to be valid WireGuard configs
 * with an optional [Socks5] section (added automatically by the
 * management tools if missing).
 */

#pragma once

#include <string>
#include <vector>
#include <filesystem>

namespace wpmd {

    /**
     * @brief Manages WireGuard configuration files
     * 
     * This class provides a simple interface for working with
     * WireGuard configuration files stored in the Argus cache
     * directory (~/.argus/wireproxy_confs/).
     * 
     * Usage:
     *   ConfigManager cm;
     *   auto configs = cm.list_configs();  // ["us-east.conf", "eu-west.conf"]
     *   auto path = cm.get_config_path("us-east");  // ~/.argus/wireproxy_confs/us-east.conf
     *   bool exists = cm.config_exists("us-east");  // true/false
     */
    class ConfigManager {
    public:
        /**
         * @brief Constructs ConfigManager with default paths
         * 
         * Sets up the configurations directory path to
         * ~/.argus/wireproxy_confs/
         */
        ConfigManager();

        /**
         * @brief Lists all available configuration files
         * 
         * Scans the configs directory and returns a list of all
         * files ending with .conf extension.
         * 
         * @return std::vector<std::string> List of config filenames (e.g., ["us.conf", "eu.conf"])
         */
        [[nodiscard]] std::vector<std::string> list_configs() const;

        /**
         * @brief Checks if a configuration exists
         * 
         * Normalizes the config name (adds .conf if missing) and
         * checks if the file exists in the configs directory.
         * 
         * @param config_name Name of the configuration (with or without .conf)
         * @return true if configuration file exists
         * @return false if configuration file doesn't exist
         */
        [[nodiscard]] bool config_exists(const std::string& config_name) const;

        /**
         * @brief Gets the full path to a configuration file
         * 
         * Normalizes the config name (adds .conf if missing) and
         * returns the absolute path to the file.
         * 
         * @param config_name Name of the configuration (with or without .conf)
         * @return std::filesystem::path Full path to the config file
         * 
         * @note This does NOT check if the file exists - use config_exists() first
         */
        [[nodiscard]] std::filesystem::path get_config_path(const std::string& config_name) const;

        /**
         * @brief Normalizes a configuration name
         * 
         * Adds .conf extension if not present. This ensures consistent
         * naming throughout the codebase.
         * 
         * Examples:
         *   "us-east"     → "us-east.conf"
         *   "us-east.conf" → "us-east.conf" (unchanged)
         * 
         * @param config_name The config name to normalize
         * @return std::string Normalized config name with .conf extension
         */
        [[nodiscard]] static std::string normalize_config_name(const std::string& config_name);

        /**
         * @brief Gets the configurations directory path
         * 
         * @return std::filesystem::path Path to ~/.argus/wireproxy_confs/
         */
        [[nodiscard]] std::filesystem::path get_configs_dir() const { return configs_dir_; }

    private:
        std::filesystem::path configs_dir_;  ///< ~/.argus/wireproxy_confs/
    };

} // namespace wpmd
