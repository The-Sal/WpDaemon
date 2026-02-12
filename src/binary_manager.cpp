//
// Created by opencode on 12/02/2026.
//

#include "wpmd/binary_manager.hpp"
#include "wpmd/utils.hpp"
#include <cpr/cpr.h>
#include <sys/utsname.h>
#include <array>
#include <memory>
#include <stdexcept>
#include <cstdio>

namespace wpmd {

    BinaryManager::BinaryManager()
        : binary_path_(get_argus_dir() / "wireproxy" / "wireproxy")
        , configs_dir_(get_argus_dir() / "wireproxy_confs")
        , logs_dir_(get_argus_dir() / "wp-server-logs")
        , install_dir_(get_argus_dir() / "wireproxy") {
        
        // Create directories if they don't exist
        std::filesystem::create_directories(install_dir_);
        std::filesystem::create_directories(configs_dir_);
        std::filesystem::create_directories(logs_dir_);
    }

    bool BinaryManager::binary_exists() const {
        return std::filesystem::exists(binary_path_) && 
               std::filesystem::is_regular_file(binary_path_);
    }

    bool BinaryManager::ensure_binary_available() {
        // Check if binary already exists
        if (binary_exists()) {
            return true;
        }
        
        // Detect platform and get download filename
        auto [os_name, arch_name] = detect_platform();
        std::string filename = "wireproxy_" + os_name + "_" + arch_name + ".tar.gz";
        
        std::cout << "Checking OS information..." << std::endl;
        std::cout << "Platform: " << os_name << " " << arch_name << std::endl;
        
        // Construct download URL
        std::string url = "https://github.com/whyvl/wireproxy/releases/latest/download/" + filename;
        std::cout << "Downloading WireProxy from " << url << std::endl;
        
        // Download to temp directory
        std::filesystem::path temp_dir = std::filesystem::temp_directory_path();
        std::filesystem::path download_path = temp_dir / filename;
        
        if (!download_file(url, download_path)) {
            std::cerr << "Failed to download wireproxy" << std::endl;
            return false;
        }
        
        // Extract archive
        std::filesystem::path extract_dir = temp_dir / "wireproxy_extract";
        if (!extract_archive(download_path, extract_dir)) {
            std::cerr << "Failed to extract archive" << std::endl;
            std::filesystem::remove(download_path);
            return false;
        }
        
        // Find and copy wireproxy binary
        std::filesystem::path extracted_binary = extract_dir / "wireproxy";
        if (!std::filesystem::exists(extracted_binary)) {
            std::cerr << "Unable to find wireproxy binary in archive" << std::endl;
            std::filesystem::remove_all(extract_dir);
            std::filesystem::remove(download_path);
            return false;
        }
        
        // Copy to install directory
        std::cout << "Moving wireproxy..." << std::endl;
        std::error_code ec;
        std::filesystem::copy(extracted_binary, binary_path_, 
                              std::filesystem::copy_options::overwrite_existing, ec);
        
        if (ec) {
            std::cerr << "Failed to copy binary: " << ec.message() << std::endl;
            std::filesystem::remove_all(extract_dir);
            std::filesystem::remove(download_path);
            return false;
        }
        
        // Make binary executable
        std::filesystem::permissions(binary_path_, 
                                      std::filesystem::perms::owner_exec | 
                                      std::filesystem::perms::group_exec | 
                                      std::filesystem::perms::others_exec,
                                      std::filesystem::perm_options::add);
        
        // Cleanup temp files
        std::filesystem::remove_all(extract_dir);
        std::filesystem::remove(download_path);
        
        // Verify binary works
        std::cout << std::string(40, '*') << std::endl;
        std::string version = get_version();
        std::cout << version << std::endl;
        std::cout << std::string(40, '*') << std::endl;
        
        return binary_exists();
    }

    std::string BinaryManager::get_version() const {
        if (!binary_exists()) {
            return "Unknown (binary not found)";
        }
        
        std::array<char, 128> buffer;
        std::string result;
        
        std::string cmd = binary_path_.string() + " -v";
        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
        
        if (!pipe) {
            return "Unknown (failed to run)";
        }
        
        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
            result += buffer.data();
        }
        
        // Remove trailing newline
        if (!result.empty() && result.back() == '\n') {
            result.pop_back();
        }
        
        return result;
    }

    std::tuple<std::string, std::string> BinaryManager::detect_platform() const {
        struct utsname uname_data;
        if (uname(&uname_data) != 0) {
            throw std::runtime_error("Failed to detect platform");
        }
        
        std::string sysname = uname_data.sysname;
        std::string machine = uname_data.machine;
        
        // Normalize OS name
        std::string os_name;
        if (sysname == "Darwin") {
            os_name = "darwin";
        } else if (sysname == "Linux") {
            os_name = "linux";
        } else {
            throw std::runtime_error("Unsupported OS: " + sysname);
        }
        
        // Map architecture
        std::string arch_name;
        if (machine == "x86_64" || machine == "AMD64") {
            arch_name = "amd64";
        } else if (machine == "arm64") {
            // Darwin uses arm64, Linux uses arm for aarch64
            arch_name = "arm64";
        } else if (machine == "aarch64") {
            // Linux aarch64 maps to arm in wireproxy naming
            if (os_name == "linux") {
                arch_name = "arm";
            } else {
                arch_name = "arm64";
            }
        } else if (machine == "armv7l" || machine == "armv6l" || machine == "arm") {
            // ARMv7 and ARMv6 (32-bit ARM) support
            if (os_name == "linux") {
                arch_name = "arm";
            } else {
                throw std::runtime_error("ARMv7/ARMv6 only supported on Linux");
            }
        } else {
            throw std::runtime_error("Unsupported architecture: " + machine);
        }
        
        // Validate supported platforms
        std::string filename = "wireproxy_" + os_name + "_" + arch_name + ".tar.gz";
        std::set<std::string> valid_filenames = {
            "wireproxy_darwin_amd64.tar.gz",
            "wireproxy_darwin_arm64.tar.gz",
            "wireproxy_linux_amd64.tar.gz",
            "wireproxy_linux_arm.tar.gz"
        };
        
        if (valid_filenames.find(filename) == valid_filenames.end()) {
            throw std::runtime_error("Unsupported platform: " + sysname + " " + machine);
        }
        
        return {os_name, arch_name};
    }

    bool BinaryManager::download_file(const std::string& url, 
                                       const std::filesystem::path& destination) const {
        cpr::Response response = cpr::Get(cpr::Url{url},
                                          cpr::Redirect(true));
        
        if (response.status_code != 200) {
            std::cerr << "Download failed with status: " << response.status_code << std::endl;
            return false;
        }
        
        std::ofstream file(destination, std::ios::binary);
        if (!file) {
            std::cerr << "Failed to open destination file: " << destination << std::endl;
            return false;
        }
        
        file.write(response.text.data(), response.text.size());
        file.close();
        
        return true;
    }

    bool BinaryManager::extract_archive(const std::filesystem::path& archive_path,
                                         const std::filesystem::path& extract_dir) const {
        // Create extract directory
        std::filesystem::create_directories(extract_dir);
        
        // Use system tar command for extraction
        std::string cmd = "tar -xzf \"" + archive_path.string() + "\" -C \"" + 
                         extract_dir.string() + "\"";
        
        int result = std::system(cmd.c_str());
        return result == 0;
    }

} // namespace wpmd
