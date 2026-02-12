/**
 * @file binary_manager.hpp
 * @brief Manages WireProxy binary download, installation, and validation
 * 
 * This header provides functionality to:
 * - Detect the current platform (OS + architecture)
 * - Download the correct WireProxy binary from GitHub releases
 * - Extract and install the binary to ~/.argus/wireproxy/
 * - Verify binary existence and functionality
 * 
 * The binary manager runs on daemon startup to ensure wireproxy is available
 * before any connection attempts are made.
 */

#pragma once

#include <string>
#include <filesystem>
#include <tuple>

namespace wpmd {

    /**
     * @brief Manages WireProxy binary lifecycle
     * 
     * This class handles everything related to the wireproxy executable:
     * - Platform detection (OS + architecture mapping)
     * - Binary download from GitHub releases
     * - Tar.gz extraction
     * - Binary installation to ~/.argus/wireproxy/
     * - Version verification
     * 
     * Supported platforms:
     * - Darwin (macOS): amd64, arm64
     * - Linux: amd64, arm
     * 
     * Architecture mapping:
     * - x86_64 / AMD64 → amd64
     * - arm64 / aarch64 → arm64 (Darwin) or arm (Linux)
     */
    class BinaryManager {
    public:
        /**
         * @brief Constructs BinaryManager and defines paths
         * 
         * Sets up paths for:
         * - Binary location: ~/.argus/wireproxy/wireproxy
         * - Config directory: ~/.argus/wireproxy_confs/
         * - Logs directory: ~/.argus/wp-server-logs/
         */
        BinaryManager();

        /**
         * @brief Checks if wireproxy binary exists at expected location
         * 
         * @return true if binary file exists and is regular file
         * @return false otherwise
         */
        [[nodiscard]] bool binary_exists() const;

        /**
         * @brief Gets the full path to the wireproxy binary
         * 
         * @return std::filesystem::path Path to ~/.argus/wireproxy/wireproxy
         */
        [[nodiscard]] std::filesystem::path get_binary_path() const { return binary_path_; }

        /**
         * @brief Gets the directory containing WireGuard configurations
         * 
         * @return std::filesystem::path Path to ~/.argus/wireproxy_confs/
         */
        [[nodiscard]] std::filesystem::path get_configs_dir() const { return configs_dir_; }

        /**
         * @brief Gets the directory for server logs
         * 
         * @return std::filesystem::path Path to ~/.argus/wp-server-logs/
         */
        [[nodiscard]] std::filesystem::path get_logs_dir() const { return logs_dir_; }

        /**
         * @brief Downloads and installs wireproxy if not present
         * 
         * This method:
         * 1. Detects current platform (OS + architecture)
         * 2. Constructs download URL for wireproxy release
         * 3. Downloads tar.gz archive using HTTP
         * 4. Extracts the archive
         * 5. Copies wireproxy binary to target location
         * 6. Verifies binary with `wireproxy -v`
         * 
         * @return true if binary is available (already existed or downloaded successfully)
         * @return false if download/installation failed
         * 
         * @throws std::runtime_error if platform is unsupported
         */
        bool ensure_binary_available();

        /**
         * @brief Gets the wireproxy version string
         * 
         * Runs `wireproxy -v` and returns the output
         * 
         * @return std::string Version string or "Unknown" if failed
         */
        [[nodiscard]] std::string get_version() const;

    private:
        /**
         * @brief Detects current platform and returns download filename
         * 
         * Uses uname() system call to detect OS and architecture,
         * then maps to wireproxy's naming convention.
         * 
         * @return std::tuple<std::string, std::string> (OS name, architecture)
         * @throws std::runtime_error if platform is unsupported
         */
        std::tuple<std::string, std::string> detect_platform() const;

        /**
         * @brief Downloads file from URL to destination
         * 
         * Uses libcpr (libcurl wrapper) for HTTP download with
         * redirect following.
         * 
         * @param url The URL to download from
         * @param destination Path where to save the file
         * @return true if download succeeded
         * @return false if download failed
         */
        bool download_file(const std::string& url, const std::filesystem::path& destination) const;

        /**
         * @brief Extracts tar.gz archive
         * 
         * Uses system tar command to extract archive contents
         * 
         * @param archive_path Path to the tar.gz file
         * @param extract_dir Directory to extract into
         * @return true if extraction succeeded
         * @return false if extraction failed
         */
        bool extract_archive(const std::filesystem::path& archive_path, 
                            const std::filesystem::path& extract_dir) const;

        std::filesystem::path binary_path_;     ///< ~/.argus/wireproxy/wireproxy
        std::filesystem::path configs_dir_;     ///< ~/.argus/wireproxy_confs/
        std::filesystem::path logs_dir_;        ///< ~/.argus/wp-server-logs/
        std::filesystem::path install_dir_;     ///< ~/.argus/wireproxy/
    };

} // namespace wpmd
