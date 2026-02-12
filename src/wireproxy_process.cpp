//
// Created by opencode on 12/02/2026.
//

#include "wpmd/wireproxy_process.hpp"
#include "wpmd/log_manager.hpp"
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <chrono>
#include <thread>
#include <fstream>
#include <iostream>
#include <regex>

namespace wpmd {

    WireProxyProcess::WireProxyProcess(std::filesystem::path binary_path, 
                                       LogManager& log_manager)
        : binary_path_(std::move(binary_path))
        , log_manager_(log_manager)
        , pid_(-1) {}

    WireProxyProcess::~WireProxyProcess() {
        // Stop monitoring thread first
        stop_monitoring_ = true;
        if (monitor_thread_.joinable()) {
            monitor_thread_.join();
        }
        
        if (pid_ != -1 && !terminated_) {
            // Force kill on destruction if still running
            terminate();
        }
    }

    bool WireProxyProcess::spawn(const std::filesystem::path& config_path) {
        if (pid_ != -1) {
            // Already have a process
            return false;
        }
        
        config_path_ = config_path;
        
        // Get log file handle before forking
        FILE* log_handle = nullptr;
        try {
            log_handle = log_manager_.get_log_handle();
        } catch (...) {
            return false;
        }
        
        pid_t pid = fork();
        
        if (pid < 0) {
            // Fork failed
            return false;
        }
        
        if (pid == 0) {
            // Child process
            
            // Create new process group for clean termination
            setpgid(0, 0);
            
            // Redirect stdout and stderr to log file
            int log_fd = fileno(log_handle);
            dup2(log_fd, STDOUT_FILENO);
            dup2(log_fd, STDERR_FILENO);
            
            // Close original file descriptor to avoid double-close
            // (parent will close it too)
            
            // Execute wireproxy
            execl(binary_path_.c_str(), 
                  "wireproxy",
                  "-c", config_path_.c_str(),
                  nullptr);
            
            // If we get here, exec failed
            _exit(1);
        }
        
        // Parent process
        pid_ = pid;
        terminated_ = false;
        stop_monitoring_ = false;
        network_drop_detected_ = false;
        
        // Start log monitoring thread
        monitor_thread_ = std::thread(&WireProxyProcess::monitor_log_for_network_errors, this);
        
        return true;
    }

    bool WireProxyProcess::is_alive() const {
        if (pid_ == -1) {
            return false;
        }
        
        int status;
        pid_t result = waitpid(pid_, &status, WNOHANG);
        
        if (result == 0) {
            // Process is still running
            return true;
        } else if (result == pid_) {
            // Process has terminated
            return false;
        } else {
            // Error checking process
            return false;
        }
    }

    std::string WireProxyProcess::terminate() {
        if (pid_ == -1 || terminated_) {
            return "Not running";
        }
        
        std::string shutdown_method;
        
        // Try graceful termination first (SIGTERM to process group)
        kill(-pid_, SIGTERM);
        
        // Wait up to 5 seconds for graceful termination
        bool terminated_gracefully = false;
        for (int i = 0; i < 50; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            int status;
            pid_t result = waitpid(pid_, &status, WNOHANG);
            
            if (result == pid_) {
                terminated_gracefully = true;
                break;
            }
        }
        
        if (terminated_gracefully) {
            shutdown_method = "Graceful termination";
        } else {
            // Force kill
            kill(-pid_, SIGKILL);
            
            // Wait for process to die
            int status;
            waitpid(pid_, &status, 0);
            
            shutdown_method = "Force killed";
        }
        
        cleanup();
        return shutdown_method;
    }

    void WireProxyProcess::cleanup() {
        // Stop monitoring thread
        stop_monitoring_ = true;
        if (monitor_thread_.joinable()) {
            monitor_thread_.join();
        }
        
        terminated_ = true;
        pid_ = -1;
        config_path_.clear();
    }

    void WireProxyProcess::monitor_log_for_network_errors() {
        // Wait a bit for log file to be created and process to start
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        auto log_path = log_manager_.get_current_log_path();
        if (log_path.empty()) {
            return;
        }
        
        std::ifstream log_file(log_path);
        if (!log_file.is_open()) {
            return;
        }
        
        // Patterns to detect network drops
        std::regex network_unreachable("network is unreachable");
        std::regex cant_assign_address("can't assign requested address");
        
        // Threshold: 5 consecutive error lines triggers termination
        const int ERROR_THRESHOLD = 5;
        int consecutive_errors = 0;
        
        std::string line;
        // Seek to end of file to start tailing
        log_file.seekg(0, std::ios::end);
        
        while (!stop_monitoring_) {
            // Check if there's new content
            if (std::getline(log_file, line)) {
                // Check for network drop patterns
                if (std::regex_search(line, network_unreachable) || 
                    std::regex_search(line, cant_assign_address)) {
                    consecutive_errors++;
                    
                    std::cout << "[WpDaemon] Network error detected (" << consecutive_errors 
                              << "/" << ERROR_THRESHOLD << "): " << line << std::endl;
                    
                    if (consecutive_errors >= ERROR_THRESHOLD) {
                        std::lock_guard<std::mutex> lock(monitor_mutex_);
                        network_drop_detected_ = true;
                        
                        std::cout << "[WpDaemon] Network drop threshold reached! "
                                  << "Auto-terminating WireProxy process PID " << pid_ << std::endl;
                        
                        // Terminate the process
                        if (pid_ != -1 && !terminated_) {
                            kill(-pid_, SIGTERM);
                        }
                        
                        return;  // Exit monitoring thread
                    }
                } else if (line.find("ERROR:") == std::string::npos) {
                    // Reset counter on non-error lines
                    consecutive_errors = 0;
                }
            } else {
                // No new data, clear EOF flag and wait
                log_file.clear();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }

    bool WireProxyProcess::has_network_drop() const {
        std::lock_guard<std::mutex> lock(monitor_mutex_);
        return network_drop_detected_;
    }

} // namespace wpmd
