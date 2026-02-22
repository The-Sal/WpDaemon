//
// Created by opencode on 22/02/2026.
//

#include "wpmd/interactive_cli.hpp"
#include "wpmd/daemonizer.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <fstream>
#include <deque>
#include <filesystem>
#include <sockpp/tcp_connector.h>
#include <sockpp/tcp_socket.h>
#include <sockpp/inet_address.h>

namespace wpmd {

    //=========================================================================
    // DaemonClient Implementation
    //=========================================================================

    DaemonClient::DaemonClient(const std::string& host, int port)
        : host_(host)
        , port_(port) {}

    DaemonClient::~DaemonClient() = default;

    bool DaemonClient::is_daemon_running() {
        try {
            sockpp::tcp_connector conn;
            sockpp::inet_address addr(host_, port_);
            
            // Try to connect with short timeout
            if (!conn.connect(addr, std::chrono::milliseconds(1000))) {
                return false;
            }
            
            // Try to send whoami command
            std::string cmd = "whoami:\n";
            auto result = conn.write(cmd);
            if (!result.is_ok()) {
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

    nlohmann::json DaemonClient::send_command(const std::string& command) {
        try {
            sockpp::tcp_connector conn;
            sockpp::inet_address addr(host_, port_);
            
            if (!conn.connect(addr, std::chrono::milliseconds(2000))) {
                last_error_ = "Failed to connect to daemon";
                return nullptr;
            }
            
            // Ensure command ends with newline
            std::string cmd = command;
            if (cmd.empty() || cmd.back() != '\n') {
                cmd += '\n';
            }
            
            // Send command
            auto write_result = conn.write(cmd);
            if (!write_result.is_ok()) {
                last_error_ = "Failed to send command: " + write_result.error_message();
                conn.close();
                return nullptr;
            }
            
            // Set read timeout and read response
            conn.read_timeout(std::chrono::milliseconds(5000));
            char buffer[4096];
            auto read_result = conn.read(buffer, sizeof(buffer) - 1);
            
            if (!read_result.is_ok()) {
                last_error_ = "Failed to read response: " + read_result.error_message();
                conn.close();
                return nullptr;
            }
            
            size_t bytes_read = read_result.value();
            if (bytes_read == 0) {
                last_error_ = "No response from daemon";
                conn.close();
                return nullptr;
            }
            
            buffer[bytes_read] = '\0';
            std::string response_str(buffer);
            
            conn.close();
            
            // Parse JSON response
            try {
                return nlohmann::json::parse(response_str);
            } catch (const std::exception& e) {
                last_error_ = std::string("Failed to parse response: ") + e.what();
                return nullptr;
            }
            
        } catch (const std::exception& e) {
            last_error_ = std::string("Exception: ") + e.what();
            return nullptr;
        }
    }

    //=========================================================================
    // InteractiveCLI Implementation
    //=========================================================================

    InteractiveCLI::InteractiveCLI(const std::string& host, int port)
        : client_(host, port) {}

    void InteractiveCLI::run() {
        print_welcome();
        
        running_ = true;
        std::string input;
        
        while (running_) {
            print_prompt();
            
            if (!std::getline(std::cin, input)) {
                // EOF received
                std::cout << std::endl;
                break;
            }
            
            if (!execute_command(input)) {
                break;
            }
        }
        
        std::cout << "Goodbye!" << std::endl;
    }

    bool InteractiveCLI::run_command(const std::string& command) {
        return execute_command(command);
    }

    void InteractiveCLI::print_welcome() {
        std::cout << "========================================" << std::endl;
        std::cout << "  WireProxy Daemon (WpDaemon) CLI" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << std::endl;
        
        // Check if daemon is running
        if (client_.is_daemon_running()) {
            std::cout << "Connected to daemon on port 23888" << std::endl;
            
            // Get status
            auto response = client_.send_command("state:");
            if (response != nullptr && !response.contains("error")) {
                auto result = response["result"];
                if (result["running"]) {
                    std::cout << "Status: Running with config: " << result["config"] << std::endl;
                } else {
                    std::cout << "Status: Idle (no process running)" << std::endl;
                }
            }
        } else {
            std::cout << "WARNING: Daemon is not running!" << std::endl;
            std::cout << "Use 'daemonize' command to start the daemon." << std::endl;
        }
        
        std::cout << std::endl;
        std::cout << "Type 'help' for available commands." << std::endl;
        std::cout << std::endl;
    }

    void InteractiveCLI::print_prompt() {
        std::cout << "wpd> ";
        std::cout.flush();
    }

    bool InteractiveCLI::execute_command(const std::string& input) {
        std::string trimmed = trim(input);
        
        if (trimmed.empty()) {
            return true;
        }
        
        auto parts = split(trimmed, ' ');
        if (parts.empty()) {
            return true;
        }
        
        std::string cmd = parts[0];
        std::string args;
        if (parts.size() > 1) {
            size_t space_pos = trimmed.find(' ');
            if (space_pos != std::string::npos) {
                args = trim(trimmed.substr(space_pos + 1));
            }
        }
        
        if (cmd == "quit" || cmd == "exit") {
            return false;
        } else if (cmd == "status") {
            cmd_status();
        } else if (cmd == "configs") {
            cmd_configs();
        } else if (cmd == "start") {
            if (args.empty()) {
                std::cout << "Usage: start <config_name>" << std::endl;
            } else {
                cmd_start(args);
            }
        } else if (cmd == "stop") {
            cmd_stop();
        } else if (cmd == "logs") {
            cmd_logs(args);
        } else if (cmd == "daemonize") {
            cmd_daemonize();
        } else if (cmd == "help") {
            cmd_help();
        } else {
            std::cout << "Unknown command: " << cmd << std::endl;
            std::cout << "Type 'help' for available commands." << std::endl;
        }
        
        return true;
    }

    void InteractiveCLI::cmd_status() {
        if (!client_.is_daemon_running()) {
            std::cout << "Daemon is not running!" << std::endl;
            return;
        }
        
        auto response = client_.send_command("state:");
        if (response == nullptr) {
            std::cout << "Error: " << client_.get_last_error() << std::endl;
            return;
        }
        
        if (response.contains("error") && !response["error"].is_null()) {
            std::cout << "Error: " << response["error"] << std::endl;
            return;
        }
        
        auto result = response["result"];
        
        std::cout << "Daemon Status:" << std::endl;
        std::cout << "  Running: " << (result["running"] ? "Yes" : "No") << std::endl;
        
        if (result["running"]) {
            std::cout << "  Config: " << result["config"] << std::endl;
            std::cout << "  PID: " << result["pid"] << std::endl;
            if (!result["log_file"].is_null()) {
                std::cout << "  Log file: " << result["log_file"] << std::endl;
            }
        }
    }

    void InteractiveCLI::cmd_configs() {
        if (!client_.is_daemon_running()) {
            std::cout << "Daemon is not running!" << std::endl;
            return;
        }
        
        auto response = client_.send_command("available_confs:");
        if (response == nullptr) {
            std::cout << "Error: " << client_.get_last_error() << std::endl;
            return;
        }
        
        if (response.contains("error") && !response["error"].is_null()) {
            std::cout << "Error: " << response["error"] << std::endl;
            return;
        }
        
        auto result = response["result"];
        int count = result["count"];
        
        std::cout << "Available configurations (" << count << "):" << std::endl;
        
        if (count == 0) {
            std::cout << "  (none)" << std::endl;
        } else {
            for (const auto& config : result["configs"]) {
                std::cout << "  - " << config << std::endl;
            }
        }
    }

    void InteractiveCLI::cmd_start(const std::string& config) {
        if (!client_.is_daemon_running()) {
            std::cout << "Daemon is not running! Use 'daemonize' to start it." << std::endl;
            return;
        }
        
        std::cout << "Starting WireProxy with config: " << config << "..." << std::endl;
        
        auto response = client_.send_command("spin_up:" + config);
        if (response == nullptr) {
            std::cout << "Error: " << client_.get_last_error() << std::endl;
            return;
        }
        
        if (response.contains("error") && !response["error"].is_null()) {
            std::cout << "Error: " << response["error"] << std::endl;
            return;
        }
        
        auto result = response["result"];
        std::cout << "Success! WireProxy is running." << std::endl;
        std::cout << "  Config: " << result["config"] << std::endl;
        std::cout << "  PID: " << result["pid"] << std::endl;
        std::cout << "  Log: " << result["log_file"] << std::endl;
    }

    void InteractiveCLI::cmd_stop() {
        if (!client_.is_daemon_running()) {
            std::cout << "Daemon is not running!" << std::endl;
            return;
        }
        
        std::cout << "Stopping WireProxy..." << std::endl;
        
        auto response = client_.send_command("spin_down:");
        if (response == nullptr) {
            std::cout << "Error: " << client_.get_last_error() << std::endl;
            return;
        }
        
        if (response.contains("error") && !response["error"].is_null()) {
            std::cout << "Error: " << response["error"] << std::endl;
            return;
        }
        
        auto result = response["result"];
        std::cout << "Success! WireProxy stopped." << std::endl;
        std::cout << "  Previous config: " << result["previous_config"] << std::endl;
        std::cout << "  Log file: " << result["log_file"] << std::endl;
    }

    void InteractiveCLI::cmd_logs(const std::string& args) {
        // For now, just show audit log from file
        std::filesystem::path audit_log;
        const char* home = std::getenv("HOME");
        if (home) {
            audit_log = std::filesystem::path(home) / ".argus" / "wp-server-logs" / "audit.log";
        } else {
            audit_log = std::filesystem::temp_directory_path() / "wp-server-logs" / "audit.log";
        }
        
        if (!std::filesystem::exists(audit_log)) {
            std::cout << "No audit log found." << std::endl;
            return;
        }
        
        // Parse number of lines
        size_t num_lines = 50;
        if (!args.empty()) {
            try {
                num_lines = std::stoul(args);
            } catch (...) {
                // Use default
            }
        }
        
        // Read last N lines
        std::ifstream file(audit_log);
        if (!file.is_open()) {
            std::cout << "Failed to open audit log." << std::endl;
            return;
        }
        
        std::deque<std::string> lines;
        std::string line;
        while (std::getline(file, line)) {
            lines.push_back(line);
            if (lines.size() > num_lines) {
                lines.pop_front();
            }
        }
        file.close();
        
        std::cout << "Last " << lines.size() << " lines of audit log:" << std::endl;
        std::cout << "----------------------------------------" << std::endl;
        for (const auto& l : lines) {
            std::cout << l << std::endl;
        }
    }

    void InteractiveCLI::cmd_daemonize() {
        // Check if daemon is already running
        if (client_.is_daemon_running()) {
            std::cout << "Daemon is already running!" << std::endl;
            return;
        }

        std::cout << "Starting daemon..." << std::endl;

        // Create daemonizer and spawn daemon
        Daemonizer daemonizer;
        std::string binary_path = Daemonizer::get_executable_path();

        if (binary_path.empty()) {
            // Fallback to argv[0] or just "WpDaemon"
            binary_path = "./WpDaemon";
        }

        std::cout << "Spawning daemon from: " << binary_path << std::endl;

        if (!daemonizer.spawn_daemon(binary_path)) {
            std::cout << "ERROR: Failed to spawn daemon: " << daemonizer.get_last_error() << std::endl;
            return;
        }

        std::cout << "Daemon spawned. Waiting for it to be ready..." << std::endl;

        // Wait for daemon to be ready
        if (!daemonizer.wait_for_daemon(std::chrono::seconds(10))) {
            std::cout << "ERROR: " << daemonizer.get_last_error() << std::endl;
            std::cout << "Daemon may have failed to start. Check logs for details." << std::endl;
            return;
        }

        std::cout << "SUCCESS! Daemon is now running." << std::endl;
        std::cout << "You can now use 'status' to check the daemon state." << std::endl;
    }

    void InteractiveCLI::cmd_help() {
        std::cout << "Available commands:" << std::endl;
        std::cout << std::endl;
        std::cout << "  status               Show daemon status" << std::endl;
        std::cout << "  configs              List available WireGuard configurations" << std::endl;
        std::cout << "  start <config>       Start WireProxy with specified config" << std::endl;
        std::cout << "  stop                 Stop running WireProxy" << std::endl;
        std::cout << "  logs [n]             Show last n lines of audit log (default: 50)" << std::endl;
        std::cout << "  daemonize            Start daemon and detach (coming soon)" << std::endl;
        std::cout << "  help                 Show this help" << std::endl;
        std::cout << "  quit, exit           Exit interactive mode" << std::endl;
        std::cout << std::endl;
        std::cout << "Note: Most commands require the daemon to be running." << std::endl;
        std::cout << "      Start the daemon first with: ./WpDaemon --daemon" << std::endl;
    }

    std::string InteractiveCLI::trim(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\n\r");
        if (first == std::string::npos) {
            return "";
        }
        size_t last = str.find_last_not_of(" \t\n\r");
        return str.substr(first, last - first + 1);
    }

    std::vector<std::string> InteractiveCLI::split(const std::string& str, char delim) {
        std::vector<std::string> parts;
        std::stringstream ss(str);
        std::string part;
        while (std::getline(ss, part, delim)) {
            if (!part.empty()) {
                parts.push_back(part);
            }
        }
        return parts;
    }

} // namespace wpmd
