CXX := g++
# CXXFLAGS := -g -fopenmp
CXXFLAGS := -fopenmp
LDFLAGS := -lfaiss


SRC := $(wildcard *.cpp)
TARGETS := $(patsubst %.cpp,%,$(SRC))

all: $(TARGETS)


%: %.cpp
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LDFLAGS)

clean:
	rm -f $(TARGETS)
