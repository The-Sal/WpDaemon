/**
 * @file tcp_server.hpp
 * @brief TCP server wrapper for handling client connections
 * 
 * This header provides a TCP server that:
 * - Listens on localhost:23888
 * - Accepts client connections
 * - Parses incoming commands (CMD:ARGS\n format)
 * - Dispatches to CommandHandler
 * - Returns JSON responses
 * 
 * Uses sockpp library for socket operations and spawns
 * detached threads for each client connection.
 * 
 * Protocol details:
 * - Message format: CMD:ARG1,ARG2,...\n
 * - Response format: {"CMD": "...", "result": {...}, "error": null}\n
 * - Commands with no args still require colon: CMD:\n
 */

#pragma once

#include <string>
#include <functional>
#include <memory>
#include <atomic>
#include <sockpp/tcp_acceptor.h>
#include <sockpp/tcp_socket.h>
#include <nlohmann/json.hpp>

namespace wpmd {

    /**
     * @brief TCP server for WireProxy daemon
     * 
     * This class wraps sockpp's TCP acceptor and provides:
     * - Single-port listening on localhost
     * - Concurrent client handling via threads
     * - Protocol parsing (command extraction)
     * - JSON response serialization
     * 
     * Each client connection runs in a detached thread, allowing
     * concurrent command processing. The server runs forever
     * until the process is killed.
     * 
     * Usage:
     *   TCPServer server(command_handler);
     *   server.start();  // Blocks forever
     */
    class TCPServer {
    public:
        /**
         * @brief Constructor
         * 
         * @param command_handler Callback function that receives commands
         *                        and returns JSON responses
         */
        explicit TCPServer(std::function<nlohmann::json(const std::string&)> command_handler);

        /**
         * @brief Destructor
         * 
         * Closes the acceptor socket.
         */
        ~TCPServer();

        /**
         * @brief Starts the TCP server
         * 
         * Opens the acceptor on localhost:23888 and begins accepting
         * client connections. This method blocks forever.
         * 
         * @throws std::runtime_error if socket binding fails
         */
        void start();

        /**
         * @brief Stops the TCP server
         * 
         * Closes the acceptor socket, causing accept() to fail
         * and the server to shut down gracefully.
         */
        void stop();

        /**
         * @brief Gets the port number
         * 
         * @return int16_t Port number (always 23888)
         */
        [[nodiscard]] int16_t get_port() const { return port_; }

    private:
        int16_t port_ = 23888;                                    ///< TCP port to listen on
        std::unique_ptr<sockpp::tcp_acceptor> acceptor_;          ///< Socket acceptor
        std::function<nlohmann::json(const std::string&)> on_recv_;  ///< Command handler callback
        std::atomic<bool> running_{false};                        ///< Server running flag

        /**
         * @brief Processes a single client connection
         * 
         * Reads commands from client, dispatches to handler,
         * and sends back JSON responses. Runs in detached thread.
         * 
         * @param client The connected client socket
         */
        void process_client(sockpp::tcp_socket client);

        /**
         * @brief Parses incoming data into command string
         * 
         * Extracts the command up to and including newline.
         * Validates format (must contain colon and newline).
         * 
         * @param data Raw bytes received from client
         * @return std::pair<std::string, bool> (command, success)
         *         If parsing fails, returns ("", false)
         */
        std::pair<std::string, bool> parse_command(const std::string& data) const;
    };

} // namespace wpmd
