#include "ZMQSubServer.h"
#include "CuckooHashing.h"

#include <iostream>

using namespace std;
using std::cout; 
using std::endl;
using std::string;

int main() {
    ZMQSubServer server("tcp://localhost:5555");
    CuckooHashing ch;
    for (int i = 0; i < 100; i++) {
        string message = server.receiveMessage();
        cout << "Received: " << message << endl;
        // server.sendMessage("Hello from Server!");
    }
    // string message = server.receiveMessage();
    // server.sendMessage("recceived");
    return 0;
}
