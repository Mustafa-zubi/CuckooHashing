#ifndef ZMQSUBSERVER_H
#define ZMQSUBSERVER_H

#include <zmq.h>
#include <string>

class ZMQSubServer {
public:
    ZMQSubServer(const std::string& address);
    ~ZMQSubServer();
    void sendMessage(const std::string& message);
    std::string receiveMessage();

private:
    void* context;
    void* socket;   
};  

#endif
