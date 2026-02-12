# WpDaemon

> **Lightweight C++ daemon for managing WireProxy instances in the Argus ecosystem**

[![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Linux-lightgrey.svg)](https://github.com/The-Sal/WpDaemon)
[![C++](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](https://en.cppreference.com/w/cpp/23)

WpDaemon is a C++ TCP server that manages [WireProxy](https://github.com/whyvl/wireproxy) instances for [Argus](https://github.com/The-Sal/Argus). It serves as a drop-in replacement for the original Python-based WireProxy daemon with **100% API compatibility** and a **97% reduction in memory footprint** (1.4-4.4 MB vs 106 MB).

---

## Table of Contents

- [Why WpDaemon?](#why-wpdaemon)
- [Role in the Argus Ecosystem](#role-in-the-argus-ecosystem)
- [Features](#features)
- [Architecture](#architecture)
- [Installation](#installation)
- [Usage](#usage)
- [Protocol Specification](#protocol-specification)
- [Development](#development)
- [Notes](#notes)

---

## Why WpDaemon?

### The Problem

The original Python implementation (`WireProxyServer`) had several issues:

| Issue | Description | Impact |
|-------|-------------|--------|
| [#65](https://github.com/The-Sal/Argus/issues/65) | **Memory footprint**: 106 MB for a simple daemon | Excessive resource usage on minimal systems |
| [#64](https://github.com/The-Sal/Argus/issues/64) | **Race condition**: Exception during binary download | Daemon crashes on first run |
| [#56](https://github.com/The-Sal/Argus/issues/56) | **Silent dead connections**: Network drops not detected | Stale websockets remain "connected" indefinitely |
| [#54](https://github.com/The-Sal/Argus/issues/54) | **Event detection deficiency**: No heartbeat monitoring | Binance/Polymarket connections silently fail |

### The Solution

WpDaemon is a **ground-up C++ rewrite** that:

**Reduces memory footprint by 97%** (1.4-4.4 MB on macOS)  
**Eliminates race conditions** with proper state machine transitions  
**Maintains 100% API compatibility** with Python implementation  
**Improves process management** using fork/exec and signal handling  
**Enables cross-platform deployment** as a single binary (no Python runtime)

---

## Role in the Argus Ecosystem

### What is Argus?

[**Argus**](https://github.com/The-Sal/Argus) is a high-performance financial market data system that provides unified access to multiple exchanges through a custom binary protocol (Protocol 2). It follows a **dispatcher paradigm** where server processes connect to financial APIs and multiplex real-time market data to multiple clients.

**Example Argus Architecture:**
```
Client 1 (Python) ──┐
Client 2 (C++)    ──┼──> TCP 9972 ──> [Polymarket Dispatcher] ──> CLOB (Central Limit Order Book)
Client 3 (Rust)   ──┘                        ↓
                                  Protocol 2 stream / Order Execution
                           (bid/ask/last/volume/20-100 levels of order book)
```

### WpDaemon's Role

WpDaemon sits **between Argus dispatchers and the internet**, providing transparent VPN routing via WireGuard:

```
┌─────────────────────────────────────────────────────────────────┐
│                    Argus Dispatcher (Python)                    │
│  PolymarketDispatcher connects to wss://ws.polymarket.com       │
└────────────────────┬────────────────────────────────────────────┘
                     │
                     │ Check .env: WIREPROXY_MAPPING_POLYMARKET=us-east
                     │ Call: start_proxy_aware_ws()
                     ▼
          ┌─────────────────────────┐
          │ argus.wireproxy.wrapper │
          │ TCP → 127.0.0.1:23888   │
          └──────────┬──────────────┘
                     │
                     │ CMD: "spin_up:us-east.conf\n"
                     ▼
┌─────────────────────────────────────────────────────────────────┐
│               WpDaemon (C++) - Port 23888                       │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │  • Validates configuration exists                        │   │
│  │  • Creates timestamped log file                          │   │
│  │  │  • Spawns: wireproxy -c ~/.argus/wireproxy_confs/...  │   │
│  └──────────────────────────────────────────────────────────┘   │
│                         │                                       │
│                         ▼                                       │
│              SOCKS5 Proxy: 127.0.0.1:25344                      │
└─────────────────────────┬───────────────────────────────────────┘
                          │
                          │ WireGuard VPN Tunnel
                          ▼
                   [US-East VPN Server]
                          │
                          ▼
              [Polymarket API]
```

**Key Responsibilities:**

1. **Binary Management**: Auto-detects platform (macOS/Linux, amd64/arm64) and downloads correct WireProxy binary
2. **Configuration Management**: Stores/retrieves WireGuard configs from `~/.argus/wireproxy_confs/`
3. **Process Lifecycle**: Spawns, monitors, and terminates WireProxy processes
4. **Logging**: Creates timestamped session logs with structured headers/teardown footers
5. **TCP API**: Exposes 4 commands (`spin_up`, `spin_down`, `state`, `available_confs`) with JSON responses

---

## Features

### Core Functionality

- **Lightweight**: 1.4-4.4 MB memory footprint (vs 106 MB Python)
- **Drop-in Replacement**: Identical TCP API, zero changes to Argus code
- **Cross-Platform**: macOS (amd64/arm64), Linux (amd64/arm64/aarch64)
- **Auto-Download**: Fetches correct WireProxy binary from [GitHub releases](https://github.com/whyvl/wireproxy/releases)
- **Thread-Safe**: Mutex-protected state machine and log writes
- **Structured Logging**: Timestamped session logs with headers/footers
- **Zero Dependencies**: Single binary, no Python runtime required

### Highlights

- **State Machine**: Enforces valid process lifecycle transitions (IDLE → STARTING → RUNNING → STOPPING)
- **Process Management**: Fork/exec with signal handling (SIGTERM → SIGKILL fallback)
- **Graceful Shutdown**: Proper cleanup on daemon exit or process crashes
- **Log Management**: Real-time line-buffered writes to `~/.argus/wp-server-logs/`

---

## Architecture

### Component Overview

```
┌─────────────────────────────────────────────────────────────┐
│  TCPServer (sockpp)  │  Handles connections, spawns threads │
└──────────────────────┬──────────────────────────────────────┘
                       │ std::string command
                       ▼
┌─────────────────────────────────────────────────────────────┐
│  CommandHandler      │  Parses commands, coordinates actions│
└──────┬───────────────┼───────────────┬──────────────────────┘
       │               │               │
       ▼               ▼               ▼
┌─────────────┐ ┌──────────────┐ ┌──────────────────────────┐
│ StateMachine│ │ ConfigManager│ │ WireProxyProcess         │
│ (lifecycle) │ │ (configs)    │ │ (process management)     │
└─────────────┘ └──────────────┘ └───────────┬──────────────┘
                                             │
                              ┌──────────────┴──────────────┐
                              ▼                             ▼
                    ┌──────────────────┐         ┌──────────────────┐
                    │ LogManager       │         │ BinaryManager    │
                    │ (log files)      │         │ (binary mgmt)    │
                    └──────────────────┘         └──────────────────┘
```

**See [ARCHITECTURE.md](ARCHITECTURE.md) for detailed design documentation.**

### Directory Structure

```
~/.argus/                          # Argus cache directory
├── wireproxy/
│   └── wireproxy                  # WireProxy binary (auto-downloaded)
├── wireproxy_confs/
│   ├── us-east.conf              # WireGuard configs
│   └── eu-west.conf
└── wp-server-logs/
    ├── 1739342640_us-east.log    # Session logs
    └── 1739344200_eu-west.log
```

---

## Installation

### Prerequisites

- **C++23** compiler (Clang 17+ or GCC 13+)
- **CMake** 3.15+
- **Git** (for cloning submodules)
- **tar** command (for extracting WireProxy archive)

### Build from Source

```bash
# Clone repository with submodules
git clone --recursive https://github.com/The-Sal/WpDaemon.git
cd WpDaemon

# Create build directory
mkdir build && cd build

# Configure
cmake ..

# Build
make WpDaemon -j$(nproc)

# Run
./WpDaemon
```

**Output:**
```
========================================
WireProxy Daemon (WpDaemon)
========================================
Checking for wireproxy binary...
Downloading WireProxy from https://github.com/whyvl/wireproxy/releases/latest/download/wireproxy_darwin_arm64.tar.gz
Moving wireproxy...
WireProxy binary ready at: /Users/user/.argus/wireproxy/wireproxy
Daemon initialized successfully
========================================
Listening on 127.0.0.1:23888
```

---

## Usage

### From Argus (Automatic)

Argus dispatchers automatically manage WpDaemon via the `argus.wireproxy` module. No manual intervention is needed.

**Example `.env` configuration:**
```bash
# Route Polymarket dispatcher through US-East VPN
WIREPROXY_MAPPING_POLYMARKET=us-east

# Route Binance dispatcher through EU-West VPN
WIREPROXY_MAPPING_BINANCE=eu-west
```

When you start a dispatcher:
```bash
python3 runtime.py polymarket --port 9972
```

Argus will:
1. Check if WpDaemon is running (port 23888)
2. Auto-start daemon if needed
3. Send `spin_up:us-east` command
4. Route all WebSocket traffic through SOCKS5 proxy on `127.0.0.1:25344`

### Manual Control (via netcat)

```bash
# Terminal 1: Start daemon
./WpDaemon

# Terminal 2: Send commands
echo "available_confs:" | nc localhost 23888
# {"CMD":"available_confs","result":{"count":2,"configs":["us-east.conf","eu-west.conf"]},"error":null}

echo "spin_up:us-east.conf" | nc localhost 23888
# {"CMD":"spin_up","result":{"status":"running","config":"us-east.conf","pid":12345,"log_file":"..."},"error":null}

echo "state:" | nc localhost 23888
# {"CMD":"state","result":{"running":true,"config":"us-east.conf","pid":12345,"log_file":"..."},"error":null}

echo "spin_down:" | nc localhost 23888
# {"CMD":"spin_down","result":{"status":"stopped","previous_config":"us-east.conf","log_file":"..."},"error":null}
```

### Via Argus CLI

```bash
# Add WireGuard configuration
python3 -m argus.wireproxy --add-conf ~/my-vpn.conf

# Start WireProxy server with config
python3 -m argus.wireproxy --start-server us-east

# Check status
python3 -m argus.wireproxy --server-status

# Stop server
python3 -m argus.wireproxy --stop-server
```

---

## Protocol Specification

### Request Format

```
CMD:ARG1,ARG2,...\n
```

**Required:**
- `CMD`: Command name
- `:`: Separator (always required, even with no args)
- `\n`: Newline terminator

### Response Format

```json
{
    "CMD": "echo of command received",
    "result": { ... } | null,
    "error": null | "error message string"
}
```

### Available Commands

| Command | Args | Description |
|---------|------|-------------|
| `spin_up` | `conf_name` | Start WireProxy with specified config |
| `spin_down` | *(none)* | Stop running WireProxy instance |
| `state` | *(none)* | Get current WireProxy status |
| `available_confs` | *(none)* | List available configurations |

### Examples

**Spin Up:**
```
Request:  spin_up:us-east.conf\n
Response: {
  "CMD": "spin_up",
  "result": {
    "status": "running",
    "config": "us-east.conf",
    "pid": 12345,
    "log_file": "/Users/user/.argus/wp-server-logs/1739342640_us-east.log"
  },
  "error": null
}
```

**State Check:**
```
Request:  state:\n
Response: {
  "CMD": "state",
  "result": {
    "running": true,
    "config": "us-east.conf",
    "pid": 12345,
    "log_file": "/Users/user/.argus/wp-server-logs/1739342640_us-east.log"
  },
  "error": null
}
```

**Error Response:**
```
Request:  spin_up:nonexistent.conf\n
Response: {
  "CMD": "spin_up",
  "result": null,
  "error": "Configuration not found: nonexistent.conf"
}
```

---

## Development

### Project Structure

```
WpDaemon/
├── CMakeLists.txt          # Build configuration
├── ARCHITECTURE.md         # Detailed architecture docs
├── README.md               # This file
├── include/
│   └── wpmd/              # All header files
│       ├── binary_manager.hpp
│       ├── command_handler.hpp
│       ├── config_manager.hpp
│       ├── log_manager.hpp
│       ├── state_machine.hpp
│       ├── tcp_server.hpp
│       ├── utils.hpp
│       └── wireproxy_process.hpp
├── src/                    # Implementation files
│   ├── main.cpp
│   ├── binary_manager.cpp
│   ├── command_handler.cpp
│   ├── config_manager.cpp
│   ├── log_manager.cpp
│   ├── state_machine.cpp
│   ├── tcp_server.cpp
│   └── wireproxy_process.cpp
└── vendor/                 # Third-party dependencies
    ├── json/              # nlohmann/json
    ├── libcpr/            # HTTP client library
    └── sockpp/            # TCP socket library
```

### Dependencies

| Library | Purpose | Version |
|---------|---------|---------|
| [sockpp](https://github.com/fpagliughi/sockpp) | TCP socket networking | Latest |
| [libcpr](https://github.com/libcpr/cpr) | HTTP client (libcurl wrapper) | Latest |
| [nlohmann/json](https://github.com/nlohmann/json) | JSON parsing/serialization | Latest |

All dependencies are vendored as Git submodules.

### Building with Debug Symbols

```bash
mkdir build-debug && cd build-debug
cmake -DCMAKE_BUILD_TYPE=Debug ..
make WpDaemon
```

### Running Tests

```bash
# Terminal 1: Start daemon
./WpDaemon

# Terminal 2: Run integration tests
./test_suite.sh
```

### Adding a New Command

1. **Add handler declaration** in `command_handler.hpp`:
```cpp
nlohmann::json handle_my_command(const std::string& arg);
```

2. **Add to execute()** in `command_handler.cpp`:
```cpp
} else if (cmd == "my_command") {
    return handle_my_command(args.empty() ? "" : args[0]);
}
```

3. **Implement handler**:
```cpp
nlohmann::json CommandHandler::handle_my_command(const std::string& arg) {
    // Validate state
    if (state_machine_.get_state() != State::RUNNING) {
        return {{"CMD", "my_command"}, {"result", nullptr}, 
                {"error", "Not running"}};
    }
    
    // Do work...
    
    return {{"CMD", "my_command"}, {"result", {...}}, {"error", nullptr}};
}
```

---

## Notes
- As of February 2026, WpDaemon can only be used with mainline (`runtime.py` or Python) Argus. `argus_server` (from argus-swift [branch](https://github.com/The-Sal/Argus/tree/argus-swift)) is not supported.
because it does not implement the WireProxy subsystem and is still behind [PR#70](https://github.com/The-Sal/Argus/pull/70) [PR#52](https://github.com/The-Sal/Argus/pull/52)
- WpDaemon is not part of the Argus CI/CD pipeline.


## Related Projects

- **[Argus](https://github.com/The-Sal/Argus)** - High-performance financial market data system
- **[WireProxy](https://github.com/whyvl/wireproxy)** – Userspace WireGuard implementation with SOCKS5 proxy
- **[oRoute](https://github.com/the-sal/oRoute)** – A routing optimiser on top of tailscale used by `build_system/`  

---

## Acknowledgments

- Built as part of the [Argus](https://github.com/The-Sal/Argus) ecosystem
- Created to solve [issues #65, #64, #56, #54](https://github.com/The-Sal/Argus/issues?q=is%3Aissue+is%3Aopen+label%3Abug) in Argus

---

**Status**: Ready for prod, not integrated directly into Argus; Argus still uses native daemon for now \
**Memory**: 1.4–4.4 MB (97% reduction vs. Python)\
**API**: 100% compatible with Python WireProxyServer
