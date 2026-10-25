#!/usr/bin/env python3
"""Deterministic MCP protocol tests without a live server or credentials."""

import json
import sys
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import mcp_protocol_probe as probe


class FakeResponse:
    def __init__(self, status, body=b"", content_type="application/json"):
        self.status = status
        self.body = body
        self.headers = {"Content-Type": content_type}

    def getheader(self, name, default=None):
        return self.headers.get(name, default)

    def read(self):
        return self.body


class FakeConnection:
    def __init__(self, host, port, timeout=5.0, version=probe.PROTOCOL_VERSION, allow_no_client_error=True):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.version = version
        self.allow_no_client_error = allow_no_client_error
        self.requests = []
        self.responses = []
        self.closed = False

    def __enter__(self):
        return self

    def __exit__(self, *_):
        return False

    def close(self):
        self.closed = True

    def request(self, method, path, body, headers):
        self.requests.append((method, path, json.loads(body), headers))
        payload = self.requests[-1][2]
        if payload["method"] == "initialize":
            response = {"jsonrpc": "2.0", "id": payload["id"], "result": {
                "protocolVersion": self.version, "capabilities": {"tools": {}},
            }}
            self.responses.append(FakeResponse(200, json.dumps(response).encode()))
        elif payload["method"] == "notifications/initialized":
            self.responses.append(FakeResponse(202, b"", ""))
        elif payload["method"] == "tools/list":
            injected = json.loads(payload["params"].get("_metasiberia_neurocontrol_tools", "[]"))
            response = {"jsonrpc": "2.0", "id": payload["id"], "result": {
                "tools": [{"name": "get_luau_reference"}, *injected],
            }}
            self.responses.append(FakeResponse(200, json.dumps(response).encode()))
        elif payload["method"] == "tools/call":
            result = {"isError": self.allow_no_client_error, "content": [{
                "type": "text",
                "text": "NeuroControl tools require the connected Qt MCP bridge" if self.allow_no_client_error else "false success",
            }]}
            response = {"jsonrpc": "2.0", "id": payload["id"], "result": result}
            self.responses.append(FakeResponse(200, json.dumps(response).encode()))

    def getresponse(self):
        return self.responses.pop(0)


class MCPProtocolProbeTests(unittest.TestCase):
    token = "616c696365.70617373"

    def endpoint_connection(self, status=401, body=b"Valid Substrata-Login credentials are required."):
        connection = FakeConnection("vr.metasiberia.com", 8443)
        connection.request = Mock()
        connection.responses.append(FakeResponse(status, body, "text/plain"))
        return connection

    def test_endpoint_does_not_send_credentials(self):
        connection = self.endpoint_connection()
        probe.run_endpoint_probe("vr.metasiberia.com", 8443, lambda *args, **kwargs: connection)
        args, kwargs = connection.request.call_args
        self.assertEqual(args, ("POST", "/mcp"))
        self.assertNotIn("Authorization", kwargs["headers"])
        self.assertNotIn("Cookie", kwargs["headers"])
        self.assertTrue(connection.closed)

    def test_endpoint_rejects_wrong_route_or_unprotected_endpoint(self):
        for status in (200, 302, 404, 503):
            with self.subTest(status=status), self.assertRaises(probe.ProbeFailure):
                connection = self.endpoint_connection(status)
                probe.run_endpoint_probe("vr.metasiberia.com", 8443, lambda *args, **kwargs: connection)

    def test_endpoint_rejects_unrelated_unauthorized_response(self):
        connection = self.endpoint_connection(body=b"Proxy authentication required")
        with self.assertRaises(probe.ProbeFailure):
            probe.run_endpoint_probe("vr.metasiberia.com", 8443, lambda *args, **kwargs: connection)

    def test_endpoint_cli_never_reads_credentials(self):
        with patch.object(sys, "argv", ["probe", "vr.metasiberia.com", "8443", "--endpoint"]), \
             patch.object(sys, "stdin") as stdin, patch.object(probe, "run_endpoint_probe") as endpoint, \
             patch("builtins.print"):
            stdin.readline.side_effect = AssertionError("endpoint probe must not read credentials")
            self.assertEqual(probe.main(), 0)
            endpoint.assert_called_once_with("vr.metasiberia.com", 8443)

    def test_legacy_server_bridge_probe_is_explicit(self):
        connection = FakeConnection("vr.metasiberia.com", 8443)
        version, count = probe.run_probe(
            "vr.metasiberia.com", 8443, self.token, "__root__",
            connection_factory=lambda *args, **kwargs: connection,
            require_neuro_bridge=True,
        )
        self.assertEqual(version, probe.PROTOCOL_VERSION)
        self.assertEqual(count, 2)
        self.assertEqual([request[2]["method"] for request in connection.requests], [
            "initialize", "notifications/initialized", "tools/list", "tools/call",
        ])
        self.assertEqual(connection.requests[2][3]["MCP-Protocol-Version"], version)
        self.assertEqual(connection.requests[2][3]["X-Substrata-World"], "__root__")

    def test_rejects_unsupported_protocol_version(self):
        connection = FakeConnection("vr.metasiberia.com", 8443, version="2099-01-01")
        with self.assertRaisesRegex(probe.ProbeFailure, "unsupported protocol version"):
            probe.run_probe("vr.metasiberia.com", 8443, self.token, "__root__", lambda *args, **kwargs: connection)

    def test_rejects_a_fake_success_without_qt_client(self):
        connection = FakeConnection("vr.metasiberia.com", 8443, allow_no_client_error=False)
        with self.assertRaisesRegex(probe.ProbeFailure, "expected no-client error"):
            probe.run_probe("vr.metasiberia.com", 8443, self.token, "__root__", lambda *args, **kwargs: connection,
                require_neuro_bridge=True)

    def test_rejects_invalid_token_without_network_request(self):
        connection = FakeConnection("vr.metasiberia.com", 8443)
        with self.assertRaisesRegex(probe.ProbeFailure, "login token format is invalid"):
            probe.run_probe("vr.metasiberia.com", 8443, "not-a-token", "__root__", lambda *args, **kwargs: connection)
        self.assertEqual(connection.requests, [])

    def test_basic_rollback_probe_does_not_require_neuro_extension(self):
        connection = FakeConnection("vr.metasiberia.com", 8443)
        version, count = probe.run_probe(
            "vr.metasiberia.com", 8443, self.token, "__root__",
            connection_factory=lambda *args, **kwargs: connection,
        )
        self.assertEqual(version, probe.PROTOCOL_VERSION)
        self.assertEqual(count, 1)
        self.assertEqual([request[2]["method"] for request in connection.requests], [
            "initialize", "notifications/initialized", "tools/list",
        ])


if __name__ == "__main__":
    unittest.main(verbosity=2)
