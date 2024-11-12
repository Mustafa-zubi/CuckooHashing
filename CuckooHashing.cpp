#include "CuckooHashing.h"
#include <iostream>

using std::cout;
using std::endl;
using std::string;

CuckooHashing::CuckooHashing(int tableSize, int maxRehashLimit, int P) : tableSize(tableSize),
 maxRehashLimit(maxRehashLimit), P(P), randomNumGen(randomDevice()), dist(0, P - 1) 
 {
    T1.fill(0);
    T2.fill(0);
    
    aH1 = dist(randomNumGen);
    aH2 = dist(randomNumGen);
    bH1 = dist(randomNumGen);
    bH2 = dist(randomNumGen);

    cout << "aH1: " << aH1 << " aH2: " << aH2 << " bH1: " << bH1 << " bH2: " << bH2 << endl;
    std::cout << "Defining Cuckoohashing object" << std::endl;
}

CuckooHashing::~CuckooHashing() {
    std::cout << "CuckooHashing destructor called" << std::endl;
}

int CuckooHashing::hashFunc1(int key) {
    return ((aH1 * key + bH1) % P) % tableSize;
}

int CuckooHashing::hashFunc2(int key) {
    return ((aH2 * key + bH2) % P) % tableSize;
}

void CuckooHashing::print(string table, int index) {
    if (table == "T1") {
        cout << "Table 1: " << T1[index] << endl;
    } else if (table == "T2") {
        cout << "Table 2: " << T2[index] << endl;
    } else {
        cout << "Invalid table name" << endl;
    }

}
