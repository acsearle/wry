#!/usr/bin/env python3
"""wry_mcp: an MCP server (stdio) exposing wry's commands as tools.

A projection of the game's command registry, not a second vocabulary
(client/client/core/docs/commands.md section 9): at startup it asks the
running game to `describe` its commands and offers one tool per command,
with an input schema built from the parameter types.  It knows no command
by name, so it never needs editing when commands are added.  Replies that
carry an image (screenshot) are returned as image content.

Register it with Claude Code, for example:

    claude mcp add wry -- python3 /path/to/wry/tools/wry_mcp.py

The game must be running with its control socket open
(`open client.app --args --control-socket`).  If it is not running when the
client lists tools, only the generic `wry_command` tool is offered; once a
later call reaches the game, the full list is announced with
notifications/tools/list_changed.

The socket is $WRY_CONTROL_SOCKET if set, else $TMPDIR/wry-control.sock.
Standard library only.
"""

import base64
import json
import os
import socket
import sys
import tempfile

PROTOCOL_VERSION = "2025-06-18"

GENERIC_TOOL = {
    "name": "wry_command",
    "description": (
        "Run one wry command line, e.g. `step 10`, `write Coordinate(3, 4) "
        "OPCODE::SWAP`, `press \"NEW GAME\"`.  `help` lists commands; "
        "`help NAME` describes one.  Arguments are literals: integers, "
        "\"quoted strings\", OPCODE::SWAP, Coordinate(12, -34), EntityID(7); "
        "a bare word is read as the parameter's type expects."),
    "inputSchema": {
        "type": "object",
        "properties": {"line": {"type": "string", "description": "the command line"}},
        "required": ["line"],
    },
}

JSON_TYPES = {"Int": "integer", "Bool": "boolean", "String": "string"}


def socket_path():
    return (os.environ.get("WRY_CONTROL_SOCKET")
            or os.path.join(tempfile.gettempdir(), "wry-control.sock"))


class Game:
    """A lazily (re)opened connection to the game's control socket."""

    def __init__(self):
        self.sock = None
        self.buffer = b""
        self.next_id = 1

    def _connect(self):
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.connect(socket_path())
        self.sock, self.buffer = sock, b""

    def request(self, payload):
        """Send one request and return its reply; raises OSError if the
        game is unreachable."""
        for attempt in (0, 1):
            try:
                if self.sock is None:
                    self._connect()
                payload = dict(payload, id=self.next_id)
                self.next_id += 1
                self.sock.sendall(json.dumps(payload).encode("utf-8") + b"\n")
                while b"\n" not in self.buffer:
                    chunk = self.sock.recv(1 << 20)
                    if not chunk:
                        raise ConnectionError("the game closed the control socket")
                    self.buffer += chunk
                line, self.buffer = self.buffer.split(b"\n", 1)
                return json.loads(line)
            except OSError:
                self.sock = None
                if attempt:
                    raise


def tool_from_command(command):
    properties, required = {}, []
    for p in command["params"]:
        description = p["summary"]
        json_type = JSON_TYPES.get(p["type"])
        if json_type is None:
            # Any other type travels as a string holding its literal text.
            json_type = "string"
            description += f". A {p['type']} literal"
            if "example" in p:
                description += f", e.g. {p['example']}"
        if "default" in p:
            description += f" (default: {p['default']})"
        else:
            required.append(p["name"])
        properties[p["name"]] = {"type": json_type, "description": description}
    return {
        "name": command["name"],
        "description": f"{command['summary']} [{command['category']}]",
        "inputSchema": {"type": "object", "properties": properties, "required": required},
    }


class Server:

    def __init__(self):
        self.game = Game()
        self.commands = None        # the registry, once fetched

    def fetch_commands(self):
        if self.commands is not None:
            return True
        try:
            reply = self.game.request({"command": "describe"})
            self.commands = json.loads(reply["text"])
            return True
        except (OSError, ValueError, KeyError):
            return False

    def tools(self):
        tools = [GENERIC_TOOL]
        if self.fetch_commands():
            tools += [tool_from_command(c) for c in self.commands]
        return tools

    def call(self, name, arguments):
        had_commands = self.commands is not None
        if name == GENERIC_TOOL["name"]:
            payload = {"line": arguments.get("line", "")}
        else:
            payload = {"command": name, "args": arguments}
        try:
            reply = self.game.request(payload)
        except OSError as e:
            return {"content": [{"type": "text", "text":
                     f"cannot reach the game at {socket_path()}: {e}.  Is it running "
                     "with --control-socket?"}], "isError": True}
        content = []
        text = reply.get("text") or ""
        status = reply.get("status")
        summary = reply.get("line") or ""
        if status != "ok":
            summary = f"{summary} ({status})".strip()
        if text:
            summary = f"{summary}\n{text}" if summary else text
        content.append({"type": "text", "text": summary or "ok"})
        image = reply.get("image")
        if image:
            try:
                with open(image, "rb") as f:
                    content.append({"type": "image", "mimeType": "image/png",
                                    "data": base64.b64encode(f.read()).decode("ascii")})
            except OSError as e:
                content.append({"type": "text", "text": f"(could not read {image}: {e})"})
        result = {"content": content, "isError": status != "ok"}
        if not had_commands and self.fetch_commands():
            self.notify("notifications/tools/list_changed")
        return result

    def notify(self, method):
        send({"jsonrpc": "2.0", "method": method})

    def handle(self, message):
        method = message.get("method")
        params = message.get("params") or {}
        if method == "initialize":
            return {
                "protocolVersion": params.get("protocolVersion", PROTOCOL_VERSION),
                "capabilities": {"tools": {"listChanged": True}},
                "serverInfo": {"name": "wry", "version": "0.1"},
                "instructions": (
                    "Drives the running wry game through its command registry.  "
                    "Commands that take time (step, frame, screenshot) reply when "
                    "done.  `widgets` lists what is on screen and where; `press` "
                    "clicks a button by label; `screenshot` returns the next frame."),
            }
        if method == "ping":
            return {}
        if method == "tools/list":
            return {"tools": self.tools()}
        if method == "tools/call":
            return self.call(params.get("name", ""), params.get("arguments") or {})
        raise LookupError(method)


def send(message):
    sys.stdout.write(json.dumps(message) + "\n")
    sys.stdout.flush()


def main():
    server = Server()
    for line in sys.stdin:
        if not line.strip():
            continue
        try:
            message = json.loads(line)
        except ValueError:
            send({"jsonrpc": "2.0", "id": None,
                  "error": {"code": -32700, "message": "parse error"}})
            continue
        if "id" not in message:
            continue            # a notification; none need answering
        try:
            send({"jsonrpc": "2.0", "id": message["id"], "result": server.handle(message)})
        except LookupError:
            send({"jsonrpc": "2.0", "id": message["id"],
                  "error": {"code": -32601, "message": f"unknown method {message.get('method')}"}})


if __name__ == "__main__":
    main()
