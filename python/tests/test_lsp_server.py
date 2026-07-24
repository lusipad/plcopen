"""D1 stdio LSP acceptance tests."""

from __future__ import annotations

import io
import json
import sys
import unittest
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from plcopen_lsp.server import (  # noqa: E402
    MAX_BODY_BYTES,
    MAX_DOCUMENT_BYTES,
    MAX_HEADER_BYTES,
    MAX_OPEN_DOCUMENTS,
    ChangeError,
    FatalProtocolError,
    RecoverableProtocolError,
    apply_content_changes,
    read_message,
    run_session,
)


def frame(message: dict[str, Any]) -> bytes:
    body = json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode(
        "utf-8"
    )
    return f"Content-Length: {len(body)}\r\n\r\n".encode("ascii") + body


def transcript(*messages: dict[str, Any]) -> bytes:
    return b"".join(frame(message) for message in messages)


def decode_frames(data: bytes) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    at = 0
    while at < len(data):
        header_end = data.index(b"\r\n\r\n", at)
        headers = data[at:header_end].decode("ascii").split("\r\n")
        length = int(
            next(
                line.split(":", 1)[1].strip()
                for line in headers
                if line.lower().startswith("content-length:")
            )
        )
        body_start = header_end + 4
        body_end = body_start + length
        result.append(json.loads(data[body_start:body_end].decode("utf-8")))
        at = body_end
    return result


class FakeDocument:
    def __init__(self, text: str):
        self.text = text
        self.updates: list[str] = []

    def update(self, text: str) -> dict[str, int]:
        self.text = text
        self.updates.append(text)
        return {"reparsed_pous": 1, "reused_pous": 2}

    def diagnostics(self) -> list[dict[str, Any]]:
        if "bad" not in self.text:
            return []
        return [
            {
                "range": {
                    "start": {"line": 0, "character": 0},
                    "end": {"line": 0, "character": 3},
                },
                "code": "parse_expected_expression",
                "message": "bad source",
                "warning": False,
            }
        ]

    def complete(self, line: int, character: int) -> dict[str, Any]:
        return {
            "isIncomplete": False,
            "items": [
                {
                    "label": "Counter",
                    "detail": "local Counter : INT",
                    "kind": 6,
                }
            ],
        }

    def definition(self, line: int, character: int) -> dict[str, Any] | None:
        return {
            "start": {"line": 1, "character": 2},
            "end": {"line": 1, "character": 9},
        }

    def hover(self, line: int, character: int) -> dict[str, Any] | None:
        return {
            "range": {
                "start": {"line": line, "character": character},
                "end": {"line": line, "character": character + 1},
            },
            "contents": "local Counter : INT",
        }


class Factory:
    def __init__(self):
        self.documents: list[FakeDocument] = []

    def __call__(self, text: str) -> FakeDocument:
        document = FakeDocument(text)
        self.documents.append(document)
        return document


class Utf16ChangeTests(unittest.TestCase):
    def test_emoji_crlf_and_multi_change(self) -> None:
        text = "A😀B\r\nC"
        changed = apply_content_changes(
            text,
            [
                {
                    "range": {
                        "start": {"line": 0, "character": 1},
                        "end": {"line": 0, "character": 3},
                    },
                    "text": "X",
                },
                {
                    "range": {
                        "start": {"line": 1, "character": 1},
                        "end": {"line": 1, "character": 1},
                    },
                    "text": "D",
                },
            ],
        )
        self.assertEqual(changed, "AXB\r\nCD")

    def test_surrogate_split_rejects_whole_batch(self) -> None:
        original = "A😀B"
        changes = [
            {
                "range": {
                    "start": {"line": 0, "character": 0},
                    "end": {"line": 0, "character": 1},
                },
                "text": "Z",
            },
            {
                "range": {
                    "start": {"line": 0, "character": 2},
                    "end": {"line": 0, "character": 2},
                },
                "text": "invalid",
            },
        ]
        with self.assertRaises(ChangeError):
            apply_content_changes(original, changes)
        self.assertEqual(original, "A😀B")

    def test_full_replacement(self) -> None:
        self.assertEqual(
            apply_content_changes("old", [{"text": "new"}]),
            "new",
        )


