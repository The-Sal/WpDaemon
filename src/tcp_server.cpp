//
// Created by opencode on 12/02/2026.
//

#include "wpmd/tcp_server.hpp"
#include <sockpp/tcp_acceptor.h>
#include <sockpp/tcp_socket.h>
#include <thread>
#include <iostream>

namespace wpmd {

    TCPServer::TCPServer(std::function<nlohmann::json(const std::string&)> command_handler)
        : on_recv_(std::move(command_handler))
        , acceptor_(std::make_unique<sockpp::tcp_acceptor>()) {}

    TCPServer::~TCPServer() {
        stop();
    }

    void TCPServer::start() {
        // Bind to localhost:23888
        sockpp::inet_address addr("127.0.0.1", port_);
        
        if (!acceptor_->open(addr, 5, sockpp::tcp_acceptor::REUSE)) {
            throw std::runtime_error("Failed to bind to port " + std::to_string(port_));
        }
        
        std::cout << "WireProxy Server listening on 127.0.0.1:" << port_ << std::endl;
        
        running_ = true;
        
        while (running_) {
            auto client = acceptor_->accept();
            
            if (!client.is_ok()) {
                if (running_) {
                    std::cerr << "Failed to accept client: " << client.error_message() << std::endl;
                }
                continue;
            }
            
            // Spawn detached thread for client
            auto client_value = client.release();
            std::thread client_thread(&TCPServer::process_client, this, std::move(client_value));
            client_thread.detach();
        }
    }

    void TCPServer::stop() {
        running_ = false;
        if (acceptor_ && acceptor_->is_open()) {
            acceptor_->close();
        }
    }

    void TCPServer::process_client(sockpp::tcp_socket client) {
        char buffer[4096];
        
        while (true) {
            // Receive data
            auto read_result = client.recv(buffer, sizeof(buffer) - 1);
            
            if (!read_result.is_ok()) {
                // Client disconnected or error
                break;
            }
            
            size_t bytes_read = read_result.value();
            if (bytes_read == 0) {
                // Client closed connection
                break;
            }
            
            // Null terminate
            buffer[bytes_read] = '\0';
            std::string data(buffer, bytes_read);
            
            // Parse command
            auto [command, success] = parse_command(data);
            
            nlohmann::json response;
            
            if (!success) {
                // Parsing error
                response = {
                    {"CMD", "unknown"},
                    {"result", nullptr},
                    {"error", "Newline not found"}
                };
            } else {
                // Process command
                response = on_recv_(command);
            }
            
            // Send response with newline termination
            std::string response_str = response.dump() + "\n";
            auto write_result = client.write(response_str);
            
            if (!write_result.is_ok()) {
                std::cerr << "Failed to send response: " << write_result.error_message() << std::endl;
                break;
            }
        }
        
        // Close client socket
        client.close();
    }

    std::pair<std::string, bool> TCPServer::parse_command(const std::string& data) const {
        // Look for newline
        size_t newline_pos = data.find('\n');
        if (newline_pos == std::string::npos) {
            return {"", false};
        }
        
        // Extract command up to newline
        std::string command = data.substr(0, newline_pos + 1);
        return {command, true};
    }

} // namespace wpmd
