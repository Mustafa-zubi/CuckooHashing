# A Study of Evaluating Cuckoo Hashing vs. Robin Hood Hashing

In this project we present two hashing techniques, namely Cuckoo Hashing and Robin Hood Hashing, it focus more on implementing Cuckoo Hashing and evaluating its performance with Robin Hood Hashing. A pipeline that descries the whole process is shown in the figure below: 

![Project pipeline chart](docs/pipeline.png)

## Prerequisites

- C++17 (The project is developed using this version, but it should be compatible with other C++ versions)
- C++ libzmq https://github.com/zeromq/zmqpp
- More will be added as required while developing the code 

## Working environment: 
HW: A node consists of two **Intel Xeon Gold 5220R** processors, each with **24 cores** (48 threading cores) and a base frequency of **2.2GHz**. The system is equipped with **96 GB of RAM** and two **240GB SSDs** local disks. However, for storage, instead of using the local disks, we relied on a GPFS (General Parallel File System) mounted on the server. The GPFS provided a storage capacity of 0.5 Peta Byte (PB), with a bandwidth of **100 Gbps** and a read/write performance of **5 GB** per second. The computing node runs Red Hat Enterprise Linux (RHEL) release 8.9. 

## Installation 
After installing all prerequisites: 
- Clone this repo
- make
- make install **(not ready yet)**

## Uninstalling 

To uninstall this code from your system: 
-  make clean or make distclean
-  make uninstall (not ready yet)

## How to run: 

After compiling and with reference to the pipeline above, the idea is to send data in real-time using ZMQ to both Cuckoo Hashing and Robin Hood. For this, we primarily need to run two software applications:

- main (./main): Responsible for preparing Cuckoo Hashing and Robin Hood to receive data and build their hash tables.
- dataSender (./dataSender): Responsible for retrieving data from the Marsaglia CD-ROM and streaming it to both hashing algorithms.
