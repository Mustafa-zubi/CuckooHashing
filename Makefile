# Compiler
CXX = g++

# Compiler flags
CXXFLAGS = -std=c++17 -O3

# Targets
all: benchmark_cuckoo_robin

# Build benchmark_cuckoo_robin: focused Cuckoo vs Robin Hood comparison.
benchmark_cuckoo_robin: benchmark_cuckoo_robin.o CuckooHashing.o RobinHoodHashing.o
	$(CXX) -v benchmark_cuckoo_robin.o CuckooHashing.o RobinHoodHashing.o -o benchmark_cuckoo_robin $(CXXFLAGS)

# Compile CuckooHashing
CuckooHashing.o: CuckooHashing.cpp
	$(CXX) -v $(CXXFLAGS) -c CuckooHashing.cpp

# Compile RobinHoodHashing
RobinHoodHashing.o: RobinHoodHashing.cpp
	$(CXX) -v $(CXXFLAGS) -c RobinHoodHashing.cpp

# Compile benchmark_cuckoo_robin.cpp
benchmark_cuckoo_robin.o: benchmark_cuckoo_robin.cpp
	$(CXX) -v $(CXXFLAGS) -c benchmark_cuckoo_robin.cpp

# Clean up build files
clean:
	rm -f *.o benchmark_cuckoo_robin
