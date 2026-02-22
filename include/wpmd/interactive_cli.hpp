/**
 * @file interactive_cli.hpp
 * @brief Interactive CLI client for WpDaemon
 * 
 * Provides a user-friendly command-line interface that connects
 * to the WpDaemon TCP server and allows inspecting state,
 * controlling the daemon, and viewing audit logs.
 */

#pragma once

#include <string>
#include <functional>
#include <nlohmann/json.hpp>

namespace wpmd {

    /**
     * @brief TCP client for daemon communication
     */
    class DaemonClient {
    public:
        /**
         * @brief Construct client
         * 
         * @param host Host to connect to (default: 127.0.0.1)
         * @param port Port to connect to (default: 23888)
         */
        DaemonClient(const std::string& host = "127.0.0.1", int port = 23888);
        
        /**
         * @brief Destructor
         */
        ~DaemonClient();

        /**
         * @brief Check if daemon is running
         * 
         * @return true if daemon responds to connection
         * @return false if daemon is not running
         */
        bool is_daemon_running();

        /**
         * @brief Send command to daemon and get response
         * 
         * @param command Command string to send
         * @return nlohmann::json JSON response, or null on error
         */
        nlohmann::json send_command(const std::string& command);

        /**
         * @brief Get last error message
         * 
         * @return std::string Last error message
         */
        [[nodiscard]] std::string get_last_error() const { return last_error_; }

    private:
        std::string host_;
        int port_;
        std::string last_error_;
    };

    /**
     * @brief Interactive CLI for WpDaemon
     * 
     * Provides commands:
     * - status: Show daemon state
     * - configs: List available configurations
     * - start <config>: Start WireProxy
     * - stop: Stop WireProxy
     * - logs [n]: Show last n lines of audit log
     * - daemonize: Start daemon and detach
     * - help: Show commands
     * - quit/exit: Exit CLI
     */
    class InteractiveCLI {
    public:
        /**
         * @brief Construct CLI
         * 
         * @param host Daemon host
         * @param port Daemon port
         */
        InteractiveCLI(const std::string& host = "127.0.0.1", int port = 23888);

        /**
         * @brief Run the interactive CLI
         * 
         * Enters command loop until user exits
         */
        void run();

        /**
         * @brief Run single command (for testing)
         * 
         * @param command Command to execute
         * @return true if command succeeded
         * @return false if command failed
         */
        bool run_command(const std::string& command);

    private:
        DaemonClient client_;
        bool running_ = false;

        /**
         * @brief Print welcome message
         */
        void print_welcome();

        /**
         * @brief Print prompt
         */
        void print_prompt();

        /**
         * @brief Parse and execute command
         * 
         * @param input User input line
         * @return true to continue, false to exit
         */
        bool execute_command(const std::string& input);

        /**
         * @brief Handle status command
         */
        void cmd_status();

        /**
         * @brief Handle configs command
         */
        void cmd_configs();

        /**
         * @brief Handle start command
         * 
         * @param config Config name
         */
        void cmd_start(const std::string& config);

        /**
         * @brief Handle stop command
         */
        void cmd_stop();

        /**
         * @brief Handle logs command
         * 
         * @param args Arguments (number of lines)
         */
        void cmd_logs(const std::string& args);

        /**
         * @brief Handle daemonize command
         */
        void cmd_daemonize();

        /**
         * @brief Handle help command
         */
        void cmd_help();

        /**
         * @brief Trim whitespace from string
         */
        [[nodiscard]] static std::string trim(const std::string& str);

        /**
         * @brief Split string by delimiter
         */
        [[nodiscard]] static std::vector<std::string> split(const std::string& str, char delim);
    };

} // namespace wpmd
