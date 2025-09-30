CXX=g++
CXXFLAGS=-std=c++17 -O2 -Wall -Wextra -Isrc `pkg-config --cflags libcurl wiringPi`
LDFLAGS=`pkg-config --libs libcurl wiringPi`
SRC=$(shell find src -name '*.cpp')
OBJ=$(SRC:.cpp=.o)

all: monitor

monitor: $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(OBJ) monitor
