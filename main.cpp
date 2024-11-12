#include "ZMQSubServer.h"
#include "CuckooHashing.h"

#include <iostream>

using namespace std;
using std::cout; 
using std::endl;
using std::string;
int bigPrimeNum = 2147483647;

int main() {
    ZMQSubServer server("tcp://localhost:5555");
    CuckooHashing ch(550, 100, bigPrimeNum);

    //test hash functions
    cout << "hashFunc1(10): " << ch.hashFunc1(10) << endl;
    cout << "hashFunc2(10): " << ch.hashFunc2(10) << endl;
    
    for (int i = 0; i < 100; i++) {
        string message = server.receiveMessage();
        cout << "Received: " << message << endl;
        // server.sendMessage("Hello from Server!");
    }
    // string message = server.receiveMessage();
    // server.sendMessage("recceived");
    return 0;
}
