#!/usr/bin/env python3
"""MCP readiness and optional authenticated protocol checks.

The login token and active-world key are read from stdin so they never appear
in the process arguments, URL, or probe output. The HTTPS connection uses the
public hostname for SNI/certificate verification while connecting to loopback.
The --endpoint mode requires no account and only verifies the protected route;
it does not establish that authenticated tools or the Qt bridge work.
"""

from __future__ import annotations

import http.client
import json
import re
import socket
import ssl
import sys
from contextlib import closing
from typing import Callable


PROTOCOL_VERSION = "2024-11-05"
PROBE_TOOL_NAME = "neuro.activation_probe"


class ProbeFailure(RuntimeError):
    pass


class LoopbackHTTPSConnection(http.client.HTTPSConnection):
    def __init__(self, host: str, port: int, timeout: float = 5.0) -> None:
        super().__init__(host, port, timeout=timeout, context=ssl.create_default_context())

    def connect(self) -> None:
        raw_socket = socket.create_connection(("127.0.0.1", self.port), self.timeout)
        self.sock = self._context.wrap_socket(raw_socket, server_hostname=self.host)


def _validate_credentials(token: str, world: str) -> None:
    # Server MCP's Substrata-Login token is hex(username).hex(password).
    if not re.fullmatch(r"[0-9a-fA-F]+\.[0-9a-fA-F]+", token):
        raise ProbeFailure("login token format is invalid; expected hex username and password separated by one dot")
    if not world or "\r" in world or "\n" in world:
        raise ProbeFailure("active world key is empty or contains a line break")


def run_endpoint_probe(
    host: str,
    port: int,
    connection_factory: Callable[..., http.client.HTTPSConnection] = LoopbackHTTPSConnection,
) -> None:
    with closing(connection_factory(host, port, timeout=5.0)) as connection:
        connection.request("POST", "/mcp", body=b'{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}',
            headers={"Host": host, "Content-Type": "application/json", "Accept": "application/json"})
        response = connection.getresponse()
        body = response.read()
        if response.status != 401:
            raise ProbeFailure(f"MCP endpoint returned HTTP {response.status}; expected HTTP 401 without credentials")
        if b"Valid Substrata-Login credentials are required." not in body:
            raise ProbeFailure("MCP endpoint did not return the expected application authentication response")


def _post(
    connection: http.client.HTTPSConnection,
    token: str,
    world: str,
    payload: dict,
    protocol_version: str | None = None,
) -> tuple[int, str, bytes]:
    headers = {
        "Host": connection.host,
        "Content-Type": "application/json",
        "Accept": "application/json",
        "Authorization": f"Substrata-Login {token}",
        "X-Substrata-World": world,
    }
    if protocol_version:
        headers["MCP-Protocol-Version"] = protocol_version
    body = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    connection.request("POST", "/mcp", body=body, headers=headers)
    response = connection.getresponse()
    content_type = response.getheader("Content-Type", "")
    return response.status, content_type, response.read()


def _json_response(
    connection: http.client.HTTPSConnection,
    token: str,
    world: str,
    payload: dict,
    protocol_version: str | None = None,
) -> dict:
    status, content_type, body = _post(connection, token, world, payload, protocol_version)
    if status != 200:
        raise ProbeFailure(f"{payload.get('method', 'MCP request')} returned HTTP {status}")
    if "application/json" not in content_type.lower():
        raise ProbeFailure(f"{payload.get('method', 'MCP request')} returned a non-JSON content type")
    try:
        document = json.loads(body)
    except (UnicodeDecodeError, json.JSONDecodeError):
        raise ProbeFailure(f"{payload.get('method', 'MCP request')} returned malformed JSON") from None
    if not isinstance(document, dict) or document.get("jsonrpc") != "2.0":
        raise ProbeFailure(f"{payload.get('method', 'MCP request')} returned an invalid JSON-RPC envelope")
    if document.get("id") != payload.get("id"):
        raise ProbeFailure(f"{payload.get('method', 'MCP request')} returned a mismatched request id")
    if "error" in document:
        error = document["error"]
        code = error.get("code") if isinstance(error, dict) else "unknown"
        raise ProbeFailure(f"{payload.get('method', 'MCP request')} returned JSON-RPC error {code}")
    result = document.get("result")
    if not isinstance(result, dict):
        raise ProbeFailure(f"{payload.get('method', 'MCP request')} did not return a result object")
    return result


