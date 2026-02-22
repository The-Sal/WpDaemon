//
// Created by opencode on 22/02/2026.
//

#include "wpmd/daemonizer.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <sockpp/tcp_connector.h>
#include <sockpp/inet_address.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace wpmd {

    Daemonizer::Daemonizer(const std::string& host, int port)
        : host_(host)
        , port_(port) {}

    bool Daemonizer::is_daemon_running() {
        try {
            sockpp::tcp_connector conn;
            sockpp::inet_address addr(host_, port_);
            
            // Try to connect with short timeout
            if (!conn.connect(addr, std::chrono::milliseconds(1000))) {
                return false;
            }
            
            // Send whoami command
            std::string cmd = "whoami:\n";
            auto write_result = conn.write(cmd);
            if (!write_result.is_ok()) {
                conn.close();
                return false;
            }
            
            // Set read timeout and read response
            conn.read_timeout(std::chrono::milliseconds(2000));
            char buffer[1024];
            auto read_result = conn.read(buffer, sizeof(buffer) - 1);
            conn.close();
            
            if (!read_result.is_ok()) {
                return false;
            }
            
            size_t bytes_read = read_result.value();
            if (bytes_read == 0) {
                return false;
            }
            
            buffer[bytes_read] = '\0';
            std::string response(buffer);
            
            // Check if response contains expected fields
            return response.find("version") != std::string::npos;
            
        } catch (...) {
            return false;
        }
    }

    bool Daemonizer::spawn_daemon(const std::string& daemon_binary_path) {
        // Fork to spawn daemon
        pid_t pid = fork();
        
        if (pid < 0) {
            last_error_ = "Failed to fork: " + std::string(strerror(errno));
            return false;
        }
        
        if (pid > 0) {
            // Parent process - wait for child to finish forking
            int status;
            pid_t result = waitpid(pid, &status, 0);
            
            if (result < 0) {
                last_error_ = "Failed to wait for child: " + std::string(strerror(errno));
                return false;
            }
            
            if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                last_error_ = "Child process failed to daemonize";
                return false;
            }
            
            return true;
        }
        
        // Child process - double fork to daemonize
        // First fork
        pid_t sid = setsid();
        if (sid < 0) {
            _exit(1);
        }
        
        // Second fork
        pid_t pid2 = fork();
        if (pid2 < 0) {
            _exit(1);
        }
        
        if (pid2 > 0) {
            // First child exits
            _exit(0);
        }
        
        // Grandchild (daemon) continues
        // Change working directory to root
        if (chdir("/") < 0) {
            _exit(1);
        }
        
        // Close standard file descriptors
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
        
        // Redirect to /dev/null
        open("/dev/null", O_RDONLY);  // stdin
        open("/dev/null", O_WRONLY);  // stdout
        open("/dev/null", O_WRONLY);  // stderr
        
        // Execute daemon
        execl(daemon_binary_path.c_str(), daemon_binary_path.c_str(), "--daemon", nullptr);
        
        // If we get here, exec failed
        _exit(1);
    }

    bool Daemonizer::wait_for_daemon(std::chrono::seconds timeout) {
        auto start = std::chrono::steady_clock::now();
        
        while (std::chrono::steady_clock::now() - start < timeout) {
            if (is_daemon_running()) {
                return true;
            }
            
            // Wait a bit before trying again
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        last_error_ = "Timeout waiting for daemon to start";
        return false;
    }

    std::string Daemonizer::get_executable_path() {
        char path[PATH_MAX];
        
        #ifdef __linux__
            ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
            if (len != -1) {
                path[len] = '\0';
                return std::string(path);
            }
        #elif __APPLE__
            uint32_t size = sizeof(path);
            if (_NSGetExecutablePath(path, &size) == 0) {
                return std::string(path);
            }
        #endif
        
        // Fallback - try to get from argv[0]
        return "";
    }

} // namespace wpmd
