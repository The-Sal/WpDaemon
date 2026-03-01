//
// Created by opencode on 22/02/2026.
//

#include "wpmd/audit_logger.hpp"
#include <iostream>
#include <deque>

namespace wpmd {

    AuditLogger::AuditLogger() {
        // Set up logs directory
        const char* home = std::getenv("HOME");
        if (!home) {
            home = std::getenv("USERPROFILE");  // Windows fallback
        }
        
        if (home) {
            logs_dir_ = std::filesystem::path(home) / ".argus" / "wp-server-logs";
        } else {
            logs_dir_ = std::filesystem::temp_directory_path() / "wp-server-logs";
        }

        // Create directory if it doesn't exist
        std::filesystem::create_directories(logs_dir_);
        
        audit_log_path_ = logs_dir_ / "audit.log";
        
        // Open log file in append mode
        log_file_.open(audit_log_path_, std::ios::app);
        
        if (log_file_.is_open()) {
            write_log(AuditCategory::INFO, "Audit logger initialized");
        } else {
            std::cerr << "[AuditLogger] Failed to open audit log: " << audit_log_path_ << std::endl;
        }
    }

    AuditLogger::~AuditLogger() {
        if (log_file_.is_open()) {
            write_log(AuditCategory::INFO, "Audit logger shutting down");
            log_file_.close();
        }
    }

    void AuditLogger::log_command(const std::string& command, const std::string& source) {
        std::string message = "Command received: " + command;
        if (!source.empty()) {
            message += " from " + source;
        }
        write_log(AuditCategory::CMD, message);
    }

    void AuditLogger::log_state_transition(const std::string& from_state, const std::string& to_state) {
        write_log(AuditCategory::STATE, from_state + " -> " + to_state);
    }

    void AuditLogger::log_action(const std::string& action, const std::string& details) {
        std::string message = action;
        if (!details.empty()) {
            message += ": " + details;
        }
        write_log(AuditCategory::ACTION, message);
    }

    void AuditLogger::log_error(const std::string& error, const std::string& context) {
        std::string message = error;
        if (!context.empty()) {
            message = "[" + context + "] " + error;
        }
        write_log(AuditCategory::ERROR, message);
    }

    void AuditLogger::log_success(const std::string& operation, const std::string& details) {
        std::string message = operation;
        if (!details.empty()) {
            message += ": " + details;
        }
        write_log(AuditCategory::SUCCESS, message);
    }

    void AuditLogger::log_info(const std::string& message) {
        write_log(AuditCategory::INFO, message);
    }

    std::string AuditLogger::get_last_lines(size_t n) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!std::filesystem::exists(audit_log_path_)) {
            return "";
        }

        std::ifstream file(audit_log_path_);
        if (!file.is_open()) {
            return "";
        }

        std::deque<std::string> lines;
        std::string line;
        
        while (std::getline(file, line)) {
            lines.push_back(line);
            if (lines.size() > n) {
                lines.pop_front();
            }
        }
        
        file.close();
        
        std::string result;
        for (const auto& l : lines) {
            result += l + "\n";
        }
        
        return result;
    }

    std::string AuditLogger::get_timestamp() {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }

    std::string AuditLogger::category_to_string(AuditCategory category) {
        switch (category) {
            case AuditCategory::CMD:     return "[CMD]";
            case AuditCategory::STATE:   return "[STATE]";
            case AuditCategory::ACTION:  return "[ACTION]";
            case AuditCategory::ERROR:   return "[ERROR]";
            case AuditCategory::SUCCESS: return "[SUCCESS]";
            case AuditCategory::INFO:    return "[INFO]";
            default:                     return "[UNKNOWN]";
        }
    }

    void AuditLogger::write_log(AuditCategory category, const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        ensure_open();
        
        if (log_file_.is_open()) {
            log_file_ << "[" << get_timestamp() << "] "
                     << category_to_string(category) << " "
                     << message << std::endl;
            log_file_.flush();
        }
    }

    void AuditLogger::ensure_open() {
        if (!log_file_.is_open()) {
            log_file_.open(audit_log_path_, std::ios::app);
        }
    }

} // namespace wpmd
