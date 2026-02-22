/**
 * @file daemonizer.hpp
 * @brief Daemonization utilities for WpDaemon
 * 
 * Provides functionality to:
 * - Check if daemon is already running
 * - Spawn daemon process
 * - Wait for daemon to be ready
 * - Daemonize current process (Unix double-fork)
 */

#pragma once

#include <string>
#include <chrono>

namespace wpmd {

    /**
     * @brief Daemonization utilities
     * 
     * Handles spawning and monitoring the WpDaemon background process.
     */
    class Daemonizer {
    public:
        /**
         * @brief Construct daemonizer
         * 
         * @param host Host to check/connect (default: 127.0.0.1)
         * @param port Port to check/connect (default: 23888)
         */
        Daemonizer(const std::string& host = "127.0.0.1", int port = 23888);

        /**
         * @brief Check if daemon is already running on specified port
         * 
         * @return true if daemon responds
         * @return false if no daemon is running
         */
        bool is_daemon_running();

        /**
         * @brief Spawn daemon process
         * 
         * Forks and execs the daemon binary with --daemon flag.
         * The child process becomes a daemon via double-fork.
         * 
         * @param daemon_binary_path Path to the WpDaemon binary
         * @return true if spawn succeeded
         * @return false if spawn failed
         */
        bool spawn_daemon(const std::string& daemon_binary_path);

        /**
         * @brief Wait for daemon to become ready
         * 
         * Polls the daemon with whoami command until it responds
         * or timeout is reached.
         * 
         * @param timeout Maximum time to wait
         * @return true if daemon is ready
         * @return false if timeout reached
         */
        bool wait_for_daemon(std::chrono::seconds timeout = std::chrono::seconds(10));

        /**
         * @brief Get last error message
         * 
         * @return std::string Error description
         */
        [[nodiscard]] std::string get_last_error() const { return last_error_; }

        /**
         * @brief Get the binary path of current executable
         * 
         * @return std::string Path to current binary
         */
        static std::string get_executable_path();

    private:
        std::string host_;
        int port_;
        std::string last_error_;
    };

} // namespace wpmd
