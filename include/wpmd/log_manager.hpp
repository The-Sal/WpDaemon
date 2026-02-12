/**
 * @file log_manager.hpp
 * @brief Manages WireProxy log files with structured headers/footers
 * 
 * This header provides functionality to:
 * - Create timestamped log files for WireProxy sessions
 * - Write structured headers with session metadata
 * - Write teardown footers with shutdown information
 * - Provide thread-safe access to log files
 * - Offer a stub interface for future log parsing capabilities
 * 
 * Each WireProxy session gets its own log file named:
 *   <timestamp>_<config_name>.log
 * 
 * Log format follows the Python implementation exactly:
 * - Header with start time, config name, wireproxy version
 * - Process output section
 * - Teardown footer with stop time, shutdown method
 */

#pragma once

#include <string>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <functional>

namespace wpmd {

    /**
     * @brief Stub structure for parsed log state
     * 
     * This structure will be used in the future for log parsing
     * to extract connection state, handshake info, etc.
     * For now, it serves as a placeholder.
     */
    struct LogState {
        bool connected = false;           ///< Whether WireGuard handshake completed
        std::string last_handshake;       ///< Timestamp of last handshake
        std::string endpoint;             ///< Connected endpoint
        uint64_t bytes_sent = 0;          ///< Bytes sent through tunnel
        uint64_t bytes_received = 0;      ///< Bytes received through tunnel
        std::string last_error;           ///< Last error message if any
    };

    /**
     * @brief Interface for log reading/parsing (stub for future implementation)
     * 
     * This interface defines the contract for log file reading and parsing.
     * Currently implemented as a stub - concrete implementation will be
     * added later when log parsing features are required.
     * 
     * Future implementation will:
     * - Watch log file for updates (using inotify/FSEvents)
     * - Parse WireProxy output for connection state
     * - Extract handshake information
     * - Notify listeners of state changes
     */
    class ILogReader {
    public:
        virtual ~ILogReader() = default;

        /**
         * @brief Parse a log file and extract current state
         * 
         * Stub: Currently returns empty/default LogState
         * Future: Will parse log file content and extract connection info
         * 
         * @param log_file_path Path to the log file to parse
         * @return LogState Parsed state information
         */
        virtual LogState parse_log_file(const std::filesystem::path& log_file_path) = 0;

        /**
         * @brief Get the last known state without re-parsing
         * 
         * Stub: Currently returns default LogState
         * Future: Will return cached state from last parse
         * 
         * @return LogState Last known state
         */
        virtual LogState get_last_state() const = 0;

        /**
         * @brief Register a callback for log updates
         * 
         * Stub: Currently does nothing with the callback
         * Future: Will register callback to be invoked when log file changes
         * 
         * @param callback Function to call when log is updated
         */
        virtual void on_log_update(std::function<void(const LogState&)> callback) = 0;
    };

    /**
     * @brief Stub implementation of ILogReader
     * 
     * This is a placeholder implementation that satisfies the interface
     * but does nothing. It will be replaced with a real implementation
     * when log parsing is needed.
     */
    class LogReaderStub : public ILogReader {
    public:
        /**
         * @brief Stub: Returns default/empty LogState
         */
        LogState parse_log_file(const std::filesystem::path& /* log_file_path */) override {
            return LogState{};  // Return default-initialized state
        }

        /**
         * @brief Stub: Returns default/empty LogState
         */
        LogState get_last_state() const override {
            return LogState{};  // Return default-initialized state
        }

        /**
         * @brief Stub: No-op callback registration
         */
        void on_log_update(std::function<void(const LogState&)> /* callback */) override {
            // Stub: Do nothing - callback will be used in future implementation
        }
    };

    /**
     * @brief Manages WireProxy log file lifecycle
     * 
     * This class handles creation, writing, and cleanup of WireProxy
     * session log files. It ensures thread-safe access to the log file
     * and writes structured headers/footers.
     * 
     * Usage:
     *   LogManager lm;
     *   lm.create_log("us-east.conf", wireproxy_version);
     *   lm.write_output("WireProxy stdout line\n");
     *   lm.finalize("Graceful termination");
     * 
     * Thread safety:
     * - All write operations are protected by mutex
     * - Log file handle is shared between LogManager and WireProxyProcess
     */
    class LogManager {
    public:
        /**
         * @brief Constructs LogManager with default paths
         * 
         * Sets up the logs directory path to ~/.argus/wp-server-logs/
         * Creates the directory if it doesn't exist.
         */
        LogManager();

        /**
         * @brief Creates a new log file with header
         * 
         * Creates a timestamped log file and writes the header section:
         * - Start time (human-readable and Unix timestamp)
         * - Configuration name
         * - WireProxy version
         * - Configuration file path
         * 
         * Log filename format: <timestamp>_<config_name>.log
         * 
         * @param config_name Name of the configuration (e.g., "us-east.conf")
         * @param wireproxy_version Version string from wireproxy -v
         * @return std::filesystem::path Path to the created log file
         * @throws std::runtime_error if log file cannot be created
         */
        std::filesystem::path create_log(const std::string& config_name, 
                                         const std::string& wireproxy_version);

        /**
         * @brief Gets the file handle for process output redirection
         * 
         * Returns a FILE* handle that can be used to redirect stdout/stderr
         * of the WireProxy process. The handle remains valid until finalize()
         * is called.
         * 
         * @return FILE* File handle for writing process output
         * @throws std::runtime_error if no log file is open
         */
        FILE* get_log_handle();

        /**
         * @brief Finalizes the log file with teardown footer
         * 
         * Writes the teardown section to the log file:
         * - Stop time (human-readable and Unix timestamp)
         * - Shutdown method (Graceful/Force kill)
         * - Final status
         * 
         * Then closes the file handle.
         * 
         * @param shutdown_method String describing how process was stopped
         */
        void finalize(const std::string& shutdown_method);

        /**
         * @brief Gets the path to the current log file
         * 
         * @return std::filesystem::path Path to current log file, or empty if none
         */
        [[nodiscard]] std::filesystem::path get_current_log_path() const { return current_log_path_; }

        /**
         * @brief Checks if a log file is currently open
         * 
         * @return true if log file is open
         * @return false if no log file is open
         */
        [[nodiscard]] bool is_log_open() const { return log_file_ != nullptr; }

        /**
         * @brief Gets the log reader interface (stub for future use)
         * 
         * @return ILogReader& Reference to the log reader
         */
        [[nodiscard]] ILogReader& get_log_reader() { return log_reader_; }

    private:
        std::filesystem::path logs_dir_;           ///< ~/.argus/wp-server-logs/
        std::filesystem::path current_log_path_;   ///< Path to current session log
        FILE* log_file_ = nullptr;                 ///< File handle for process output
        mutable std::mutex mutex_;                 ///< Protects log file access
        LogReaderStub log_reader_;                 ///< Stub log reader for future extension
    };

} // namespace wpmd
