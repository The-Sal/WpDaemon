/**
 * @file audit_logger.hpp
 * @brief Audit logging system for WpDaemon
 * 
 * Tracks all commands, state transitions, and actions performed by WpDaemon.
 * Logs are written to ~/.argus/wp-server-logs/audit.log
 */

#pragma once

#include <string>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>

namespace wpmd {

    /**
     * @brief Categories of audit log entries
     */
    enum class AuditCategory {
        CMD,        // Command received
        STATE,      // State machine transition
        ACTION,     // Action performed
        ERROR,      // Error occurred
        SUCCESS,    // Successful operation
        INFO        // General information
    };

    /**
     * @brief Thread-safe audit logger for WpDaemon
     * 
     * Records all significant events including:
     * - Commands received (with source)
     * - State transitions
     * - Process lifecycle events
     * - Errors and exceptions
     */
    class AuditLogger {
    public:
        /**
         * @brief Constructs audit logger
         * 
         * Initializes log directory at ~/.argus/wp-server-logs/
         * Opens audit.log in append mode
         */
        AuditLogger();
        
        /**
         * @brief Destructor - closes log file
         */
        ~AuditLogger();

        /**
         * @brief Log a command received
         * 
         * @param command The command string received
         * @param source Source of command (e.g., "127.0.0.1" or "interactive")
         */
        void log_command(const std::string& command, const std::string& source);

        /**
         * @brief Log a state transition
         * 
         * @param from_state Previous state name
         * @param to_state New state name
         */
        void log_state_transition(const std::string& from_state, const std::string& to_state);

        /**
         * @brief Log an action performed
         * 
         * @param action Description of the action
         * @param details Additional details (optional)
         */
        void log_action(const std::string& action, const std::string& details = "");

        /**
         * @brief Log an error
         * 
         * @param error Error message
         * @param context Context where error occurred
         */
        void log_error(const std::string& error, const std::string& context = "");

        /**
         * @brief Log a successful operation
         * 
         * @param operation Description of what succeeded
         * @param details Additional details (optional)
         */
        void log_success(const std::string& operation, const std::string& details = "");

        /**
         * @brief Log general information
         * 
         * @param message Information message
         */
        void log_info(const std::string& message);

        /**
         * @brief Get the path to the audit log file
         * 
         * @return std::filesystem::path Path to audit.log
         */
        [[nodiscard]] std::filesystem::path get_log_path() const { return audit_log_path_; }

        /**
         * @brief Get last N lines of the audit log
         * 
         * @param n Number of lines to retrieve
         * @return std::string The last N lines
         */
        [[nodiscard]] std::string get_last_lines(size_t n = 50) const;

    private:
        std::filesystem::path logs_dir_;
        std::filesystem::path audit_log_path_;
        std::ofstream log_file_;
        mutable std::mutex mutex_;

        /**
         * @brief Get current timestamp string
         * 
         * @return std::string Formatted timestamp [YYYY-MM-DD HH:MM:SS]
         */
        [[nodiscard]] static std::string get_timestamp();

        /**
         * @brief Get category string representation
         * 
         * @param category Audit category
         * @return std::string String representation (e.g., "[CMD]")
         */
        [[nodiscard]] static std::string category_to_string(AuditCategory category);

        /**
         * @brief Write a log entry
         * 
         * @param category Entry category
         * @param message Message to log
         */
        void write_log(AuditCategory category, const std::string& message);

        /**
         * @brief Ensure log file is open
         */
        void ensure_open();
    };

} // namespace wpmd
