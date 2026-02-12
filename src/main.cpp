#include <iostream>
#include <csignal>
#include "binary_management.cpp"
#include "tcp_server.cpp"
#include "nlohmann/json.hpp"

using json = nlohmann::json;

json echo_fn(const std::string& data) {
    string cmd = data.substr(0, data.find(':'));
    string args = data.substr(data.find(':') + 1);
    return json{};
}

int main() {
    signal(SIGPIPE, SIG_IGN);

    auto manager = BinaryManager();
    auto server = TCPServer(echo_fn);
    server.accept_forever();
    return 0;
}