# Compiler
CXX = g++

# Compiler flags
CXXFLAGS = -std=c++17 -lzmq

# Targets
all: main dataSender

# Build main
main: main.o ZMQSubServer.o CuckooHashing.o
	$(CXX) -v main.o ZMQSubServer.o CuckooHashing.o -o main $(CXXFLAGS)

# Build data sender
dataSender: dataSender.o
	$(CXX) -v dataSender.o -o dataSender $(CXXFLAGS)

# Compile main.cpp
main.o: main.cpp
	$(CXX) -v -c main.cpp

# Compile ZMQSubServer.cpp
ZMQSubServer.o: ZMQSubServer.cpp
	$(CXX) -v -c ZMQSubServer.cpp

# Compile dataSender
dataSender.o: dataSender.cpp
	$(CXX) -v -c dataSender.cpp

# Compiling CuckooHashing 
CuckooHashing.o: CuckooHashing.cpp 
	$(CXX) -v -c CuckooHashing.cpp

# Clean up build files
clean:
	rm -f *.o main dataSender

