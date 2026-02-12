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

namespace wpmd {

    WireProxyProcess::WireProxyProcess(std::filesystem::path binary_path, 
                                       LogManager& log_manager)
        : binary_path_(std::move(binary_path))
        , log_manager_(log_manager)
        , pid_(-1) {}

    WireProxyProcess::~WireProxyProcess() {
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
        terminated_ = true;
        pid_ = -1;
        config_path_.clear();
    }

} // namespace wpmd
