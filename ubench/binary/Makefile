CXX := g++
CXXFLAGS := -O2 -std=c++17 -fopenmp
LDFLAGS := -lfaiss

SRC := $(wildcard *.cpp)
TARGETS := $(patsubst %.cpp,%,$(SRC))

all: $(TARGETS)

%: %.cpp
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LDFLAGS)

clean:
	rm -f $(TARGETS)

.PHONY: all clean
