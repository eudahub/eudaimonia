# SPDX-License-Identifier: GPL-3.0-only
# A language server for the tests: diagnostics for lines with "bad", a
# definition at line 0, column 4, a request of its own to the client; every
# message it gets is written to the log named by its first argument.
import json
import sys

log = open(sys.argv[1], "a", buffering=1)
stdin = sys.stdin.buffer
stdout = sys.stdout.buffer


def send(message):
    body = json.dumps(message).encode()
    stdout.write(b"Content-Length: %d\r\n\r\n" % len(body) + body)
    stdout.flush()


def read():
    length = None
    while True:
        line = stdin.readline()
        if not line:
            return None
        line = line.strip()
        if not line:
            break
        if line.lower().startswith(b"content-length:"):
            length = int(line.split(b":")[1])
    return json.loads(stdin.read(length))


def diagnose(uri, text):
    diagnostics = []
    for number, line in enumerate(text.split("\n")):
        column = line.find("bad")
        if column >= 0:
            diagnostics.append({"range": {"start": {"line": number, "character": column},
                                          "end": {"line": number, "character": column + 3}},
                                "severity": 1, "message": "bad word", "source": "fake"})
    send({"jsonrpc": "2.0", "method": "textDocument/publishDiagnostics",
          "params": {"uri": uri, "diagnostics": diagnostics}})


while True:
    message = read()
    if message is None:
        break
    method = message.get("method")
    log.write((method or "response %s" % message.get("id")) + "\n")
    params = message.get("params", {})
    if method == "initialize":
        send({"jsonrpc": "2.0", "id": message["id"],
              "result": {"capabilities": {"textDocumentSync": 1, "definitionProvider": True}}})
    elif method == "initialized":
        send({"jsonrpc": "2.0", "id": "server-1", "method": "window/workDoneProgress/create",
              "params": {"token": "t"}})
    elif method == "textDocument/didOpen":
        diagnose(params["textDocument"]["uri"], params["textDocument"]["text"])
    elif method == "textDocument/didChange":
        diagnose(params["textDocument"]["uri"], params["contentChanges"][-1]["text"])
    elif method == "textDocument/definition":
        send({"jsonrpc": "2.0", "id": message["id"],
              "result": [{"uri": params["textDocument"]["uri"],
                          "range": {"start": {"line": 0, "character": 4}, "end": {"line": 0, "character": 8}}}]})
    elif method == "shutdown":
        send({"jsonrpc": "2.0", "id": message["id"], "result": None})
    elif method == "exit":
        break
