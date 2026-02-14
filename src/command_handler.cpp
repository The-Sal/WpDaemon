//
// Created by opencode on 12/02/2026.
//

#include "wpmd/command_handler.hpp"
#include "wpmd/state_machine.hpp"
#include "wpmd/config_manager.hpp"
#include "wpmd/binary_manager.hpp"
#include "wpmd/log_manager.hpp"
#include "wpmd/wireproxy_process.hpp"
#include <sstream>
#include <algorithm>
#include <thread>
#include <chrono>
#include <iostream>

namespace wpmd {

    CommandHandler::CommandHandler(StateMachine& state_machine,
                                   ConfigManager& config_manager,
                                   BinaryManager& binary_manager,
                                   LogManager& log_manager)
        : state_machine_(state_machine)
        , config_manager_(config_manager)
        , binary_manager_(binary_manager)
        , log_manager_(log_manager)
        , process_(nullptr) {}

    CommandHandler::~CommandHandler() {
        // Destructor implementation - needed for unique_ptr with incomplete type
        // Process cleanup happens automatically via unique_ptr destruction
    }

    nlohmann::json CommandHandler::execute(const std::string& command) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Parse command and arguments
        // Format: CMD:ARG1,ARG2,...\n
        std::string cmd;
        std::vector<std::string> args;
        
        // Find the colon separator
        size_t colon_pos = command.find(':');
        if (colon_pos == std::string::npos) {
            // No colon found - error
            return {
                {"CMD", "unknown"},
                {"result", nullptr},
                {"error", "Parsing error: colon not found"}
            };
        }
        
        // Extract command name
        cmd = command.substr(0, colon_pos);
        
        // Extract arguments (everything after colon)
        if (colon_pos + 1 < command.length()) {
            std::string args_str = command.substr(colon_pos + 1);
            // Remove trailing newline if present
            if (!args_str.empty() && args_str.back() == '\n') {
                args_str.pop_back();
            }
            
            // Split by comma
            std::stringstream ss(args_str);
            std::string arg;
            while (std::getline(ss, arg, ',')) {
                // Trim whitespace
                arg.erase(0, arg.find_first_not_of(" \t"));
                arg.erase(arg.find_last_not_of(" \t") + 1);
                if (!arg.empty()) {
                    args.push_back(arg);
                }
            }
        }
        
