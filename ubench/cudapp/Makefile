CXX = g++
CUDA_PATH = /usr/local/cuda
FAISS_PATH = /usr/local/faiss

CXXFLAGS = -std=c++17 -O2 -I$(FAISS_PATH)/include -I$(CUDA_PATH)/include
LDFLAGS = -L$(FAISS_PATH)/lib -L$(CUDA_PATH)/lib64 -lfaiss -lcublas -lcudart

SRC := $(wildcard *.cpp)
TARGETS := $(patsubst %.cpp,%,$(SRC))

all: $(TARGETS)


%: %.cpp
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LDFLAGS)

clean:
	rm -f $(TARGETS)
