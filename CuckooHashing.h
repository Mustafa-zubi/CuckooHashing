#ifndef CUCKOOHASHING_H
#define CUCKOOHASHING_H
#include<array>

using std::array;
class CuckooHashing {
private:
   std::array <int, 100> table1, table2; // hashing tables
public:
    CuckooHashing();
    void insert(int key);
    void remove(int key);
    bool search(int key);
    void print();
    void printTables();
    void hashFunc1(int key, int& index, int& table);
    void hashFunc2(int key, int& index, int& table);
    ~CuckooHashing();
};
#endif
