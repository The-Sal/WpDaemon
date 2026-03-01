/**
 * @file main.cpp
 * @brief Entry point for WireProxy daemon
 * 
 * This is the main entry point for the WireProxy C++ daemon.
 * Supports multiple modes:
 * - Daemon mode (--daemon): Run as TCP server
 * - Interactive CLI mode (--interactive): Connect to daemon or start it
 * - Auto mode (default): Auto-detect based on daemon availability
 * 
 * Components:
 * - BinaryManager: Ensures wireproxy binary is available
 * - StateMachine: Tracks daemon state
 * - ConfigManager: Manages WG configurations
 * - LogManager: Handles log file creation/writing
 * - CommandHandler: Processes TCP commands
 * - TCPServer: Listens for client connections
 * - AuditLogger: Tracks all daemon actions
 * - InteractiveCLI: User-friendly CLI interface
 * - Daemonizer: Spawns daemon process
 * 
 * The daemon runs forever until killed with SIGTERM/SIGINT.
 */

#include <iostream>
#include <csignal>
#include <memory>

#include "wpmd/binary_manager.hpp"
#include "wpmd/state_machine.hpp"
#include "wpmd/config_manager.hpp"
#include "wpmd/log_manager.hpp"
#include "wpmd/command_handler.hpp"
#include "wpmd/tcp_server.hpp"
#include "wpmd/audit_logger.hpp"
#include "wpmd/arg_parser.hpp"
#include "wpmd/interactive_cli.hpp"
#include "wpmd/daemonizer.hpp"

using namespace wpmd;

/**
 * @brief Global pointer to TCP server for signal handling
 * 
 * This is a bit ugly but necessary for signal handlers which
 * must be plain functions. We use it to gracefully shut down
 * the server on SIGINT/SIGTERM.
 */
static TCPServer* g_server = nullptr;

/**
 * @brief Signal handler for graceful shutdown
 * 
 * Handles SIGINT (Ctrl+C) and SIGTERM to gracefully shut down
 * the TCP server and clean up resources.
 * 
 * @param sig Signal number
 */
void signal_handler(int sig) {
    std::cout << "\nReceived signal " << sig << ", shutting down..." << std::endl;
    if (g_server) {
        g_server->stop();
    }
    exit(0);
}

/**
 * @brief Run daemon mode
 * 
 * Initializes all components and starts the TCP server.
 * This is the traditional daemon behavior.
 * 
 * @return int Exit code (0 for success)
 */
int run_daemon_mode() {
    // Ignore SIGPIPE to prevent crashes when clients disconnect
    signal(SIGPIPE, SIG_IGN);
    
    // Set up signal handlers for graceful shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    std::cout << "========================================" << std::endl;
    std::cout << "WireProxy Daemon (WpDaemon)" << std::endl;
    std::cout << "========================================" << std::endl;
    
    try {
        // Initialize audit logger
        AuditLogger audit_logger;
        audit_logger.log_info("Starting daemon mode");
        
        // Initialize BinaryManager and ensure wireproxy is available
        BinaryManager binary_manager;
        
        std::cout << "Checking for wireproxy binary..." << std::endl;
        audit_logger.log_action("Checking wireproxy binary availability");
        
        if (!binary_manager.ensure_binary_available()) {
            std::cerr << "ERROR: Failed to ensure wireproxy binary is available" << std::endl;
            audit_logger.log_error("Failed to ensure wireproxy binary is available", "initialization");
            return 1;
        }
        
        std::cout << "WireProxy binary ready at: " << binary_manager.get_binary_path() << std::endl;
        audit_logger.log_success("WireProxy binary ready", binary_manager.get_binary_path().string());
        
        // Initialize other managers
        StateMachine state_machine;
        ConfigManager config_manager;
        LogManager log_manager;
        
        // Create command handler
        CommandHandler command_handler(
            state_machine,
            config_manager,
            binary_manager,
            log_manager
        );
        
        // Create TCP server with command handler callback
        TCPServer server([&command_handler, &audit_logger](const std::string& cmd) {
            // Log command
            audit_logger.log_command(cmd, "tcp_client");
            
            // Execute and return result
            auto result = command_handler.execute(cmd);
            
            // Log result
            if (result.contains("error") && !result["error"].is_null()) {
                audit_logger.log_error(result["error"], "command_execution");
            } else {
                audit_logger.log_success("Command executed successfully", cmd);
            }
            
            return result;
        });
        
        // Store global pointer for signal handler
        g_server = &server;
        
        std::cout << "Daemon initialized successfully" << std::endl;
        std::cout << "========================================" << std::endl;
        audit_logger.log_info("Daemon initialized and starting TCP server");
        
        // Start server (blocks forever)
        server.start();
        
    } catch (const std::exception& e) {
        std::cerr << "FATAL ERROR: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}

/**
 * @brief Run interactive CLI mode
 * 
 * Starts the interactive command-line interface.
 * Connects to existing daemon or prompts to start one.
 * 
 * @param host Daemon host
 * @param port Daemon port
 * @return int Exit code (0 for success)
 */
int run_interactive_mode(const std::string& host, int port) {
    // No signal handlers needed in CLI mode
    
    InteractiveCLI cli(host, port);
    cli.run();
    
    return 0;
}

/**
 * @brief Run auto mode
 * 
 * Tries to connect to existing daemon. If daemon is not running,
 * starts interactive CLI mode.
 * 
 * @param port Daemon port
 * @return int Exit code (0 for success)
 */
int run_auto_mode(int port) {
    Daemonizer daemonizer("127.0.0.1", port);
    
    std::cout << "Checking if daemon is running on port " << port << "..." << std::endl;
    
    if (daemonizer.is_daemon_running()) {
        std::cout << "Daemon is running. Connecting to interactive CLI..." << std::endl;
        return run_interactive_mode("127.0.0.1", port);
    } else {
        std::cout << "No daemon found on port " << port << "." << std::endl;
        std::cout << "Starting interactive CLI (daemon not running)." << std::endl;
        std::cout << "Use 'daemonize' command to start the daemon." << std::endl;
        std::cout << std::endl;
        return run_interactive_mode("127.0.0.1", port);
    }
}

/**
 * @brief Main entry point
 * 
 * Parses command-line arguments and dispatches to appropriate mode.
 * 
 * @param argc Argument count
 * @param argv Argument values
 * @return int Exit code (0 for success)
 */
int main(int argc, char* argv[]) {
    // Parse command-line arguments
    ParsedArgs args = ArgParser::parse(argc, argv);
    
    // Handle help
    if (args.show_help) {
        std::cout << ArgParser::get_help_message() << std::endl;
        return 0;
    }
    
    // Handle version
    if (args.show_version) {
        std::cout << ArgParser::get_version_string() << std::endl;
        return 0;
    }
    
    // Dispatch to appropriate mode
    switch (args.mode) {
        case RunMode::DAEMON:
            return run_daemon_mode();
            
        case RunMode::INTERACTIVE:
            return run_interactive_mode("127.0.0.1", args.port);
            
        case RunMode::AUTO:
        default:
            return run_auto_mode(args.port);
    }
}