class FramingTests(unittest.TestCase):
    def test_header_capacity_is_n_and_n_plus_one(self) -> None:
        prefix = b"Content-Length: 2\r\nX-Pad: "
        suffix = b"\r\n\r\n"
        padding = b"a" * (MAX_HEADER_BYTES - len(prefix) - len(suffix))
        exact = prefix + padding + suffix + b"{}"
        self.assertEqual(len(prefix + padding + suffix), MAX_HEADER_BYTES)
        self.assertEqual(read_message(io.BytesIO(exact)), {})

        over = prefix + padding + b"a" + suffix + b"{}"
        with self.assertRaises(FatalProtocolError):
            read_message(io.BytesIO(over))

    def test_body_capacity_is_n_and_n_plus_one(self) -> None:
        prefix = b'{"jsonrpc":"2.0","method":"padding","value":"'
        suffix = b'"}'
        body = prefix + b"a" * (MAX_BODY_BYTES - len(prefix) - len(suffix)) + suffix
        self.assertEqual(len(body), MAX_BODY_BYTES)
        exact = f"Content-Length: {len(body)}\r\n\r\n".encode("ascii") + body
        message = read_message(io.BytesIO(exact))
        self.assertEqual(message["method"], "padding")

        over = f"Content-Length: {MAX_BODY_BYTES + 1}\r\n\r\n".encode("ascii")
        with self.assertRaises(FatalProtocolError):
            read_message(io.BytesIO(over))

    def test_bad_json_and_batch_are_recoverable(self) -> None:
        bad = b"Content-Length: 1\r\n\r\n{"
        with self.assertRaises(RecoverableProtocolError) as parse:
            read_message(io.BytesIO(bad))
        self.assertEqual(parse.exception.code, -32700)

        batch = b"[]"
        framed = b"Content-Length: 2\r\n\r\n" + batch
        with self.assertRaises(RecoverableProtocolError) as invalid:
            read_message(io.BytesIO(framed))
        self.assertEqual(invalid.exception.code, -32600)


