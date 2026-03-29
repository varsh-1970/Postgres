#!/usr/bin/env bash
# install.sh — Install Rust + PostgreSQL dev libs on Ubuntu/Debian
# Run: bash install.sh

set -e

echo ""
echo "==> [1/3] Installing system dependencies..."
sudo apt update
sudo apt install -y \
    build-essential \
    pkg-config \
    libssl-dev \
    libpq-dev \
    curl

echo ""
echo "==> [2/3] Installing Rust via rustup..."
if command -v cargo &>/dev/null; then
    echo "    Rust already installed: $(cargo --version)"
else
    curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
    # Load cargo into current shell
    source "$HOME/.cargo/env"
fi

echo ""
echo "==> [3/3] Done! Rust $(rustc --version) is ready."
echo ""
echo "  Next steps:"
echo "    cd pgwebapp_rust"
echo "    cargo build --release"
echo "    ./target/release/pgwebapp 8080"
echo ""
