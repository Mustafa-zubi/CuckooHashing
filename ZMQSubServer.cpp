#include "ZMQSubServer.h"
#include <iostream>
#include <cstring>

// to avoid any missunderstading, this is a ZMQ subscription client
// but acting as data receiver server. Thats why it is called ZMQSubServer.

ZMQSubServer::ZMQSubServer(const std::string& address) {
    context = zmq_ctx_new();
    if (!context) {
        std::cerr << "Failed to create ZeroMQ context" << std::endl;
        throw std::runtime_error("Failed to create ZeroMQ context");
    }
    socket = zmq_socket(context, ZMQ_SUB);
    if (!socket) {
        zmq_ctx_destroy(context);
        std::cerr << "Failed to create ZeroMQ socket" << std::endl;
        throw std::runtime_error("Failed to create ZeroMQ socket");
    }

    // Bind the socket to the specified address
    if (zmq_connect(socket, address.c_str()) != 0) {
        zmq_close(socket);
        zmq_ctx_destroy(context);
        std::cerr << "Failed to bind ZeroMQ socket" << std::endl;
        throw std::runtime_error("Failed to bind ZeroMQ socket");
    }

    zmq_setsockopt(socket, ZMQ_SUBSCRIBE, "", 0);
}

ZMQSubServer::~ZMQSubServer() {
    zmq_close(socket);
    zmq_ctx_destroy(context);
}

std::string ZMQSubServer::receiveMessage() {
    char buffer[256];
    std::cout << "Waiting for message..." << std::endl;
    int bytesReceived = zmq_recv(socket, buffer, sizeof(buffer) - 1, 0);

    if (bytesReceived == -1) {
        std::cerr << "Failed to receive message" << std::endl;
        return "";
    }
    buffer[bytesReceived] = '\0'; // Null-terminate the received string
    std::cout << "Received: " << buffer << std::endl;
    return std::string(buffer);
}

void ZMQSubServer::sendMessage(const std::string& message) {
    zmq_send(socket, message.c_str(), message.size(), 0);
    std::cout << "Sent: " << message << std::endl;
}