def run_probe(
    host: str,
    port: int,
    token: str,
    world: str,
    connection_factory: Callable[..., http.client.HTTPSConnection] = LoopbackHTTPSConnection,
    require_neuro_bridge: bool = False,
) -> tuple[str, int]:
    _validate_credentials(token, world)
    with closing(connection_factory(host, port, timeout=5.0)) as connection:
        initialized = _json_response(
            connection,
            token,
            world,
            {
                "jsonrpc": "2.0",
                "id": 1,
                "method": "initialize",
                "params": {
                    "protocolVersion": PROTOCOL_VERSION,
                    "capabilities": {"tools": {}, "resources": {"subscribe": False, "listChanged": False}},
                    "clientInfo": {"name": "metasiberia-activation-healthcheck", "version": "1"},
                },
            },
        )
        negotiated_version = initialized.get("protocolVersion")
        capabilities = initialized.get("capabilities")
        if negotiated_version != PROTOCOL_VERSION:
            raise ProbeFailure(f"server selected unsupported protocol version {negotiated_version!r}")
        if not isinstance(capabilities, dict) or not isinstance(capabilities.get("tools"), dict):
            raise ProbeFailure("initialize response does not advertise MCP tools")

        status, _, body = _post(
            connection,
            token,
            world,
            {"jsonrpc": "2.0", "method": "notifications/initialized"},
            negotiated_version,
        )
        if status != 202 or body:
            raise ProbeFailure(f"notifications/initialized returned HTTP {status}, expected empty HTTP 202")

        probe_tool = {
            "name": PROBE_TOOL_NAME,
            "description": "Transient read-only activation check; no operation is performed.",
            "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
        }
        list_params = {}
        if require_neuro_bridge:
            list_params["_metasiberia_neurocontrol_tools"] = json.dumps([probe_tool], separators=(",", ":"))
        listed = _json_response(
            connection,
            token,
            world,
            {
                "jsonrpc": "2.0",
                "id": 2,
                "method": "tools/list",
                "params": list_params,
            },
            negotiated_version,
        )
        tools = listed.get("tools")
        if not isinstance(tools, list):
            raise ProbeFailure("tools/list returned no tools array")
        names = [tool.get("name") for tool in tools if isinstance(tool, dict)]
        if len(names) != len(tools) or any(not isinstance(name, str) for name in names):
            raise ProbeFailure("tools/list returned a tool without a string name")
        if len(set(names)) != len(names):
            raise ProbeFailure("tools/list returned malformed or duplicate tool names")
        if not names:
            raise ProbeFailure("tools/list returned an empty tool registry")
        if require_neuro_bridge and PROBE_TOOL_NAME not in names:
            raise ProbeFailure("tools/list did not return the authenticated desktop bridge extension")

        if require_neuro_bridge:
            # A server without a Qt client must reject execution instead of
            # faking a successful NeuroControl result.
            no_client_result = _json_response(
                connection,
                token,
                world,
                {
                    "jsonrpc": "2.0",
                    "id": 3,
                    "method": "tools/call",
                    "params": {"name": PROBE_TOOL_NAME, "arguments": {}},
                },
                negotiated_version,
            )
            content = no_client_result.get("content", [])
            if not isinstance(content, list):
                raise ProbeFailure("tools/call returned malformed content for the no-client check")
            error_text = " ".join(
                item.get("text", "")
                for item in content
                if isinstance(item, dict) and item.get("type") == "text" and isinstance(item.get("text", ""), str)
            )
            if no_client_result.get("isError") is not True or "connected Qt MCP bridge" not in error_text:
                raise ProbeFailure("server did not report the expected no-client error for a NeuroControl call")

        return negotiated_version, len(tools)


def main() -> int:
    if len(sys.argv) not in {3, 4} or (len(sys.argv) == 4 and sys.argv[3] not in {"--basic", "--endpoint"}):
        print("usage: mcp_protocol_probe.py HOST PORT [--basic | --endpoint] (--endpoint needs no credentials; other modes read token and world from stdin)", file=sys.stderr)
        return 2
    try:
        if len(sys.argv) == 4 and sys.argv[3] == "--endpoint":
            run_endpoint_probe(sys.argv[1], int(sys.argv[2]))
            print("MCP endpoint check OK: HTTPS route rejected an unauthenticated request with HTTP 401; authenticated tools not checked")
            return 0
        token = sys.stdin.readline().rstrip("\r\n")
        world = sys.stdin.readline().rstrip("\r\n")
        # NeuroControl tools are registered by the desktop bridge. A direct
        # server probe can verify only the server's own authenticated tools.
        version, tool_count = run_probe(sys.argv[1], int(sys.argv[2]), token, world)
    except (ProbeFailure, OSError, ssl.SSLError, http.client.HTTPException, ValueError) as error:
        print(f"MCP protocol check failed: {error}", file=sys.stderr)
        return 1
    print(f"MCP protocol check OK: initialized {version}; tools/list returned {tool_count} server tools; desktop neuro.* tools not checked")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
