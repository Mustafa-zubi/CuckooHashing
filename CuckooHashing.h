#ifndef CUCKOOHASHING_H
#define CUCKOOHASHING_H
#include<array>
#include <random>

using std::array;
using std::string;
using std::mt19937; // random number generator
using std::random_device; // for generating random numbers
using std::uniform_int_distribution; // for generating random numbers

class CuckooHashing {
private:
   /* 
   hashing tables size: each should be 10% 20% larger than half number of the elements 
   as replorted in the paper
   */ 
   
   array <int, 550> T1, T2; 
   int maxRehashLimit; // maximum number of rehashes allowed
   const int P;  // _AN: check this with Dr. Ibrahim 
   const int tableSize;
   random_device randomDevice;
   mt19937 randomNumGen;
   uniform_int_distribution<int> dist;

   int aH1, aH2, bH1, bH2;


public:
    CuckooHashing(int tableSize, int maxRehashLimit, int P);
    void insert(int key);
    void remove(int key);
    bool search(int key);
    void print(string table, int index);
    void printTables();
    int hashFunc1(int key);
    int hashFunc2(int key);
    ~CuckooHashing();
};
#endif
