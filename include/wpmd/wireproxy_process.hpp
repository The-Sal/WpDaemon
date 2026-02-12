/**
 * @file wireproxy_process.hpp
 * @brief Manages WireProxy subprocess lifecycle
 * 
 * This header provides functionality to:
 * - Spawn WireProxy subprocess with configuration
 * - Monitor process status (alive/dead)
 * - Redirect stdout/stderr to log file
 * - Terminate process gracefully or forcefully
 * - Handle process cleanup on exit
 * 
 * The process manager works closely with:
 * - StateMachine: to track process lifecycle
 * - LogManager: to capture process output
 * - BinaryManager: to locate the wireproxy binary
 * 
 * Process lifecycle:
 * 1. spawn() - Fork and exec wireproxy with config
 * 2. is_alive() - Poll process status
 * 3. terminate() - SIGTERM with 5s timeout, then SIGKILL
 * 4. cleanup() - Close handles, reset state
 */

#pragma once

#include <string>
#include <filesystem>
#include <chrono>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>

// Forward declarations
namespace wpmd {
    class LogManager;
}

namespace wpmd {

    /**
     * @brief Manages a single WireProxy subprocess instance
     * 
     * This class encapsulates all process management logic:
     * - Process spawning (fork/exec)
     * - Status monitoring
     * - Graceful/forced termination
     * - Log file redirection
     * 
     * Thread safety:
     * - spawn() and terminate() should be called from the same thread
     * - is_alive() can be called from any thread
     * 
     * Example:
     *   WireProxyProcess wp(binary_path, log_manager);
     *   if (wp.spawn(config_path)) {
     *       // Process started
     *       if (wp.is_alive()) {
     *           // Process still running
     *       }
     *       wp.terminate();  // Graceful shutdown
     *   }
     */
    class WireProxyProcess {
    public:
        /**
         * @brief Construct process manager
         * 
         * @param binary_path Path to wireproxy executable
         * @param log_manager Reference to log manager for output capture
         */
        WireProxyProcess(std::filesystem::path binary_path, LogManager& log_manager);

        /**
         * @brief Destructor ensures process cleanup
         * 
         * If process is still running, force kills it.
         */
        ~WireProxyProcess();

        /**
         * @brief Spawns WireProxy subprocess
         * 
         * Forks and executes wireproxy with the specified configuration.
         * - Redirects stdout/stderr to log file via LogManager
         * - Sets up process group for clean termination
         * - Stores process ID for monitoring
         * 
         * @param config_path Path to WireGuard configuration file
         * @return true if spawn succeeded
         * @return false if spawn failed (fork/exec error)
         * 
         * @note After spawn(), call is_alive() after 500ms to verify startup
         */
        bool spawn(const std::filesystem::path& config_path);

        /**
         * @brief Checks if the process is still alive
         * 
         * Uses waitpid() with WNOHANG to poll process status.
         * Updates internal state if process has died.
         * 
         * @return true if process is running
         * @return false if process has terminated
         */
        bool is_alive() const;

        /**
         * @brief Terminates the process
         * 
         * Attempts graceful shutdown first:
         * 1. Send SIGTERM to process group
         * 2. Wait up to 5 seconds for termination
         * 3. If still alive, send SIGKILL
         * 
         * Always waits for process to fully terminate.
         * 
         * @return std::string "Graceful termination" or "Force killed"
         */
        std::string terminate();

        /**
         * @brief Gets the process ID
         * 
         * @return pid_t Process ID, or -1 if no process running
         */
        [[nodiscard]] pid_t get_pid() const { return pid_; }

        /**
         * @brief Gets the configuration path used for this process
         * 
         * @return std::filesystem::path Path to config file
         */
        [[nodiscard]] std::filesystem::path get_config_path() const { return config_path_; }

        /**
         * @brief Checks if a process is currently managed
         * 
         * @return true if spawn() was called and process may be running
         * @return false if no process has been spawned
         */
        [[nodiscard]] bool has_process() const { return pid_ != -1; }

        /**
         * @brief Checks if network drop was detected
         * 
         * @return true if network errors exceeded threshold
         */
        bool has_network_drop() const;

    private:
        std::filesystem::path binary_path_;      ///< Path to wireproxy executable
        std::filesystem::path config_path_;      ///< Path to config used for spawn
        LogManager& log_manager_;                ///< Reference for log file handle
        pid_t pid_ = -1;                         ///< Process ID (-1 = no process)
        mutable bool terminated_ = false;        ///< Flag to avoid double-cleanup
        mutable bool network_drop_detected_ = false;  ///< Network drop detection flag
        std::thread monitor_thread_;             ///< Background log monitoring thread
        mutable std::mutex monitor_mutex_;       ///< Protects network_drop_detected_
        std::atomic<bool> stop_monitoring_{false};  ///< Signal to stop monitor thread

        /**
         * @brief Internal cleanup after process termination
         * 
         * Closes log file handle and resets state.
         * Called by terminate() and destructor.
         */
        void cleanup();

        /**
         * @brief Monitors log file for network drop errors
         * 
         * Runs in background thread, tails the log file and detects:
         * - "network is unreachable"
         * - "can't assign requested address"
         * 
         * When consecutive error threshold is reached, auto-terminates process.
         */
        void monitor_log_for_network_errors();
    };

} // namespace wpmd
