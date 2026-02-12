/**
 * @file main.cpp
 * @brief Entry point for WireProxy daemon
 * 
 * This is the main entry point for the WireProxy C++ daemon.
 * It coordinates all components:
 * - BinaryManager: Ensures wireproxy binary is available
 * - StateMachine: Tracks daemon state
 * - ConfigManager: Manages WG configurations
 * - LogManager: Handles log file creation/writing
 * - CommandHandler: Processes TCP commands
 * - TCPServer: Listens for client connections
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
 * @brief Main entry point
 * 
 * Initializes all components and starts the TCP server.
 * 
 * @return int Exit code (0 for success)
 */
int main() {
    // Ignore SIGPIPE to prevent crashes when clients disconnect
    signal(SIGPIPE, SIG_IGN);
    
    // Set up signal handlers for graceful shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    std::cout << "========================================" << std::endl;
    std::cout << "WireProxy Daemon (WpDaemon)" << std::endl;
    std::cout << "========================================" << std::endl;
    
    try {
        // Initialize BinaryManager and ensure wireproxy is available
        BinaryManager binary_manager;
        
        std::cout << "Checking for wireproxy binary..." << std::endl;
        if (!binary_manager.ensure_binary_available()) {
            std::cerr << "ERROR: Failed to ensure wireproxy binary is available" << std::endl;
            return 1;
        }
        std::cout << "WireProxy binary ready at: " << binary_manager.get_binary_path() << std::endl;
        
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
        TCPServer server([&command_handler](const std::string& cmd) {
            return command_handler.execute(cmd);
        });
        
        // Store global pointer for signal handler
        g_server = &server;
        
        std::cout << "Daemon initialized successfully" << std::endl;
        std::cout << "========================================" << std::endl;
        
        // Start server (blocks forever)
        server.start();
        
    } catch (const std::exception& e) {
        std::cerr << "FATAL ERROR: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
