"""
WpDaemon Integration Tests

Tests the WpDaemon TCP API using only Python standard library.
Requires the daemon to be running on localhost:23888.

Usage:
    python -m pytest tests/          # If pytest is available
    python tests/test_daemon.py      # Direct execution with unittest

Environment Variables:
    WPDAEMON_HOST: Host to connect to (default: 127.0.0.1)
    WPDAEMON_PORT: Port to connect to (default: 23888)
"""

import json
import socket
import time
import unittest
import os
from typing import Optional, Dict, Any


class WpDaemonClient:
    """Simple TCP client for WpDaemon using only stdlib."""
    
    def __init__(self, host: str = "127.0.0.1", port: int = 23888, timeout: float = 5.0):
        self.host = host
        self.port = port
        self.timeout = timeout
    
    def send_command(self, command: str) -> Dict[str, Any]:
        """
        Send a command to the daemon and return the JSON response.
        
        Args:
            command: The command string (e.g., "state:\n")
            
        Returns:
            Parsed JSON response as a dictionary
            
        Raises:
            ConnectionError: If connection fails
            TimeoutError: If operation times out
            ValueError: If response is not valid JSON
        """
        # Ensure command ends with newline
        if not command.endswith('\n'):
            command += '\n'
        
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(self.timeout)
        
        try:
            sock.connect((self.host, self.port))
            sock.sendall(command.encode('utf-8'))
            
            # Read response until newline
            response_data = b""
            while b'\n' not in response_data:
                chunk = sock.recv(4096)
                if not chunk:
                    break
                response_data += chunk
            
            response_str = response_data.decode('utf-8').strip()
            return json.loads(response_str)
            
        finally:
            sock.close()
    
    def is_available(self) -> bool:
        """Check if daemon is reachable."""
        try:
            self.send_command("state:")
            return True
        except (ConnectionError, socket.error, TimeoutError):
            return False


class TestWpDaemon(unittest.TestCase):
    """Test cases for WpDaemon TCP API."""
    
    @classmethod
    def setUpClass(cls):
        """Set up test client once for all tests."""
        host = os.environ.get('WPDAEMON_HOST', '127.0.0.1')
        port = int(os.environ.get('WPDAEMON_PORT', '23888'))
        cls.client = WpDaemonClient(host, port)
        
        # Check if daemon is running
        if not cls.client.is_available():
            raise unittest.SkipTest(
                f"WpDaemon not available at {host}:{port}. "
                f"Start it first with: ./WpDaemon"
            )
    
    def test_whoami(self):
        """Test whoami command returns version info."""
        response = self.client.send_command("whoami:")
        
        self.assertEqual(response.get("CMD"), "whoami")
        self.assertIsNone(response.get("error"))
        
        result = response.get("result")
        self.assertIsNotNone(result)
        self.assertIn("version", result)
        self.assertIn("implementation", result)
        self.assertEqual(result["implementation"], "C++")
    
    def test_available_confs(self):
        """Test available_confs returns list of configs."""
        response = self.client.send_command("available_confs:")
        
        self.assertEqual(response.get("CMD"), "available_confs")
        self.assertIsNone(response.get("error"))
        
        result = response.get("result")
        self.assertIsNotNone(result)
        self.assertIn("count", result)
        self.assertIn("configs", result)
        self.assertIsInstance(result["count"], int)
        self.assertIsInstance(result["configs"], list)
    
    def test_state(self):
        """Test state command returns current status."""
        response = self.client.send_command("state:")
        
        self.assertEqual(response.get("CMD"), "state")
        self.assertIsNone(response.get("error"))
        
        result = response.get("result")
        self.assertIsNotNone(result)
        self.assertIn("running", result)
        self.assertIn("config", result)
        self.assertIn("pid", result)
        self.assertIn("log_file", result)
        self.assertIsInstance(result["running"], bool)
    
    def test_spin_up_nonexistent_config(self):
        """Test spin_up with invalid config returns error."""
        response = self.client.send_command("spin_up:nonexistent.conf")
        
        self.assertEqual(response.get("CMD"), "spin_up")
        self.assertIsNone(response.get("result"))
        self.assertIsNotNone(response.get("error"))
        self.assertIn("Configuration not found", response["error"])
    
    def test_spin_down_when_not_running(self):
        """Test spin_down when nothing is running."""
        # First check if something is running
        state = self.client.send_command("state:")
        
        if state["result"]["running"]:
            # Stop it first
            self.client.send_command("spin_down:")
            time.sleep(0.5)
        
        response = self.client.send_command("spin_down:")
        
        self.assertEqual(response.get("CMD"), "spin_down")
        # Should return error when nothing running
        self.assertIsNotNone(response.get("error"))
    
    def test_command_format_with_newline(self):
        """Test that commands without newline are handled."""
        # Client adds newline automatically
        response = self.client.send_command("state:")  # No newline
        self.assertEqual(response.get("CMD"), "state")
        self.assertIsNone(response.get("error"))
    


