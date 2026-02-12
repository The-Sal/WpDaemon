/**
 * @file command_handler.hpp
 * @brief Handles TCP commands for WireProxy daemon
 * 
 * This header defines the command handler that processes incoming TCP
 * commands from clients. It implements the 4 commands from the Python
 * daemon:
 * 
 * 1. spin_up:conf_name    - Start WireProxy with configuration
 * 2. spin_down:           - Stop running WireProxy
 * 3. state:               - Get current daemon state
 * 4. available_confs:     - List available configurations
 * 
 * Protocol format:
 *   Request:  CMD:ARG1,ARG2,ARG3...\n
 *   Response: {"CMD": "cmd", "result": {...}, "error": null}\n
 * 
 * The handler coordinates between:
 * - StateMachine: to validate state transitions
 * - ConfigManager: to validate configurations
 * - WireProxyProcess: to spawn/terminate processes
 * - LogManager: to create/finalize logs
 */

#pragma once

#include <string>
#include <functional>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>

// Forward declarations
namespace wpmd {
    class StateMachine;
    class ConfigManager;
    class BinaryManager;
    class LogManager;
    class WireProxyProcess;
}

namespace wpmd {

    /**
     * @brief Handles TCP commands and returns JSON responses
     * 
     * This class is the core of the daemon's API. It receives parsed
     * commands from the TCP server, executes them, and returns JSON
     * responses that match the Python daemon exactly.
     * 
     * Thread safety:
     * - All command handlers are synchronized via mutex
     */
    class CommandHandler {
    public:
        /**
         * @brief Destructor
         * 
         * Defined in .cpp file to allow proper cleanup of unique_ptr
         * with incomplete type.
         */
        ~CommandHandler();
        /**
         * @brief Constructs command handler with dependencies
         * 
         * @param state_machine Reference to state machine for lifecycle tracking
         * @param config_manager Reference to config manager for validation
         * @param binary_manager Reference to binary manager for paths
         * @param log_manager Reference to log manager for output capture
         */
        CommandHandler(StateMachine& state_machine,
                      ConfigManager& config_manager,
                      BinaryManager& binary_manager,
                      LogManager& log_manager);

        /**
         * @brief Executes a command and returns JSON response
         * 
         * This is the main entry point for command processing.
         * Parses the command string and dispatches to appropriate handler.
         * 
         * @param command Full command string (e.g., "spin_up:us-east.conf\n")
         * @return nlohmann::json JSON response matching Python format
         * 
         * Response format:
         * {
         *   "CMD": "echo of command name",
         *   "result": { ... } | null,
         *   "error": null | "error message"
         * }
         */
        nlohmann::json execute(const std::string& command);

    private:
        StateMachine& state_machine_;           ///< Reference to state machine
        ConfigManager& config_manager_;         ///< Reference to config manager
        BinaryManager& binary_manager_;         ///< Reference to binary manager
        LogManager& log_manager_;               ///< Reference to log manager
        std::unique_ptr<WireProxyProcess> process_;  ///< Current wireproxy process
        std::string current_config_;            ///< Name of current config file
        mutable std::mutex mutex_;              ///< Synchronizes command execution

        /**
         * @brief Handles spin_up command
         * 
         * Command: spin_up:conf_name
         * 
         * Validates:
         * - Not already running
         * - Config exists
         * 
         * Actions:
         * - Create log file
         * - Spawn wireproxy process
         * - Wait 500ms and verify alive
         * 
         * Response:
         *   result: {"status": "running", "config": "...", "pid": 12345, "log_file": "..."}
         *   error: "WireProxy is already running..." | "Configuration not found..." | "WireProxy failed to start..."
         * 
         * @param config_name Name of configuration to start
         * @return nlohmann::json Response object
         */
        nlohmann::json handle_spin_up(const std::string& config_name);

        /**
         * @brief Handles spin_down command
         * 
         * Command: spin_down:
         * 
         * Validates:
         * - Process is running
         * 
         * Actions:
         * - Write teardown header to log
         * - SIGTERM (wait 5s) → SIGKILL if needed
         * - Close log file
         * - Reset state
         * 
         * Response:
         *   result: {"status": "stopped", "previous_config": "...", "log_file": "..."}
         *   error: "WireProxy is not running"
         * 
         * @return nlohmann::json Response object
         */
        nlohmann::json handle_spin_down();

        /**
         * @brief Handles state command
         * 
         * Command: state:
         * 
         * Returns current state without modifying anything.
         * If process died since last check, auto-cleans state.
         * 
         * Response:
         *   result: {"running": true/false, "config": "...", "pid": 12345, "log_file": "..."}
         *   error: null
         * 
         * @return nlohmann::json Response object
         */
        nlohmann::json handle_state();

        /**
         * @brief Handles available_confs command
         * 
         * Command: available_confs:
         * 
         * Lists all .conf files in ~/.argus/wireproxy_confs/
         * 
         * Response:
         *   result: {"count": N, "configs": ["...", "..."]}
         *   error: null
         * 
         * @return nlohmann::json Response object
         */
        nlohmann::json handle_available_confs();

        /**
         * @brief Checks if current process is alive and cleans up if dead
         * 
         * Helper method called by state command to auto-detect
         * process death and clean up state.
         * 
         * @return true if process is alive
         * @return false if process is dead (state cleaned up)
         */
        bool check_and_cleanup_process();
    };

} // namespace wpmd
