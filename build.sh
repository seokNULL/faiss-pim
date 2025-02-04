#!/bin/bash

set -e  # Exit immediately if an error occurs

BUILD_DIR="build_debug"

# Configure CMake
echo "[INFO] Configuring CMake..."
cmake -B $BUILD_DIR \
      -DBUILD_TESTING=ON \
      -DBUILD_SHARED_LIBS=ON \
      -DFAISS_ENABLE_GPU=OFF \
      -DFAISS_ENABLE_RAFT=OFF \
      -DFAISS_OPT_LEVEL=generic \
      -DFAISS_ENABLE_C_API=ON \
      -DPYTHON_EXECUTABLE=$(which python) \
      -DCMAKE_BUILD_TYPE=Debug

echo "[INFO] CMake configuration completed."

# Build Faiss
echo "[INFO] Building Faiss..."
make -C $BUILD_DIR -j faiss
echo "[INFO] Build completed."
# Install Faiss (system-wide, requires sudo)
sudo -v
sudo make -C $BUILD_DIR install

# Build Python bindings
echo "[INFO] Building Python bindings..."
cd $BUILD_DIR/faiss/python && python setup.py build
echo "[INFO] Python bindings build completed."