        // Dispatch to appropriate handler
        try {
            if (cmd == "spin_up") {
                if (args.empty()) {
                    return {
                        {"CMD", cmd},
                        {"result", nullptr},
                        {"error", "Not enough args: spin_up requires config name"}
                    };
                }
                return handle_spin_up(args[0]);
            } else if (cmd == "spin_down") {
                return handle_spin_down();
            } else if (cmd == "state") {
                return handle_state();
            } else if (cmd == "available_confs") {
                return handle_available_confs();
            } else if (cmd == "whoami") {
                return handle_whoami();
            } else {
                return {
                    {"CMD", cmd},
                    {"result", nullptr},
                    {"error", "Unknown command: " + cmd}
                };
            }
        } catch (const std::exception& e) {
            return {
                {"CMD", cmd},
                {"result", nullptr},
                {"error", e.what()}
            };
        }
    }

    nlohmann::json CommandHandler::handle_spin_up(const std::string& config_name) {
        // Check current state
        if (state_machine_.get_state() != State::IDLE) {
            std::string error_msg = "WireProxy is already running";
            if (!current_config_.empty()) {
                error_msg += " with config: " + current_config_;
            }
            return {
                {"CMD", "spin_up"},
                {"result", nullptr},
                {"error", error_msg}
            };
        }
        
        // Normalize config name
        std::string normalized_config = ConfigManager::normalize_config_name(config_name);
        
        // Check if config exists
        if (!config_manager_.config_exists(normalized_config)) {
            return {
                {"CMD", "spin_up"},
                {"result", nullptr},
                {"error", "Configuration not found: " + normalized_config}
            };
        }
        
        // Transition to STARTING state
        if (!state_machine_.transition_to(State::STARTING)) {
            return {
                {"CMD", "spin_up"},
                {"result", nullptr},
                {"error", "Failed to transition to STARTING state"}
            };
        }
        
        try {
            // Get wireproxy version for log
            std::string version = binary_manager_.get_version();
            
            // Create log file
            std::filesystem::path log_path = log_manager_.create_log(normalized_config, version);
            
            // Create process manager
            process_ = std::make_unique<WireProxyProcess>(
                binary_manager_.get_binary_path(), 
                log_manager_
            );
            
            // Get config path
            std::filesystem::path config_path = config_manager_.get_config_path(normalized_config);
            
            // Spawn process
            if (!process_->spawn(config_path)) {
                log_manager_.finalize("Spawn failed");
                process_.reset();
                state_machine_.transition_to(State::IDLE);
                return {
                    {"CMD", "spin_up"},
                    {"result", nullptr},
                    {"error", "Failed to spawn WireProxy process"}
                };
            }
            
            // Wait 500ms and check if still alive (matching Python behavior)
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            
            if (!process_->is_alive()) {
                // Process died during startup
                std::filesystem::path failed_log = log_manager_.get_current_log_path();
                log_manager_.finalize("Process died during startup");
                process_.reset();
                state_machine_.transition_to(State::IDLE);
                current_config_.clear();
                
                return {
                    {"CMD", "spin_up"},
                    {"result", nullptr},
                    {"error", "WireProxy failed to start. Check log: " + failed_log.string()}
                };
            }
            
            // Success! Transition to RUNNING
            current_config_ = normalized_config;
            state_machine_.transition_to(State::RUNNING);
            
            return {
                {"CMD", "spin_up"},
                {"result", {
                    {"status", "running"},
                    {"config", normalized_config},
                    {"pid", process_->get_pid()},
                    {"log_file", log_path.string()}
                }},
                {"error", nullptr}
            };
            
        } catch (const std::exception& e) {
            // Cleanup on error
            if (log_manager_.is_log_open()) {
                log_manager_.finalize("Error during startup");
            }
            process_.reset();
            state_machine_.transition_to(State::IDLE);
            current_config_.clear();
            
            return {
                {"CMD", "spin_up"},
                {"result", nullptr},
                {"error", std::string("Exception during spin_up: ") + e.what()}
            };
        }
    }

    nlohmann::json CommandHandler::handle_spin_down() {
        // Check current state
        if (state_machine_.get_state() != State::RUNNING || !process_) {
            return {
                {"CMD", "spin_down"},
                {"result", nullptr},
                {"error", "WireProxy is not running"}
            };
        }
        
        // Transition to STOPPING state
        if (!state_machine_.transition_to(State::STOPPING)) {
            return {
                {"CMD", "spin_down"},
                {"result", nullptr},
                {"error", "Failed to transition to STOPPING state"}
            };
        }
        
        std::string prev_config = current_config_;
        std::filesystem::path log_path = log_manager_.get_current_log_path();
        
        try {
            // Terminate process
            std::string shutdown_method = process_->terminate();
            
            // Finalize log
            log_manager_.finalize(shutdown_method);
            
            // Cleanup
            process_.reset();
            current_config_.clear();
            state_machine_.transition_to(State::IDLE);
            
            return {
                {"CMD", "spin_down"},
                {"result", {
                    {"status", "stopped"},
                    {"previous_config", prev_config},
                    {"log_file", log_path.string()}
                }},
                {"error", nullptr}
            };
            
        } catch (const std::exception& e) {
            // Cleanup even on error
            process_.reset();
            current_config_.clear();
            state_machine_.transition_to(State::IDLE);
            
            return {
                {"CMD", "spin_down"},
                {"result", nullptr},
                {"error", std::string("Failed to stop WireProxy: ") + e.what()}
            };
        }
    }

    nlohmann::json CommandHandler::handle_state() {
        // Check if process died and cleanup if needed
        check_and_cleanup_process();
        
        State state = state_machine_.get_state();
        
        if (state == State::RUNNING && process_) {
            return {
                {"CMD", "state"},
                {"result", {
                    {"running", true},
                    {"config", current_config_},
                    {"pid", process_->get_pid()},
                    {"log_file", log_manager_.get_current_log_path().string()}
                }},
                {"error", nullptr}
            };
        } else {
            return {
                {"CMD", "state"},
                {"result", {
                    {"running", false},
                    {"config", nullptr},
                    {"pid", nullptr},
                    {"log_file", log_manager_.get_current_log_path().empty() ? 
                                  nullptr : 
                                  nlohmann::json(log_manager_.get_current_log_path().string())}
                }},
                {"error", nullptr}
            };
        }
    }

    nlohmann::json CommandHandler::handle_available_confs() {
        auto configs = config_manager_.list_configs();

        return {
            {"CMD", "available_confs"},
            {"result", {
                {"count", configs.size()},
                {"configs", configs}
            }},
            {"error", nullptr}
        };
    }

    nlohmann::json CommandHandler::handle_whoami() {
        return {
            {"CMD", "whoami"},
            {"result", {
                {"version", WPDAEMON_VERSION},
                {"implementation", "C++"}
            }},
            {"error", nullptr}
        };
    }

    bool CommandHandler::check_and_cleanup_process() {
        if (state_machine_.get_state() == State::RUNNING && process_) {
            if (!process_->is_alive()) {
                // Process died unexpectedly - check if it was due to network drop
                std::string termination_reason = "Process died unexpectedly";
                if (process_->has_network_drop()) {
                    termination_reason = "Network drop detected - auto-terminated";
                    std::cout << "[WpDaemon] " << termination_reason << std::endl;
                }
                
                log_manager_.finalize(termination_reason);
                process_.reset();
                current_config_.clear();
                state_machine_.transition_to(State::IDLE);
                return false;
            }
            return true;
        }
        return false;
    }

} // namespace wpmd
