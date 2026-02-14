# WpDaemon Tests

Integration tests for WpDaemon using only Python standard library.

## Requirements

- Python 3.7+ (uses only stdlib)
- WpDaemon running on localhost:23888

## Running Tests

### Direct execution:
```bash
python tests/test_daemon.py
```

### Using unittest:
```bash
python -m unittest tests.test_daemon -v
```

### With pytest (if installed):
```bash
pytest tests/
```

## Environment Variables

- `WPDAEMON_HOST`: Host to connect to (default: 127.0.0.1)
- `WPDAEMON_PORT`: Port to connect to (default: 23888)

## Test Coverage

- **test_whoami**: Tests version and implementation info
- **test_available_confs**: Tests config listing
- **test_state**: Tests status checking
- **test_spin_up_nonexistent_config**: Tests error handling
- **test_spin_down_when_not_running**: Tests stop when idle
- **test_spin_up_down_lifecycle**: Tests full lifecycle (requires config files)

## Manual Testing

You can also test manually with netcat:

```bash
# Check version
echo "whoami:" | nc localhost 23888

# List configs
echo "available_confs:" | nc localhost 23888

# Check state
echo "state:" | nc localhost 23888

# Start proxy (requires valid config)
echo "spin_up:us-east.conf" | nc localhost 23888

# Stop proxy
echo "spin_down:" | nc localhost 23888
```
