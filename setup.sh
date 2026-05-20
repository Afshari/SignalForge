#!/bin/bash
# setup.sh — AWS instance setup for SignalForge
# Run once on a fresh Ubuntu 24.04 instance with Tesla T4
# Usage: chmod +x setup.sh && ./setup.sh

set -e

echo "[INFO] Updating system packages..."
sudo apt-get update
sudo apt-get upgrade -y

echo "[INFO] Installing build tools..."
sudo apt-get install -y \
    build-essential \
    cmake \
    git \
    wget \
    python3 \
    python3-pip

echo "[INFO] Installing Docker..."
sudo apt-get install -y docker.io
sudo usermod -aG docker $USER

echo "[INFO] Installing NVIDIA Container Toolkit..."
curl -fsSL https://nvidia.github.io/libnvidia-container/gpgkey | \
    sudo gpg --dearmor -o /usr/share/keyrings/nvidia-container-toolkit-keyring.gpg

curl -s -L https://nvidia.github.io/libnvidia-container/stable/deb/nvidia-container-toolkit.list | \
    sed 's#deb https://#deb [signed-by=/usr/share/keyrings/nvidia-container-toolkit-keyring.gpg] https://#g' | \
    sudo tee /etc/apt/sources.list.d/nvidia-container-toolkit.list

sudo apt-get update
sudo apt-get install -y nvidia-container-toolkit
sudo nvidia-ctk runtime configure --runtime=docker
sudo systemctl restart docker

echo "[INFO] Verifying NVIDIA GPU..."
nvidia-smi

echo "[INFO] Verifying Docker..."
docker --version

echo ""
echo "[INFO] Setup complete!"
echo "[INFO] NOTE: Log out and back in for docker group changes to take effect."
echo "[INFO] Then run: docker build -t signalforge:latest ."