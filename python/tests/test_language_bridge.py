"""Acceptance tests for the private pybind ST language bridge."""

from __future__ import annotations

import io
import json
import os
from pathlib import Path
import subprocess
import sys
import unittest
from typing import Any

import pyplcopen
from plcopen_lsp.server import run_session


def position_of(text: str, needle: str) -> tuple[int, int]:
    offset = text.index(needle)
    line = text.count("\n", 0, offset)
    line_start = text.rfind("\n", 0, offset) + 1
    character = len(text[line_start:offset].encode("utf-16-le")) // 2
    return line, character


def frame(message: dict[str, Any]) -> bytes:
    body = json.dumps(message, separators=(",", ":")).encode("utf-8")
    return f"Content-Length: {len(body)}\r\n\r\n".encode("ascii") + body


def decode_frames(data: bytes) -> list[dict[str, Any]]:
    result = []
    at = 0
    while at < len(data):
        header_end = data.index(b"\r\n\r\n", at)
        length = int(data[at:header_end].decode("ascii").split(":", 1)[1].strip())
        begin = header_end + 4
        end = begin + length
        result.append(json.loads(data[begin:end]))
        at = end
    return result


class LanguageBridgeTests(unittest.TestCase):
    def test_diagnostics_completion_definition_and_hover(self) -> None:
        source = (
            "FUNCTION_BLOCK Controller\n"
            "VAR_INPUT Setpoint : REAL; END_VAR\n"
            "END_FUNCTION_BLOCK\n"
            "PROGRAM Main\n"
            "VAR\n"
            "  Counter : INT;\n"
            "  Ctrl : Controller;\n"
            "  Motion : MC_MoveAbsolute;\n"
            "END_VAR\n"
            "Counter := ;\n"
            "Ctrl.Setpoint := 1.0;\n"
            "Motion.Execute := TRUE;\n"
            "END_PROGRAM\n"
        )
        document = pyplcopen._StLanguageDocument(source)

        diagnostics = document.diagnostics()
        self.assertTrue(diagnostics)
        self.assertIn("code", diagnostics[0])
        self.assertIn("range", diagnostics[0])

        line, character = position_of(source, "Counter := ;")
        completion = document.complete(line, character)
        labels = {item["label"].lower() for item in completion["items"]}
        self.assertIn("counter", labels)
        self.assertIn("abs", labels)
        self.assertIn("mc_moveabsolute", labels)

        line, character = position_of(source, "Setpoint := 1.0")
        definition = document.definition(line, character)
        self.assertEqual(
            definition["start"],
            {
                "line": position_of(source, "Setpoint : REAL")[0],
                "character": position_of(source, "Setpoint : REAL")[1],
            },
        )

        line, character = position_of(source, "Execute := TRUE")
        hover = document.hover(line, character)
        self.assertIn("input", hover["contents"])
        self.assertIn("BOOL", hover["contents"])

    def test_update_report_reuses_shifted_pous(self) -> None:
        source = (
            "FUNCTION Scale : REAL\n"
            "VAR_INPUT x : REAL; END_VAR\n"
            "Scale := x;\n"
            "END_FUNCTION\n"
            "PROGRAM Main\n"
            "VAR value : REAL; END_VAR\n"
            "value := Scale(value);\n"
            "END_PROGRAM\n"
        )
        document = pyplcopen._StLanguageDocument(source)
        report = document.update("(* 😀 *)\r\n" + source)
        self.assertEqual(report, {"reparsed_pous": 0, "reused_pous": 2})

        shifted = "(* 😀 *)\r\n" + source
        line, character = position_of(shifted, "Scale(value)")
        definition = document.definition(line, character)
        expected_line, expected_character = position_of(shifted, "Scale : REAL")
        self.assertEqual(
            definition["start"],
            {"line": expected_line, "character": expected_character},
        )

    def test_unknown_position_returns_none(self) -> None:
        document = pyplcopen._StLanguageDocument("PROGRAM Main\nEND_PROGRAM\n")
        self.assertIsNone(document.definition(100, 0))
        self.assertIsNone(document.hover(100, 0))

    def test_real_bridge_runs_through_stdio_session(self) -> None:
        uri = "untitled:real-bridge"
        source = "PROGRAM Main\nVAR Counter : INT; END_VAR\nCounter := ;\nEND_PROGRAM\n"
        messages = [
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
                        "uri": uri,
                        "languageId": "plcopen-st",
                        "version": 1,
                        "text": source,
                    }
                },
            },
            {
                "jsonrpc": "2.0",
                "id": 2,
                "method": "textDocument/completion",
                "params": {
                    "textDocument": {"uri": uri},
                    "position": {"line": 2, "character": 0},
                },
            },
            {
                "jsonrpc": "2.0",
                "id": 3,
                "method": "shutdown",
                "params": None,
            },
            {"jsonrpc": "2.0", "method": "exit"},
        ]
        output = io.BytesIO()
        self.assertEqual(
            run_session(io.BytesIO(b"".join(map(frame, messages))), output),
            0,
        )
        responses = decode_frames(output.getvalue())
        diagnostics = next(
            message["params"]
            for message in responses
            if message.get("method") == "textDocument/publishDiagnostics"
        )
        self.assertTrue(diagnostics["diagnostics"])
        completion = next(
            message["result"] for message in responses if message.get("id") == 2
        )
        self.assertIn(
            "counter",
            {item["label"].lower() for item in completion["items"]},
        )

    def test_module_entrypoint_runs_stdio_session(self) -> None:
        messages = [
            {
                "jsonrpc": "2.0",
                "id": 1,
                "method": "initialize",
                "params": {"capabilities": {}},
            },
            {
                "jsonrpc": "2.0",
                "id": 2,
                "method": "shutdown",
                "params": None,
            },
            {"jsonrpc": "2.0", "method": "exit"},
        ]
        python_paths = [
            str(Path(pyplcopen.__file__).parent),
            str(Path(__file__).parents[1]),
        ]
        environment = os.environ.copy()
        if environment.get("PYTHONPATH"):
            python_paths.append(environment["PYTHONPATH"])
        environment["PYTHONPATH"] = os.pathsep.join(python_paths)

        completed = subprocess.run(
            [sys.executable, "-m", "plcopen_lsp"],
            input=b"".join(map(frame, messages)),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=environment,
            timeout=10,
            check=False,
        )

        self.assertEqual(completed.returncode, 0, completed.stderr.decode())
        responses = decode_frames(completed.stdout)
        self.assertEqual(responses[0]["id"], 1)
        self.assertEqual(responses[1], {"jsonrpc": "2.0", "id": 2, "result": None})


if __name__ == "__main__":
    unittest.main()