class ProtocolTests(unittest.TestCase):
    def test_full_lifecycle_and_four_queries(self) -> None:
        uri = "untitled:Untitled-1"
        factory = Factory()
        input_stream = io.BytesIO(
            transcript(
                {
                    "jsonrpc": "2.0",
                    "id": 1,
                    "method": "initialize",
                    "params": {"capabilities": {}},
                },
                {
                    "jsonrpc": "2.0",
                    "method": "initialized",
                    "params": {},
                },
                {
                    "jsonrpc": "2.0",
                    "method": "textDocument/didOpen",
                    "params": {
                        "textDocument": {
                            "uri": uri,
                            "languageId": "plcopen-st",
                            "version": 1,
                            "text": "bad😀\r\nC",
                        }
                    },
                },
                {
                    "jsonrpc": "2.0",
                    "id": 2,
                    "method": "textDocument/completion",
                    "params": {
                        "textDocument": {"uri": uri},
                        "position": {"line": 0, "character": 0},
                    },
                },
                {
                    "jsonrpc": "2.0",
                    "id": 3,
                    "method": "textDocument/definition",
                    "params": {
                        "textDocument": {"uri": uri},
                        "position": {"line": 0, "character": 0},
                    },
                },
                {
                    "jsonrpc": "2.0",
                    "id": 4,
                    "method": "textDocument/hover",
                    "params": {
                        "textDocument": {"uri": uri},
                        "position": {"line": 0, "character": 0},
                    },
                },
                {
                    "jsonrpc": "2.0",
                    "method": "textDocument/didChange",
                    "params": {
                        "textDocument": {"uri": uri, "version": 2},
                        "contentChanges": [
                            {
                                "range": {
                                    "start": {"line": 0, "character": 0},
                                    "end": {"line": 0, "character": 3},
                                },
                                "text": "ok",
                            }
                        ],
                    },
                },
                {
                    "jsonrpc": "2.0",
                    "method": "textDocument/didClose",
                    "params": {"textDocument": {"uri": uri}},
                },
                {
                    "jsonrpc": "2.0",
                    "id": 5,
                    "method": "shutdown",
                    "params": None,
                },
                {"jsonrpc": "2.0", "method": "exit"},
            )
        )
        output_stream = io.BytesIO()

        exit_code = run_session(input_stream, output_stream, document_factory=factory)

        self.assertEqual(exit_code, 0)
        messages = decode_frames(output_stream.getvalue())
        initialize = next(message for message in messages if message.get("id") == 1)
        capabilities = initialize["result"]["capabilities"]
        self.assertEqual(capabilities["positionEncoding"], "utf-16")
        self.assertEqual(capabilities["textDocumentSync"]["change"], 2)
        self.assertTrue(capabilities["definitionProvider"])
        self.assertTrue(capabilities["hoverProvider"])

        publications = [
            message["params"]
            for message in messages
            if message.get("method") == "textDocument/publishDiagnostics"
        ]
        self.assertEqual([item["version"] for item in publications], [1, 2, 2])
        self.assertEqual(
            publications[0]["diagnostics"][0]["code"],
            "parse_expected_expression",
        )
        self.assertEqual(publications[1]["diagnostics"], [])
        self.assertEqual(publications[2]["diagnostics"], [])

        completion = next(message for message in messages if message.get("id") == 2)
        self.assertEqual(completion["result"]["items"][0]["label"], "Counter")
        definition = next(message for message in messages if message.get("id") == 3)
        self.assertEqual(definition["result"]["uri"], uri)
        hover = next(message for message in messages if message.get("id") == 4)
        self.assertEqual(
            hover["result"]["contents"],
            {"kind": "plaintext", "value": "local Counter : INT"},
        )
        shutdown = next(message for message in messages if message.get("id") == 5)
        self.assertIsNone(shutdown["result"])
        self.assertEqual(factory.documents[0].updates, ["ok😀\r\nC"])

    def test_lifecycle_errors_and_stale_change(self) -> None:
        uri = "file:///workspace/main.st"
        factory = Factory()
        output = io.BytesIO()
        exit_code = run_session(
            io.BytesIO(
                transcript(
                    {
                        "jsonrpc": "2.0",
                        "id": 1,
                        "method": "textDocument/hover",
                        "params": {
                            "textDocument": {"uri": uri},
                            "position": {"line": 0, "character": 0},
                        },
                    },
                    {
                        "jsonrpc": "2.0",
                        "id": 2,
                        "method": "initialize",
                        "params": {"capabilities": {}},
                    },
                    {
                        "jsonrpc": "2.0",
                        "method": "textDocument/didOpen",
                        "params": {
                            "textDocument": {
                                "uri": uri,
                                "languageId": "plcopen-st",
                                "version": 4,
                                "text": "ok",
                            }
                        },
                    },
                    {
                        "jsonrpc": "2.0",
                        "method": "textDocument/didChange",
                        "params": {
                            "textDocument": {"uri": uri, "version": 4},
                            "contentChanges": [{"text": "stale"}],
                        },
                    },
                    {
                        "jsonrpc": "2.0",
                        "id": 3,
                        "method": "plcopen/unknown",
                        "params": {},
                    },
                    {
                        "jsonrpc": "2.0",
                        "id": 4,
                        "method": "shutdown",
                        "params": None,
                    },
                    {
                        "jsonrpc": "2.0",
                        "id": 5,
                        "method": "textDocument/hover",
                        "params": {
                            "textDocument": {"uri": uri},
                            "position": {"line": 0, "character": 0},
                        },
                    },
                    {"jsonrpc": "2.0", "method": "exit"},
                )
            ),
            output,
            document_factory=factory,
        )
        self.assertEqual(exit_code, 0)
        messages = decode_frames(output.getvalue())
        errors = {
            message["id"]: message["error"]["code"]
            for message in messages
            if "error" in message and message.get("id") is not None
        }
        self.assertEqual(errors[1], -32002)
        self.assertEqual(errors[3], -32601)
        self.assertEqual(errors[5], -32600)
        self.assertEqual(factory.documents[0].text, "ok")
        self.assertEqual(factory.documents[0].updates, [])
        logs = [
            message
            for message in messages
            if message.get("method") == "window/logMessage"
        ]
        self.assertTrue(any("version" in item["params"]["message"] for item in logs))

    def test_document_count_capacity_is_n_and_n_plus_one(self) -> None:
        factory = Factory()
        messages: list[dict[str, Any]] = [
            {
                "jsonrpc": "2.0",
                "id": 1,
                "method": "initialize",
                "params": {"capabilities": {}},
            }
        ]
        for index in range(MAX_OPEN_DOCUMENTS + 1):
            messages.append(
                {
                    "jsonrpc": "2.0",
                    "method": "textDocument/didOpen",
                    "params": {
                        "textDocument": {
                            "uri": f"untitled:doc-{index}",
                            "languageId": "plcopen-st",
                            "version": 1,
                            "text": "PROGRAM P END_PROGRAM",
                        }
                    },
                }
            )
        messages.extend(
            [
                {
                    "jsonrpc": "2.0",
                    "id": 2,
                    "method": "shutdown",
                    "params": None,
                },
                {"jsonrpc": "2.0", "method": "exit"},
            ]
        )
        output = io.BytesIO()
        self.assertEqual(
            run_session(
                io.BytesIO(transcript(*messages)),
                output,
                document_factory=factory,
            ),
            0,
        )
        self.assertEqual(len(factory.documents), MAX_OPEN_DOCUMENTS)
        publications = [
            message["params"]
            for message in decode_frames(output.getvalue())
            if message.get("method") == "textDocument/publishDiagnostics"
        ]
        self.assertEqual(
            publications[-1]["diagnostics"][0]["code"],
            "capacity_open_documents",
        )

    def test_document_byte_capacity_is_n_and_n_plus_one(self) -> None:
        factory = Factory()
        exact = "a" * MAX_DOCUMENT_BYTES
        over = exact + "b"
        output = io.BytesIO()
        self.assertEqual(
            run_session(
                io.BytesIO(
                    transcript(
                        {
                            "jsonrpc": "2.0",
                            "id": 1,
                            "method": "initialize",
                            "params": {"capabilities": {}},
                        },
                        {
                            "jsonrpc": "2.0",
                            "method": "textDocument/didOpen",
                            "params": {
                                "textDocument": {
                                    "uri": "untitled:exact",
                                    "languageId": "plcopen-st",
                                    "version": 1,
                                    "text": exact,
                                }
                            },
                        },
                        {
                            "jsonrpc": "2.0",
                            "method": "textDocument/didOpen",
                            "params": {
                                "textDocument": {
                                    "uri": "untitled:over",
                                    "languageId": "plcopen-st",
                                    "version": 1,
                                    "text": over,
                                }
                            },
                        },
                        {
                            "jsonrpc": "2.0",
                            "id": 2,
                            "method": "shutdown",
                            "params": None,
                        },
                        {"jsonrpc": "2.0", "method": "exit"},
                    )
                ),
                output,
                document_factory=factory,
            ),
            0,
        )
        self.assertEqual(len(factory.documents), 1)
        publications = [
            message["params"]
            for message in decode_frames(output.getvalue())
            if message.get("method") == "textDocument/publishDiagnostics"
        ]
        over_publication = next(
            item for item in publications if item["uri"] == "untitled:over"
        )
        self.assertEqual(
            over_publication["diagnostics"][0]["code"],
            "capacity_document_bytes",
        )


if __name__ == "__main__":
    unittest.main()
