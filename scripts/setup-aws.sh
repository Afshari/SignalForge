#!/bin/bash
# setup.sh - AWS instance setup for SignalForge
# Run once on a fresh Ubuntu 24.04 instance with Tesla T4
# Usage: chmod +x scripts/setup.sh && ./scripts/setup.sh

set -e

# ---------------------------------------------------------------------------
# Pre-flight checks
# ---------------------------------------------------------------------------
echo "[INFO] Checking Ubuntu version..."
. /etc/os-release
if [[ "$ID" != "ubuntu" ]]; then
    echo "[ERROR] This script is intended for Ubuntu only."
    exit 1
fi
echo "[INFO] Running on Ubuntu $VERSION_ID"

echo "[INFO] Checking NVIDIA GPU..."
if ! command -v nvidia-smi &> /dev/null; then
    echo "[WARN] nvidia-smi not found - make sure NVIDIA drivers are installed."
else
    nvidia-smi
fi

# ---------------------------------------------------------------------------
# System packages
# ---------------------------------------------------------------------------
echo "[INFO] Updating system packages..."
sudo apt-get update
sudo apt-get upgrade -y

echo "[INFO] Installing build tools and utilities..."
sudo apt-get install -y \
    build-essential \
    cmake \
    git \
    wget \
    curl \
    vim \
    tmux \
    python3 \
    python3-pip \
    python3-venv \
    ca-certificates

# ---------------------------------------------------------------------------
# Docker (official repo - newer than Ubuntu default)
# ---------------------------------------------------------------------------
echo "[INFO] Installing Docker..."
sudo install -m 0755 -d /etc/apt/keyrings
sudo curl -fsSL https://download.docker.com/linux/ubuntu/gpg \
    -o /etc/apt/keyrings/docker.asc
sudo chmod a+r /etc/apt/keyrings/docker.asc

echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.asc] \
https://download.docker.com/linux/ubuntu \
$(. /etc/os-release && echo "$VERSION_CODENAME") stable" | \
sudo tee /etc/apt/sources.list.d/docker.list > /dev/null

sudo apt-get update
sudo apt-get install -y \
    docker-ce \
    docker-ce-cli \
    containerd.io \
    docker-compose-plugin \
    docker-buildx-plugin

sudo usermod -aG docker $USER

# ---------------------------------------------------------------------------
# NVIDIA Container Toolkit
# ---------------------------------------------------------------------------
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

# ---------------------------------------------------------------------------
# ulimit - file descriptor limits for large benchmark runs
# 262144 covers runs up to 20k+ files
# ---------------------------------------------------------------------------
echo "[INFO] Setting ulimit for large file runs..."
echo "ubuntu soft nofile 262144" | sudo tee -a /etc/security/limits.conf
echo "ubuntu hard nofile 262144" | sudo tee -a /etc/security/limits.conf
echo "ulimit -n 262144" | sudo tee /etc/profile.d/nofile.sh

# ---------------------------------------------------------------------------
# SSH key for GitHub (hint only)
# ---------------------------------------------------------------------------
echo ""
echo "[INFO] SSH key setup for GitHub:"
echo "  1. Generate a key: ssh-keygen -t ed25519 -C 'aws-signalforge'"
echo "  2. Copy the public key: cat ~/.ssh/id_ed25519.pub"
echo "  3. Add it to GitHub: Settings -> SSH and GPG keys -> New SSH key"
echo "  4. Test: ssh -T git@github.com"

# ---------------------------------------------------------------------------
# Verification
# ---------------------------------------------------------------------------
echo ""
echo "[INFO] Verifying installations..."
echo -n "  Docker:         "; docker --version
echo -n "  Docker Compose: "; docker compose version
echo -n "  Python3:        "; python3 --version
echo -n "  cmake:          "; cmake --version | head -1
nvidia-smi

echo ""
echo "[INFO] Setup complete!"
echo "[WARN] Log out and back in for docker group and ulimit changes to take effect."
echo "[INFO] Then run:"
echo "  git clone git@github.com:<your-username>/SignalForge.git"
echo "  cd SignalForge"
echo "  docker compose build"
echo "  docker compose up -d redis"
echo "  docker compose --profile shell run --rm shell"