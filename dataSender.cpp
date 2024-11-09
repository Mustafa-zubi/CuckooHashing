#include <zmq.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include<chrono>
#include<thread>

int main() {
    void *context = zmq_ctx_new();                 
    void *socket = zmq_socket(context, ZMQ_PUB);   

    zmq_bind(socket, "tcp://*:5555");

    char buffer[256];
    for (int i = 0; i < 100; i++) {
        const char *msg = "Hello from Client!";
        zmq_send(socket, msg, strlen(msg), 0);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        // zmq_recv(socket, buffer, 255, 0);
        // printf("Received reply: %s\n", buffer);
    }
    
    zmq_close(socket);
    zmq_ctx_destroy(context);
    return 0;
}
