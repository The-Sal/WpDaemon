# WireProxy Daemon (WpDaemon) Architecture

This document provides a comprehensive architectural overview of the WireProxy C++ daemon, designed for developers who want to understand, modify, or extend the codebase.

## Table of Contents

1. [Overview](#overview)
2. [Project Structure](#project-structure)
3. [Architecture Overview](#architecture-overview)
4. [Component Details](#component-details)
5. [Data Flow](#data-flow)
6. [Threading Model](#threading-model)
7. [Protocol Specification](#protocol-specification)
8. [State Machine](#state-machine)
9. [Directory Structure](#directory-structure)
10. [Dependencies](#dependencies)
11. [Development Guide](#development-guide)

---

## Overview

WpDaemon is a TCP server daemon that manages WireProxy instances. It provides an identical API to the original Python implementation, allowing seamless replacement without modifying client code.

### Key Features

- **Auto-download**: Automatically downloads WireProxy binary for current platform
- **Process Management**: Spawns, monitors, and terminates WireProxy processes
- **State Tracking**: Thread-safe state machine for connection lifecycle
- **Logging**: Structured log files with headers/footers
- **TCP API**: JSON-based protocol over TCP (port 23888)

---

## Project Structure

```
WpDaemon/
├── CMakeLists.txt          # Build configuration
├── ARCHITECTURE.md         # This file
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
│   ├── main.cpp           # Entry point
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

---

## Architecture Overview

The daemon follows a layered architecture with clear separation of concerns:

```
┌─────────────────────────────────────────────────────────────┐
│                     Client (Python)                         │
└──────────────────────┬──────────────────────────────────────┘
                       │ TCP Port 23888
                       ▼
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

### Component Interactions

1. **TCPServer** receives TCP connections and spawns threads
2. Each thread calls **CommandHandler::execute()** with the raw command
3. **CommandHandler** coordinates between StateMachine, ConfigManager, and WireProxyProcess
4. **WireProxyProcess** uses **LogManager** for stdout/stderr redirection
5. **BinaryManager** ensures the wireproxy binary exists before operations

---

## Component Details

### 1. TCPServer (`tcp_server.hpp/cpp`)

**Purpose**: TCP socket server using sockpp library

**Key Responsibilities**:
- Bind to `127.0.0.1:23888`
- Accept client connections
- Spawn detached threads per client
- Parse incoming commands (extract up to newline)

**Thread Safety**: Each client gets its own thread

**Public Interface**:
```cpp
class TCPServer {
public:
    explicit TCPServer(std::function<nlohmann::json(const std::string&)> command_handler);
    void start();  // Blocks forever
    void stop();   // Graceful shutdown
};
```

**Internal Flow**:
1. `start()` creates socket and enters accept loop
2. `accept()` returns new client socket
3. `process_client()` runs in detached thread
4. Reads data until newline found
5. Calls command handler callback
6. Sends JSON response + newline

---

### 2. CommandHandler (`command_handler.hpp/cpp`)

**Purpose**: Central command dispatcher and coordinator

**Key Responsibilities**:
- Parse command format (`CMD:ARG1,ARG2...\n`)
- Dispatch to appropriate handler
- Coordinate state transitions
- Manage WireProxyProcess lifecycle

**Commands Supported**:
- `spin_up:conf_name` - Start WireProxy
- `spin_down:` - Stop WireProxy
- `state:` - Get current status
- `available_confs:` - List configurations

**Thread Safety**: Mutex protects all operations (only one command executes at a time)

**State Management**:
```cpp
class CommandHandler {
    StateMachine& state_machine_;           // State tracking
    std::unique_ptr<WireProxyProcess> process_;  // Current process
    std::string current_config_;            // Active config name
    mutable std::mutex mutex_;              // Synchronization
};
```

---

### 3. StateMachine (`state_machine.hpp/cpp`)

**Purpose**: Thread-safe state tracking for WireProxy lifecycle

**States**:
- `IDLE` - No process running
- `STARTING` - Process spawn in progress
- `RUNNING` - Process is active
- `STOPPING` - Process termination in progress

**Valid Transitions**:
```
IDLE → STARTING → RUNNING → STOPPING → IDLE
         ↓                    ↓
       (failure)           (died)
         ↓                    ↓
        IDLE ←───────────────┘
```

**Thread Safety**: Atomic state storage + mutex for transitions

**Usage**:
```cpp
StateMachine sm;
if (sm.transition_to(State::STARTING)) {
    // Valid transition
}
```

---

### 4. WireProxyProcess (`wireproxy_process.hpp/cpp`)

**Purpose**: Manages a single WireProxy subprocess

**Key Responsibilities**:
- Fork and exec wireproxy binary
- Redirect stdout/stderr to log file
- Monitor process status
- Graceful/forced termination

**Process Lifecycle**:
1. `spawn(config_path)` - Fork and exec
2. `is_alive()` - Check with waitpid(WNOHANG)
3. `terminate()` - SIGTERM → wait 5s → SIGKILL

**Signal Handling**:
- Uses process group (-pid) for clean termination
- Catches child processes
- Always waits for full termination

**RAII**: Destructor calls terminate() if process still running

---

### 5. LogManager (`log_manager.hpp/cpp`)

**Purpose**: Creates and manages WireProxy session logs

**Log Format**:
```
================================================================================
WireProxy Server Log
================================================================================
Start Time: 2026-02-12 02:04:00
Unix Timestamp: 1739342640
Configuration: us-east.conf
WireProxy Version: wireproxy version 1.0.0
Configuration File: /Users/xxx/.argus/wireproxy_confs/us-east.conf

Process Output:
================================================================================
[WireProxy stdout/stderr output]

================================================================================
WireProxy Server Teardown
================================================================================
Stop Time: 2026-02-12 02:30:00
Unix Timestamp: 1739344200
Status: Initiating shutdown
Shutdown Method: Graceful termination
Final Status: Process terminated
================================================================================
End of log
================================================================================
```

**Features**:
- Line-buffered writes (real-time)
- Timestamped filenames: `<timestamp>_<config>.log`
- Thread-safe with mutex
- RAII file handle management

**LogReader Stub**:
```cpp
class ILogReader {
    virtual LogState parse_log_file(const std::filesystem::path& path) = 0;
    virtual void on_log_update(std::function<void(const LogState&)> cb) = 0;
};
```
*Note: Currently stubbed for future log parsing features*

---

### 6. BinaryManager (`binary_manager.hpp/cpp`)

**Purpose**: Manages WireProxy binary download and installation

**Platform Detection**:
- Uses `uname()` syscall
- Maps architectures:
  - `x86_64`/`AMD64` → `amd64`
  - `arm64` → `arm64` (Darwin) or `arm` (Linux)
  - `aarch64` → `arm` (Linux)
  - `armv7l`/`armv6l`/`arm` → `arm` (Linux only)

**Supported Platforms**:
- `wireproxy_darwin_amd64.tar.gz`
- `wireproxy_darwin_arm64.tar.gz`
- `wireproxy_linux_amd64.tar.gz`
- `wireproxy_linux_arm.tar.gz` (supports ARM64/ARMv7/ARMv6)

**Download Process**:
1. Detect platform
2. Download from GitHub releases (libcpr)
3. Extract with `tar -xzf`
4. Copy binary to `~/.argus/wireproxy/wireproxy`
5. Verify with `wireproxy -v`

---

### 7. ConfigManager (`config_manager.hpp/cpp`)

**Purpose**: Manages WireGuard configuration files

**Directory**: `~/.argus/wireproxy_confs/`

**Features**:
- List all `.conf` files
- Check config existence
- Normalize names (add `.conf` extension)
- Get full paths

**Name Normalization**:
```cpp
"us-east"      → "us-east.conf"
"us-east.conf" → "us-east.conf"  (unchanged)
```

---

### 8. Utils (`utils.hpp`)

**Purpose**: Utility functions

**Functions**:
- `expand_tilde()` - Convert `~` to `$HOME`
- `get_argus_dir()` - Returns `~/.argus`

---

## Data Flow

### Command Execution Flow

```
┌──────────┐     ┌──────────────┐     ┌──────────────────┐
│  Client  │────▶│   TCPServer  │────▶│  CommandHandler  │
└──────────┘     └──────────────┘     └────────┬─────────┘
                                                │
                         ┌──────────────────────┼──────────────────────┐
                         │                      │                      │
                         ▼                      ▼                      ▼
                ┌────────────────┐    ┌────────────────┐    ┌────────────────┐
                │  StateMachine  │    │ ConfigManager  │    │ WireProxyProcess│
                └────────────────┘    └────────────────┘    └────────┬───────┘
                                                                     │
                                                                     ▼
                                                            ┌────────────────┐
                                                            │  LogManager    │
                                                            └────────────────┘
```

### Example: spin_up Flow

1. **TCPServer** receives: `spin_up:us-east.conf\n`
2. **CommandHandler::execute()** parses command
3. **StateMachine** checks current state is `IDLE`
4. **ConfigManager** validates config exists
5. **LogManager** creates log file with header
6. **WireProxyProcess::spawn()** forks and execs
7. Wait 500ms, check `is_alive()`
8. **StateMachine** transitions to `RUNNING`
9. Return JSON response to client

---

## Threading Model

### Main Thread
- Runs `main()` → `TCPServer::start()`
- Blocks in `accept()` loop
- Handles SIGINT/SIGTERM for shutdown

### Client Threads (Detached)
- One thread per TCP connection
- Calls `CommandHandler::execute()`
- Terminates when client disconnects

### Process Thread
- WireProxy runs in separate process (fork)
- No threads inside the daemon itself

### Synchronization
- `CommandHandler` mutex: One command at a time
- `LogManager` mutex: Thread-safe log writes
- `StateMachine` atomic + mutex: Safe state reads/writes

---

## Protocol Specification

### Request Format

```
CMD:ARG1,ARG2,ARG3...\n
Required:
- CMD: Command name
- :: Separator (always required, even with no args)
- \n: Newline terminator
```

**Examples**:
```
spin_up:us-east.conf\n
spin_down:\n

state:\n

available_confs:\n
```

### Response Format

```json
{
    "CMD": "echo of command received",
    "result": { ... } | null,
    "error": null | "error message string"
}
```

**Success Example**:
```json
{
    "CMD": "spin_up",
    "result": {
        "status": "running",
        "config": "us-east.conf",
        "pid": 12345,
        "log_file": "/Users/xxx/.argus/wp-server-logs/1739342640_us-east.log"
    },
    "error": null
}
```

**Error Example**:
```json
{
    "CMD": "spin_up",
    "result": null,
    "error": "WireProxy is already running with config: us-east.conf"
}
```

---

## State Machine

### State Definitions

| State | Description | Valid Next States |
|-------|-------------|-------------------|
| `IDLE` | No process running | `STARTING` |
| `STARTING` | Spawn in progress | `RUNNING`, `IDLE` |
| `RUNNING` | Process active | `STOPPING`, `IDLE` |
| `STOPPING` | Termination in progress | `IDLE` |

### State Transitions

```
                    ┌────────────────────┐
                    │       IDLE         │
                    └─────────┬──────────┘
                              │ spin_up command
                              ▼
                    ┌────────────────────┐
              ┌────▶│     STARTING       │
              │     └─────────┬──────────┘
              │               │ spawn()
              │               │ (success)
              │               ▼
              │     ┌────────────────────┐      spin_down
              │     │      RUNNING       │◀─────────┐
              │     └─────────┬──────────┘          │
              │               │                     │
    (failed)  │               │ (died)              │
              │               ▼                     │
              └─────────────────────────────────────┘
```

### Thread Safety

- `std::atomic<State>` for lock-free reads
- `std::mutex` for transition validation
- `transition_to()` validates before changing

---

## Directory Structure

### User Data (created at runtime)

```
~/.argus/
├── wireproxy/
│   └── wireproxy                    # Binary executable
├── wireproxy_confs/
│   ├── config1.conf                 # WG configs
│   └── ...
└── wp-server-logs/
    ├── 1739342640_config1.log      # Session logs
    └── ...
```

### Temporary Files (during download)

```
/tmp/
├── wireproxy_<platform>.tar.gz     # Downloaded archive
└── wireproxy_extract/              # Extraction directory
    └── wireproxy                   # Extracted binary
```

---

## Dependencies

### Third-Party Libraries

| Library | Purpose | Location |
|---------|---------|----------|
| **sockpp** | TCP socket networking | `vendor/sockpp/` |
| **libcpr** | HTTP client (libcurl wrapper) | `vendor/libcpr/` |
| **nlohmann/json** | JSON parsing/serialization | `vendor/json/` |

### System Dependencies

- **C++23** compiler (Clang 17+ or GCC 13+)
- **CMake** 3.15+
- **tar** command (for archive extraction)
- POSIX system (macOS/Linux)

### CMake Targets

```cmake
target_link_libraries(WpDaemon PRIVATE
    sockpp-static      # TCP networking
    cpr::cpr          # HTTP client
    nlohmann_json::nlohmann_json  # JSON library
)
```

---

## Development Guide

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

### Adding Log Parsing (Future)

1. **Implement ILogReader**:
```cpp
class LogReader : public ILogReader {
public:
    LogState parse_log_file(const std::filesystem::path& path) override {
        // Parse log file and extract state
    }
    
    void on_log_update(std::function<void(const LogState&)> cb) override {
        // Set up file watcher (inotify/FSEvents)
    }
};
```

2. **Replace stub in LogManager**:
```cpp
// In log_manager.hpp
LogReader log_reader_;  // Instead of LogReaderStub
```

### Debugging Tips

**Enable verbose logging**:
```cpp
// Add to main.cpp
std::cout << "[DEBUG] Current state: " << state_to_string(sm.get_state()) << std::endl;
```

**Test individual components**:
```cpp
// Test ConfigManager
ConfigManager cm;
auto configs = cm.list_configs();
for (const auto& c : configs) {
    std::cout << "Config: " << c << std::endl;
}
```

**Check log files**:
```bash
ls -la ~/.argus/wp-server-logs/
tail -f ~/.argus/wp-server-logs/latest_log.log
```

### Common Issues

**"Failed to bind to port"**:
- Port 23888 is already in use
- Check: `lsof -i :23888`
- Kill existing process: `kill $(lsof -t -i:23888)`

**"Binary not found"**:
- BinaryManager will auto-download on first run
- Check permissions: `ls -la ~/.argus/wireproxy/`

**"Configuration not found"**:
- Verify config exists: `ls ~/.argus/wireproxy_confs/`
- Ensure `.conf` extension

---

## Build Instructions

```bash
# Create build directory
mkdir build && cd build

# Configure
cmake ..

# Build
make WpDaemon -j4

# Run
./WpDaemon
```

### Debug Build

```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
make WpDaemon
```

---

## Testing

### Manual Test with netcat

```bash
# Terminal 1: Start daemon
./WpDaemon

# Terminal 2: Test commands
echo "available_confs:" | nc localhost 23888
echo "state:" | nc localhost 23888
```

### Expected Output

```json
{"CMD":"available_confs","error":null,"result":{"configs":[],"count":0}}
{"CMD":"state","error":null,"result":{"config":null,"log_file":null,"pid":null,"running":false}}
```

---

## License

Same as the parent Argus project.

---

## Contributing

1. Follow existing code style (see existing files)
2. Add comments for public interfaces
3. Update this documentation for architectural changes
4. Ensure thread safety for shared state
5. Test on both macOS and Linux if possible

---

*Last Updated: February 12, 2026*
