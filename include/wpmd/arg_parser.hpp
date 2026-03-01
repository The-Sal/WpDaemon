/**
 * @file arg_parser.hpp
 * @brief Command-line argument parser for WpDaemon
 * 
 * Parses command-line arguments to determine run mode:
 * - --daemon / -d: Run as daemon (current behavior)
 * - --interactive / -i: Start interactive CLI
 * - (no args): Try to connect to existing daemon, start CLI if not found
 */

#pragma once

#include <string>
#include <vector>

namespace wpmd {

    /**
     * @brief Run modes for WpDaemon
     */
    enum class RunMode {
        DAEMON,      // Run as background daemon
        INTERACTIVE, // Start interactive CLI
        AUTO         // Auto-detect (default)
    };

    /**
     * @brief Parsed command-line arguments
     */
    struct ParsedArgs {
        RunMode mode = RunMode::AUTO;  // Default to auto-detect
        std::vector<std::string> positional_args;
        bool show_help = false;
        bool show_version = false;
        int port = 23888;              // Default port
    };

    /**
     * @brief Command-line argument parser
     * 
     * Parses argc/argv and returns structured arguments.
     * Supports:
     *   --daemon, -d     Run as daemon
     *   --interactive, -i  Start interactive CLI
     *   --help, -h       Show help
     *   --version, -v    Show version
     *   --port <port>    Set TCP port (default: 23888)
     */
    class ArgParser {
    public:
        /**
         * @brief Parse command-line arguments
         * 
         * @param argc Argument count
         * @param argv Argument values
         * @return ParsedArgs Parsed arguments structure
         */
        static ParsedArgs parse(int argc, char* argv[]);

        /**
         * @brief Get help message string
         * 
         * @return std::string Formatted help message
         */
        static std::string get_help_message();

        /**
         * @brief Get version string
         * 
         * @return std::string Version information
         */
        static std::string get_version_string();

    private:
        /**
         * @brief Convert mode to string representation
         * 
         * @param mode Run mode
         * @return std::string String representation
         */
        static std::string mode_to_string(RunMode mode);
    };

} // namespace wpmd
