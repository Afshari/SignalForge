FROM nvidia/cuda:12.0.0-devel-ubuntu22.04

ENV DEBIAN_FRONTEND=noninteractive

# Build tools
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    wget \
    python3 \
    python3-pip \
    python3-venv \
    vim \
    && rm -rf /var/lib/apt/lists/*

# Boost 1.91 from source — Boost.JSON only
RUN wget https://archives.boost.io/release/1.91.0/source/boost_1_91_0.tar.gz && \
    tar -xzf boost_1_91_0.tar.gz && \
    cd boost_1_91_0 && \
    ./bootstrap.sh --prefix=/usr/local --with-libraries=json && \
    ./b2 install -j$(nproc) && \
    cd .. && \
    rm -rf boost_1_91_0 boost_1_91_0.tar.gz

# hiredis from source - provides CMake config files
RUN git clone https://github.com/redis/hiredis.git && \
    cd hiredis && \
    mkdir build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_SSL=OFF && \
    make -j$(nproc) && \
    make install && \
    ldconfig && \
    cd ../.. && \
    rm -rf hiredis

# Copy requirements and install
COPY requirements.txt .
RUN pip3 install -r requirements.txt

# Copy source
WORKDIR /app
COPY . .

# Build
RUN mkdir -p build && cd build && \
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CUDA_ARCHITECTURES=75 \
        -DBOOST_ROOT=/usr/local \
        -DCMAKE_PREFIX_PATH=/usr/local && \
    cmake --build . --config Release -j$(nproc)
    
# Python venv setup
RUN cd /app && \
    python3 -m venv .venv && \
    .venv/bin/pip install -r requirements.txt

# Entry point
ENTRYPOINT ["/app/x64/Release/SignalForge"]
CMD []