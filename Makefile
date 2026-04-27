# Compiler
CXX = g++

# Compiler flags
CXXFLAGS = -std=c++17

# Targets
all: benchmark

# Build benchmark
benchmark: benchmark.o CuckooHashing.o
	$(CXX) -v benchmark.o CuckooHashing.o -o benchmark $(CXXFLAGS)

# Compiling CuckooHashing 
CuckooHashing.o: CuckooHashing.cpp 
	$(CXX) -v $(CXXFLAGS) -c CuckooHashing.cpp

# Compile benchmark.cpp
benchmark.o: benchmark.cpp
	$(CXX) -v $(CXXFLAGS) -c benchmark.cpp

# Clean up build files
clean:
	rm -f *.o benchmark
