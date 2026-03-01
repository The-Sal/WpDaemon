//
// Created by opencode on 22/02/2026.
//

#include "wpmd/arg_parser.hpp"
#include <iostream>
#include <cstring>

namespace wpmd {

    ParsedArgs ArgParser::parse(int argc, char* argv[]) {
        ParsedArgs args;
        
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            
            if (arg == "--daemon" || arg == "-d") {
                args.mode = RunMode::DAEMON;
            } else if (arg == "--interactive" || arg == "-i") {
                args.mode = RunMode::INTERACTIVE;
            } else if (arg == "--help" || arg == "-h") {
                args.show_help = true;
            } else if (arg == "--version" || arg == "-v") {
                args.show_version = true;
            } else if (arg == "--port" || arg == "-p") {
                if (i + 1 < argc) {
                    try {
                        args.port = std::stoi(argv[++i]);
                    } catch (...) {
                        std::cerr << "Warning: Invalid port number, using default 23888" << std::endl;
                        args.port = 23888;
                    }
                }
            } else if (arg[0] == '-') {
                std::cerr << "Warning: Unknown option: " << arg << std::endl;
            } else {
                args.positional_args.push_back(arg);
            }
        }
        
        return args;
    }

    std::string ArgParser::get_help_message() {
        return R"(WireProxy Daemon (WpDaemon)

Usage: WpDaemon [OPTIONS]

Options:
  -d, --daemon         Run as background daemon (binds to TCP port)
  -i, --interactive    Start interactive CLI mode
  -p, --port <port>    Set TCP port (default: 23888)
  -h, --help           Show this help message
  -v, --version        Show version information

Modes:
  (no args)            Auto-detect: connect to existing daemon or start CLI
  --daemon             Run as daemon server only
  --interactive        Start interactive CLI client

Interactive CLI Commands:
  status               Show daemon status
  configs              List available WireGuard configurations
  start <config>       Start WireProxy with specified config
  stop                 Stop running WireProxy
  logs [n]             Show last n lines of audit log (default: 50)
  daemonize            Start daemon and detach (spawns background process)
  help                 Show CLI commands
  quit, exit           Exit interactive mode

Examples:
  WpDaemon                     # Auto mode - try to connect or start CLI
  WpDaemon --daemon            # Run as daemon
  WpDaemon --interactive       # Start interactive CLI
  WpDaemon --port 12345        # Use custom port
)";
    }

    std::string ArgParser::get_version_string() {
        return std::string("WpDaemon version ") + WPDAEMON_VERSION + " (C++)";
    }

    std::string ArgParser::mode_to_string(RunMode mode) {
        switch (mode) {
            case RunMode::DAEMON:      return "daemon";
            case RunMode::INTERACTIVE: return "interactive";
            case RunMode::AUTO:        return "auto";
            default:                   return "unknown";
        }
    }

} // namespace wpmd
