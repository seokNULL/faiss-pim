CXX := g++
CXXFLAGS := -O2 -std=c++17 -fopenmp

# Where faiss is installed. Override if it is not under /usr/local.
FAISS_PREFIX ?= /usr/local

# libfaiss.a is a static archive and does not record its own dependencies, so
# BLAS and LAPACK have to be named explicitly and *after* -lfaiss.
# Override if the probe below picks the wrong one, e.g.
#   make BLAS_LIBS="-L$$CONDA_PREFIX/lib -lopenblas"
BLAS_LIBS ?= $(shell \
	if echo 'int main(){}' | $(CXX) -x c++ - -lopenblas -o /dev/null 2>/dev/null; then echo '-lopenblas'; \
	elif echo 'int main(){}' | $(CXX) -x c++ - -llapack -lblas -o /dev/null 2>/dev/null; then echo '-llapack -lblas'; \
	elif echo 'int main(){}' | $(CXX) -x c++ - -lmkl_rt -o /dev/null 2>/dev/null; then echo '-lmkl_rt'; \
	else echo 'BLAS_NOT_FOUND'; fi)

# Which faiss library to link. libfaiss.a is the generic build: it is compiled
# without -mavx2/-mpopcnt, so the Hamming kernels fall back to software
# popcount. A build with FAISS_OPT_LEVEL=avx2 or avx512 installs
# libfaiss_avx2.a / libfaiss_avx512.a alongside it, and those carry the
# vectorised kernels. The generic library stays generic either way, so this has
# to be switched by hand:
#   make FAISS_LIB=-lfaiss_avx512
FAISS_LIB ?= -lfaiss

INCLUDES := -I$(FAISS_PREFIX)/include
LDFLAGS := -L$(FAISS_PREFIX)/lib
LIBS := $(FAISS_LIB) $(BLAS_LIBS)

SRC := $(wildcard *.cpp)
TARGETS := $(patsubst %.cpp,%,$(SRC))

all: check-blas $(TARGETS)

check-blas:
ifeq ($(BLAS_LIBS),BLAS_NOT_FOUND)
	$(error No BLAS/LAPACK found. Install one (e.g. `sudo apt install libopenblas-dev liblapack-dev`) or set BLAS_LIBS explicitly, e.g. make BLAS_LIBS="-L$$CONDA_PREFIX/lib -lopenblas")
endif

%: %.cpp
	$(CXX) -o $@ $^ $(CXXFLAGS) $(INCLUDES) $(LDFLAGS) $(LIBS)

clean:
	rm -f $(TARGETS)

.PHONY: all clean check-blas
