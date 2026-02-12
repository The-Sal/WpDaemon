//
// Created by Sal Faris on 11/02/2026.
//

#include "thread"
#include <utility>
#include <functional>
#include "sockpp/tcp_acceptor.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using string = std::string;

class TCPServer {
public:
    int16_t port = 23888;
    sockpp::tcp_acceptor acceptor;
    std::function<json(string)> on_recv;

    explicit TCPServer(const std::function<json(string)>& callback) {
        const auto addr = sockpp::inet_address("localhost", port);
        if (const auto did_open = acceptor.open(addr, sockpp::tcp_acceptor::DFLT_QUE_SIZE, sockpp::tcp_acceptor::REUSE); !did_open) {
            std::cout << "Failed to open acceptor on port " << port << std::endl;
            exit(1);
        }

        acceptor.listen(port);
        if (!acceptor.is_open()) {
            std::cout << "Failed to open acceptor on port " << port << std::endl;
            exit(1);
        }
        on_recv = callback;
        std:: cout << "Listening on port " << port << std::endl;
    }

    [[noreturn]] void accept_forever() {
        while (true) {
            auto client = acceptor.accept();
            if (!client.is_ok()) {
                std::cout << "Failed to accept client" << std::endl;
            }

            auto client_value = client.release_or_throw();
            std::thread read_thread(&TCPServer::process_client, this, std::move(client_value));
            read_thread.detach(); // this would make it 'daemonic' at least the OS will clean it for us
        }
    }

private:
    // this will be a thread
    void process_client(sockpp::tcp_socket client) const {

        while (true) {
            char buffer[9999];
            if (auto read_result = client.recv(buffer, sizeof(buffer)); !read_result.is_ok()) {
                std::cout << "Failed to read from client" << std::endl;
                client.close();
                break;
            }

            auto response = on_recv(std::string(buffer));
            if (auto write_result = client.write(response.dump()); !write_result.is_ok()) {
                std::cout << "====================================" << std::endl;
                std::cout << "Failed to write to client" << std::endl;
                std::cout << response.dump() << std::endl;
                std::cout << write_result.error_message() << std::endl;
                std::cout << "====================================" << std::endl;
                break;
            }
        }
    }

};

