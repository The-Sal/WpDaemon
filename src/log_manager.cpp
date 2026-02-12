//
// Created by opencode on 12/02/2026.
//

#include "wpmd/log_manager.hpp"
#include "wpmd/utils.hpp"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace wpmd {

    LogManager::LogManager() 
        : logs_dir_(get_argus_dir() / "wp-server-logs") {
        // Create logs directory if it doesn't exist
        if (!std::filesystem::exists(logs_dir_)) {
            std::filesystem::create_directories(logs_dir_);
        }
    }

    std::filesystem::path LogManager::create_log(const std::string& config_name, 
                                                 const std::string& wireproxy_version) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Close any existing log file
        if (log_file_) {
            fclose(log_file_);
            log_file_ = nullptr;
        }
        
        // Generate timestamp
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();
        
        // Create filename: <timestamp>_<config_name>.log
        std::string config_clean = config_name;
        // Remove .conf extension if present for cleaner filename
        if (config_clean.length() >= 5 && 
            config_clean.compare(config_clean.length() - 5, 5, ".conf") == 0) {
            config_clean = config_clean.substr(0, config_clean.length() - 5);
        }
        
        std::string filename = std::to_string(timestamp) + "_" + config_clean + ".log";
        current_log_path_ = logs_dir_ / filename;
        
        // Open log file with line buffering
        log_file_ = fopen(current_log_path_.c_str(), "w");
        if (!log_file_) {
            throw std::runtime_error("Failed to create log file: " + current_log_path_.string());
        }
        
        // Enable line buffering for real-time writes
        setvbuf(log_file_, nullptr, _IOLBF, BUFSIZ);
        
        // Write header
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        std::tm local_time = *std::localtime(&time_t_now);
        std::ostringstream time_stream;
        time_stream << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S");
        
        fprintf(log_file_, "================================================================================\n");
        fprintf(log_file_, "WireProxy Server Log\n");
        fprintf(log_file_, "================================================================================\n");
        fprintf(log_file_, "Start Time: %s\n", time_stream.str().c_str());
        fprintf(log_file_, "Unix Timestamp: %ld\n", timestamp);
        fprintf(log_file_, "Configuration: %s\n", config_name.c_str());
        fprintf(log_file_, "WireProxy Version: %s\n", wireproxy_version.c_str());
        fprintf(log_file_, "Configuration File: %s\n", 
                (get_argus_dir() / "wireproxy_confs" / config_name).c_str());
        fprintf(log_file_, "\nProcess Output:\n");
        fprintf(log_file_, "================================================================================\n");
        fflush(log_file_);
        
        return current_log_path_;
    }

    FILE* LogManager::get_log_handle() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!log_file_) {
            throw std::runtime_error("No log file is currently open");
        }
        return log_file_;
    }

    void LogManager::finalize(const std::string& shutdown_method) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!log_file_) {
            return;
        }
        
        // Write teardown footer
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();
        
        std::tm local_time = *std::localtime(&time_t_now);
        std::ostringstream time_stream;
        time_stream << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S");
        
        fprintf(log_file_, "\n================================================================================\n");
        fprintf(log_file_, "WireProxy Server Teardown\n");
        fprintf(log_file_, "================================================================================\n");
        fprintf(log_file_, "Stop Time: %s\n", time_stream.str().c_str());
        fprintf(log_file_, "Unix Timestamp: %ld\n", timestamp);
        fprintf(log_file_, "Status: Initiating shutdown\n");
        fprintf(log_file_, "Shutdown Method: %s\n", shutdown_method.c_str());
        fprintf(log_file_, "Final Status: Process terminated\n");
        fprintf(log_file_, "================================================================================\n");
        fprintf(log_file_, "End of log\n");
        fprintf(log_file_, "================================================================================\n");
        
        // Close the file
        fclose(log_file_);
        log_file_ = nullptr;
    }

} // namespace wpmd
