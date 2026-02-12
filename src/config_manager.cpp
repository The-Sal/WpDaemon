//
// Created by opencode on 12/02/2026.
//

#include "wpmd/config_manager.hpp"
#include "wpmd/utils.hpp"
#include <algorithm>

namespace wpmd {

    ConfigManager::ConfigManager() 
        : configs_dir_(get_argus_dir() / "wireproxy_confs") {
        // Create configs directory if it doesn't exist
        if (!std::filesystem::exists(configs_dir_)) {
            std::filesystem::create_directories(configs_dir_);
        }
    }

    std::vector<std::string> ConfigManager::list_configs() const {
        std::vector<std::string> configs;
        
        if (!std::filesystem::exists(configs_dir_)) {
            return configs;
        }
        
        for (const auto& entry : std::filesystem::directory_iterator(configs_dir_)) {
            if (entry.is_regular_file() && entry.path().extension() == ".conf") {
                configs.push_back(entry.path().filename().string());
            }
        }
        
        // Sort alphabetically for consistent ordering
        std::sort(configs.begin(), configs.end());
        
        return configs;
    }

    bool ConfigManager::config_exists(const std::string& config_name) const {
        std::string normalized = normalize_config_name(config_name);
        std::filesystem::path config_path = configs_dir_ / normalized;
        return std::filesystem::exists(config_path) && 
               std::filesystem::is_regular_file(config_path);
    }

    std::filesystem::path ConfigManager::get_config_path(const std::string& config_name) const {
        std::string normalized = normalize_config_name(config_name);
        return configs_dir_ / normalized;
    }

    std::string ConfigManager::normalize_config_name(const std::string& config_name) {
        if (config_name.length() >= 5 && 
            config_name.compare(config_name.length() - 5, 5, ".conf") == 0) {
            return config_name;
        }
        return config_name + ".conf";
    }

} // namespace wpmd
