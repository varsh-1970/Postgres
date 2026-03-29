# pgwebapp — PostgreSQL Web Studio (Rust)

A single-binary web application written in **Rust** that connects to any
PostgreSQL database and lets you run SQL queries from your browser.

```
Browser  ──HTTP──▶  Rust HTTP Server (tiny_http)
                         │
                   postgres crate (sync libpq wrapper)
                         │
                    PostgreSQL
```

---

## Tech Stack

| Layer | Crate | Purpose |
|-------|-------|---------|
| HTTP server | `tiny_http 0.12` | Embedded HTTP/1.1, one thread per request |
| PostgreSQL | `postgres 0.19` | Sync client wrapping libpq |
| JSON | `serde_json 1` | Serialize query results |
| URL decode | `urlencoding 2` | Parse form-encoded bodies |
| Logging | `env_logger 0.11` | RUST_LOG=info for debug output |

The HTML/CSS/JS frontend is **embedded in the binary** at compile time via
`include_str!("index.html")` — no static files needed at runtime.

---

## Step-by-Step Setup on Ubuntu VM

### Step 1 — Install dependencies (one time)

```bash
# Fastest way — use the included install script
bash install.sh
```

This installs: `build-essential`, `pkg-config`, `libssl-dev`, `libpq-dev`,
`curl`, and Rust via rustup.

Or manually:
```bash
sudo apt update
sudo apt install -y build-essential pkg-config libssl-dev libpq-dev curl
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
source ~/.cargo/env
```

---

### Step 2 — Verify Rust installed

```bash
rustc --version    # rustc 1.7x.x
cargo --version    # cargo 1.7x.x
```

---

### Step 3 — Build

```bash
cd pgwebapp_rust

# Debug build (faster compile, slower binary)
cargo build

# Release build (optimised, smaller binary — recommended for production)
cargo build --release
```

First build downloads ~10 crates and may take 1–2 minutes.
Subsequent builds are incremental and much faster.

---

### Step 4 — Run

```bash
# Default port 8080
./target/release/pgwebapp

# Custom port
./target/release/pgwebapp 9090
```

Output:
```
  ╔══════════════════════════════════════════╗
  ║   pgwebapp Rust — PostgreSQL Studio      ║
  ╠══════════════════════════════════════════╣
  ║  Server : http://localhost:8080          ║
  ║  Backend: Rust + postgres crate          ║
  ║  Press Ctrl+C to stop                   ║
  ╚══════════════════════════════════════════╝
```

---

### Step 5 — Open in browser (from your Windows host)

```
http://your_vm_ip:8080
```

Fill in the connection form and click **Connect**.

---

## Keep Running After SSH Disconnect

**Option A — nohup:**
```bash
nohup ./target/release/pgwebapp 8080 > pgwebapp.log 2>&1 &
echo "PID: $!"
```

**Option B — systemd service:**
```bash
sudo nano /etc/systemd/system/pgwebapp.service
```
```ini
[Unit]
Description=pgwebapp Rust PostgreSQL Studio
After=network.target

[Service]
Type=simple
User=dbsupport
WorkingDirectory=/home/dbsupport/pgwebapp_rust
ExecStart=/home/dbsupport/pgwebapp_rust/target/release/pgwebapp 8080
Restart=on-failure

[Install]
WantedBy=multi-user.target
```
```bash
sudo systemctl daemon-reload
sudo systemctl enable --now pgwebapp
sudo systemctl status pgwebapp
```

---

## API Endpoints

| Method | Path | Description |
|--------|------|-------------|
| GET | `/` | Serves embedded web UI |
| POST | `/api/connect` | Connect to PostgreSQL |
| POST | `/api/disconnect` | Disconnect |
| POST | `/api/query` | Execute SQL |
| GET | `/api/status` | Connection status |

---

## Project Structure

```
pgwebapp_rust/
├── Cargo.toml          ← dependencies
├── install.sh          ← setup script
├── README.md
└── src/
    ├── main.rs         ← HTTP server, routing, entry point
    ├── db.rs           ← PostgreSQL connection & query logic
    ├── handlers.rs     ← route handler functions
    └── index.html      ← web UI (embedded via include_str!)
```

---

## Comparison: C vs C++ vs Rust backends

| Feature | C | C++ | Rust |
|---------|---|-----|------|
| Memory safety | Manual | Manual (RAII) | Compiler-enforced |
| Thread safety | pthread mutex | std::mutex | Arc + Mutex (compile-time checked) |
| JSON building | Manual snprintf | Json::Object class | serde_json macros |
| Error handling | Return codes | exceptions | Result<T, E> |
| HTML embedding | embed_html tool | include_str equivalent | `include_str!()` built-in |
| Binary size | ~120 KB | ~200 KB | ~8–12 MB (static) |
| Compile time | < 5 sec | ~10 sec | ~60 sec (first build) |

---

## Troubleshooting

| Problem | Fix |
|---------|-----|
| `pkg-config not found` | `sudo apt install pkg-config` |
| `libpq-fe.h not found` | `sudo apt install libpq-dev` |
| `cannot find -lpq` | `sudo apt install libpq-dev` |
| `error: linker cc not found` | `sudo apt install build-essential` |
| `Blocking on downloads...` | Your VM needs internet access for `cargo build` |
| Port in use | `./target/release/pgwebapp 9090` |
| Cannot connect from Windows | `sudo ufw allow 8080/tcp` + open cloud firewall port |

**Enable debug logging:**
```bash
RUST_LOG=debug ./target/release/pgwebapp 8080
```