class TestWpDaemonLifecycle(unittest.TestCase):
    """Test cases that require config files."""
    
    @classmethod
    def setUpClass(cls):
        """Set up test client."""
        host = os.environ.get('WPDAEMON_HOST', '127.0.0.1')
        port = int(os.environ.get('WPDAEMON_PORT', '23888'))
        cls.client = WpDaemonClient(host, port)
        
        if not cls.client.is_available():
            raise unittest.SkipTest(
                f"WpDaemon not available at {host}:{port}. "
                f"Start it first with: ./WpDaemon"
            )
        
        # Check if any configs are available
        configs = cls.client.send_command("available_confs:")
        cls.available_configs = configs["result"]["configs"]
    
    def setUp(self):
        """Ensure clean state before each test."""
        # Stop any running proxy
        try:
            self.client.send_command("spin_down:")
            time.sleep(0.3)
        except Exception:
            pass
    
    def tearDown(self):
        """Clean up after each test."""
        try:
            self.client.send_command("spin_down:")
        except Exception:
            pass
    
    def test_spin_up_down_lifecycle(self):
        """Test full spin_up and spin_down cycle if configs exist."""
        if not self.available_configs:
            self.skipTest("No WireGuard configs available for testing")
        
        config = self.available_configs[0]
        
        # Spin up
        up_response = self.client.send_command(f"spin_up:{config}")
        self.assertIsNone(up_response.get("error"), 
                         f"Failed to spin up: {up_response.get('error')}")
        self.assertEqual(up_response["result"]["status"], "running")
        self.assertEqual(up_response["result"]["config"], config)
        self.assertIn("pid", up_response["result"])
        self.assertIn("log_file", up_response["result"])
        
        # Wait for process to fully start
        time.sleep(0.5)
        
        # Verify state
        state = self.client.send_command("state:")
        self.assertTrue(state["result"]["running"])
        self.assertEqual(state["result"]["config"], config)
        
        # Spin down
        down_response = self.client.send_command("spin_down:")
        self.assertIsNone(down_response.get("error"))
        self.assertEqual(down_response["result"]["status"], "stopped")
        self.assertEqual(down_response["result"]["previous_config"], config)
        
        # Wait for process to stop
        time.sleep(0.3)
        
        # Verify stopped
        state = self.client.send_command("state:")
        self.assertFalse(state["result"]["running"])


def run_tests():
    """Run all tests with verbose output."""
    loader = unittest.TestLoader()
    suite = unittest.TestSuite()
    
    # Add all test classes
    suite.addTests(loader.loadTestsFromTestCase(TestWpDaemon))
    suite.addTests(loader.loadTestsFromTestCase(TestWpDaemonLifecycle))
    
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)
    
    return result.wasSuccessful()


if __name__ == "__main__":
    import sys
    success = run_tests()
    sys.exit(0 if success else 1)
