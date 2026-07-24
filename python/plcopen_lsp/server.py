"""Bounded stdio LSP 3.18 shell for the authoritative C++ ST front end."""

from __future__ import annotations

import json
import sys
from dataclasses import dataclass
from typing import Any, BinaryIO, Callable

MAX_HEADER_BYTES = 8 * 1024
MAX_BODY_BYTES = 32 * 1024 * 1024
MAX_DOCUMENT_BYTES = 4 * 1024 * 1024
MAX_OPEN_DOCUMENTS = 64

PARSE_ERROR = -32700
INVALID_REQUEST = -32600
METHOD_NOT_FOUND = -32601
INVALID_PARAMS = -32602
SERVER_NOT_INITIALIZED = -32002


class ChangeError(ValueError):
    """An incremental change cannot be applied without corrupting text."""


class RecoverableProtocolError(ValueError):
    def __init__(self, code: int, message: str):
        super().__init__(message)
        self.code = code
        self.message = message


class FatalProtocolError(ValueError):
    """A framing limit or truncated stream makes resynchronization unsafe."""


def _position_to_index(text: str, position: dict[str, Any]) -> int:
    try:
        target_line = position["line"]
        target_character = position["character"]
    except (KeyError, TypeError) as error:
        raise ChangeError("position requires line and character") from error
    if (
        not isinstance(target_line, int)
        or isinstance(target_line, bool)
        or not isinstance(target_character, int)
        or isinstance(target_character, bool)
        or target_line < 0
        or target_character < 0
    ):
        raise ChangeError("position must contain non-negative integers")

    index = 0
    line = 0
    while line < target_line and index < len(text):
        if text[index] == "\r" and index + 1 < len(text) and text[index + 1] == "\n":
            index += 2
            line += 1
        elif text[index] == "\n":
            index += 1
            line += 1
        else:
            index += 1
    if line != target_line:
        raise ChangeError("line is outside the document")

    character = 0
    while index < len(text) and text[index] not in "\r\n":
        if character == target_character:
            return index
        width = 2 if ord(text[index]) > 0xFFFF else 1
        if character + width > target_character:
            raise ChangeError("position splits a UTF-16 surrogate pair")
        character += width
        index += 1
    if character != target_character:
        raise ChangeError("character is outside the line")
    return index


def apply_content_changes(text: str, changes: list[dict[str, Any]]) -> str:
    """Apply one didChange batch to a local copy or reject the whole batch."""

    if not isinstance(changes, list):
        raise ChangeError("contentChanges must be an array")
    working = text
    for change in changes:
        if not isinstance(change, dict) or not isinstance(change.get("text"), str):
            raise ChangeError("each content change requires string text")
        if "range" not in change:
            working = change["text"]
            continue
        change_range = change["range"]
        if not isinstance(change_range, dict):
            raise ChangeError("range must be an object")
        start = _position_to_index(working, change_range.get("start"))
        end = _position_to_index(working, change_range.get("end"))
        if end < start:
            raise ChangeError("range end precedes start")
        working = working[:start] + change["text"] + working[end:]
    return working


def _read_exact(stream: BinaryIO, length: int) -> bytes:
    chunks: list[bytes] = []
    remaining = length
    while remaining:
        chunk = stream.read(remaining)
        if not chunk:
            raise FatalProtocolError("truncated JSON-RPC body")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def read_message(stream: BinaryIO) -> dict[str, Any] | None:
    header = bytearray()
    while not header.endswith(b"\r\n\r\n"):
        byte = stream.read(1)
        if not byte:
            if not header:
                return None
            raise FatalProtocolError("truncated JSON-RPC header")
        header.extend(byte)
        if len(header) > MAX_HEADER_BYTES:
            raise FatalProtocolError("JSON-RPC header exceeds 8 KiB")

    try:
        lines = bytes(header[:-4]).decode("ascii").split("\r\n")
    except UnicodeDecodeError as error:
        raise RecoverableProtocolError(
            INVALID_REQUEST, "JSON-RPC header must be ASCII"
        ) from error
    lengths: list[str] = []
    for line in lines:
        if not line or ":" not in line:
            raise RecoverableProtocolError(INVALID_REQUEST, "malformed JSON-RPC header")
        name, value = line.split(":", 1)
        if name.strip().lower() == "content-length":
            lengths.append(value.strip())
    if len(lengths) != 1 or not lengths[0].isdigit():
        raise RecoverableProtocolError(
            INVALID_REQUEST, "exactly one numeric Content-Length is required"
        )
    length = int(lengths[0])
    if length > MAX_BODY_BYTES:
        raise FatalProtocolError("JSON-RPC body exceeds 32 MiB")
    body = _read_exact(stream, length)
    try:
        decoded = body.decode("utf-8")
        message = json.loads(decoded)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise RecoverableProtocolError(PARSE_ERROR, "invalid JSON-RPC body") from error
    if not isinstance(message, dict):
        raise RecoverableProtocolError(
            INVALID_REQUEST, "JSON-RPC batch messages are not supported"
        )
    return message


class MessageWriter:
    def __init__(self, stream: BinaryIO):
        self._stream = stream

    def send(self, message: dict[str, Any]) -> None:
        body = json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode(
            "utf-8"
        )
        self._stream.write(f"Content-Length: {len(body)}\r\n\r\n".encode("ascii"))
        self._stream.write(body)
        self._stream.flush()

    def result(self, request_id: Any, result: Any) -> None:
        self.send({"jsonrpc": "2.0", "id": request_id, "result": result})

    def error(self, request_id: Any, code: int, message: str) -> None:
        self.send(
            {
                "jsonrpc": "2.0",
                "id": request_id,
                "error": {"code": code, "message": message},
            }
        )

    def notification(self, method: str, params: dict[str, Any]) -> None:
        self.send({"jsonrpc": "2.0", "method": method, "params": params})


@dataclass
class DocumentState:
    version: int
    text: str | None
    bridge: Any | None


def _default_document_factory(text: str) -> Any:
    import pyplcopen

    return pyplcopen._StLanguageDocument(text)


class LanguageServer:
    def __init__(
        self,
        writer: MessageWriter,
        document_factory: Callable[[str], Any],
    ):
        self._writer = writer
        self._document_factory = document_factory
        self._documents: dict[str, DocumentState] = {}
        self._initialized = False
        self._shutdown = False

    @property
    def shutdown_requested(self) -> bool:
        return self._shutdown

    def _log_error(self, message: str) -> None:
        self._writer.notification("window/logMessage", {"type": 1, "message": message})

    def _publish(
        self, uri: str, version: int, diagnostics: list[dict[str, Any]]
    ) -> None:
        converted = []
        for diagnostic in diagnostics:
            converted.append(
                {
                    "range": diagnostic["range"],
                    "severity": 2 if diagnostic.get("warning") else 1,
                    "code": diagnostic["code"],
                    "source": "plcopen-st",
                    "message": diagnostic["message"],
                }
            )
        self._writer.notification(
            "textDocument/publishDiagnostics",
            {"uri": uri, "version": version, "diagnostics": converted},
        )

    def _publish_capacity(
        self, uri: str, version: int, code: str, message: str
    ) -> None:
        self._publish(
            uri,
            version,
            [
                {
                    "range": {
                        "start": {"line": 0, "character": 0},
                        "end": {"line": 0, "character": 0},
                    },
                    "code": code,
                    "message": message,
                    "warning": False,
                }
            ],
        )

    @staticmethod
    def _uri(params: dict[str, Any]) -> str:
        uri = params["textDocument"]["uri"]
        if not isinstance(uri, str):
            raise TypeError("textDocument.uri must be a string")
        return uri

    @staticmethod
    def _position(params: dict[str, Any]) -> tuple[int, int]:
        position = params["position"]
        line = position["line"]
        character = position["character"]
        if (
            not isinstance(line, int)
            or isinstance(line, bool)
            or not isinstance(character, int)
            or isinstance(character, bool)
            or line < 0
            or character < 0
        ):
            raise TypeError("position must contain non-negative integers")
        return line, character

    def _did_open(self, params: dict[str, Any]) -> None:
        text_document = params["textDocument"]
        uri = text_document["uri"]
        version = text_document["version"]
        text = text_document["text"]
        if (
            not isinstance(uri, str)
            or not isinstance(version, int)
            or isinstance(version, bool)
            or not isinstance(text, str)
        ):
            raise TypeError("didOpen textDocument fields are invalid")
        if uri in self._documents:
            self._log_error(f"didOpen ignored for already-open URI: {uri}")
            return
        if len(self._documents) >= MAX_OPEN_DOCUMENTS:
            self._publish_capacity(
                uri,
                version,
                "capacity_open_documents",
                "open document capacity exceeded",
            )
            return
        if len(text.encode("utf-8")) > MAX_DOCUMENT_BYTES:
            self._documents[uri] = DocumentState(version, None, None)
            self._publish_capacity(
                uri,
                version,
                "capacity_document_bytes",
                "document exceeds 4 MiB UTF-8 capacity",
            )
            return
        bridge = self._document_factory(text)
        self._documents[uri] = DocumentState(version, text, bridge)
        self._publish(uri, version, bridge.diagnostics())

    def _did_change(self, params: dict[str, Any]) -> None:
        text_document = params["textDocument"]
        uri = text_document["uri"]
        version = text_document["version"]
        changes = params["contentChanges"]
        if (
            not isinstance(uri, str)
            or not isinstance(version, int)
            or isinstance(version, bool)
        ):
            raise TypeError("didChange textDocument fields are invalid")
        state = self._documents.get(uri)
        if state is None:
            self._log_error(f"didChange ignored for unopened URI: {uri}")
            return
        if version <= state.version:
            self._log_error(
                f"didChange version {version} is not newer than {state.version}"
            )
            return
        try:
            if state.text is None:
                if (
                    not isinstance(changes, list)
                    or len(changes) != 1
                    or "range" in changes[0]
                ):
                    raise ChangeError(
                        "over-capacity document requires a full replacement"
                    )
                changed = apply_content_changes("", changes)
            else:
                changed = apply_content_changes(state.text, changes)
        except ChangeError as error:
            self._log_error(f"didChange rejected atomically: {error}")
            return

        if len(changed.encode("utf-8")) > MAX_DOCUMENT_BYTES:
            state.version = version
            state.text = None
            state.bridge = None
            self._publish_capacity(
                uri,
                version,
                "capacity_document_bytes",
                "document exceeds 4 MiB UTF-8 capacity",
            )
            return
        if state.bridge is None:
            state.bridge = self._document_factory(changed)
        else:
            state.bridge.update(changed)
        state.version = version
        state.text = changed
        self._publish(uri, version, state.bridge.diagnostics())

    def _did_close(self, params: dict[str, Any]) -> None:
        uri = self._uri(params)
        state = self._documents.pop(uri, None)
        version = state.version if state is not None else 0
        self._publish(uri, version, [])

    def _completion(self, params: dict[str, Any]) -> dict[str, Any]:
        uri = self._uri(params)
        line, character = self._position(params)
        state = self._documents.get(uri)
        if state is None or state.bridge is None:
            return {"isIncomplete": False, "items": []}
        return state.bridge.complete(line, character)

    def _definition(self, params: dict[str, Any]) -> dict[str, Any] | None:
        uri = self._uri(params)
        line, character = self._position(params)
        state = self._documents.get(uri)
        if state is None or state.bridge is None:
            return None
        target = state.bridge.definition(line, character)
        return None if target is None else {"uri": uri, "range": target}

    def _hover(self, params: dict[str, Any]) -> dict[str, Any] | None:
        uri = self._uri(params)
        line, character = self._position(params)
        state = self._documents.get(uri)
        if state is None or state.bridge is None:
            return None
        hover = state.bridge.hover(line, character)
        if hover is None:
            return None
        return {
            "range": hover["range"],
            "contents": {"kind": "plaintext", "value": hover["contents"]},
        }

    def _initialize_result(self) -> dict[str, Any]:
        return {
            "capabilities": {
                "positionEncoding": "utf-16",
                "textDocumentSync": {"openClose": True, "change": 2},
                "completionProvider": {"triggerCharacters": ["."]},
                "definitionProvider": True,
                "hoverProvider": True,
            },
            "serverInfo": {"name": "plcopen-st-language-server"},
        }

    def handle(self, message: dict[str, Any]) -> int | None:
        request_id = message.get("id")
        is_request = "id" in message
        method = message.get("method")
        if message.get("jsonrpc") != "2.0" or not isinstance(method, str):
            self._writer.error(
                request_id if is_request else None,
                INVALID_REQUEST,
                "invalid JSON-RPC request",
            )
            return None

        if method == "exit" and not is_request:
            return 0 if self._shutdown else 1
        if self._shutdown:
            if is_request:
                self._writer.error(
                    request_id,
                    INVALID_REQUEST,
                    "server has already shut down",
                )
            return None
        if method == "initialize":
            if not is_request or self._initialized:
                if is_request:
                    self._writer.error(
                        request_id, INVALID_REQUEST, "initialize is invalid"
                    )
                return None
            self._initialized = True
            self._writer.result(request_id, self._initialize_result())
            return None
        if not self._initialized:
            if is_request:
                self._writer.error(
                    request_id,
                    SERVER_NOT_INITIALIZED,
                    "server is not initialized",
                )
            return None

        params = message.get("params")
        try:
            if method == "initialized":
                return None
            if method == "shutdown":
                if not is_request:
                    return None
                self._shutdown = True
                self._writer.result(request_id, None)
                return None
            if method == "textDocument/didOpen":
                self._did_open(params)
                return None
            if method == "textDocument/didChange":
                self._did_change(params)
                return None
            if method == "textDocument/didClose":
                self._did_close(params)
                return None
            if method == "textDocument/completion" and is_request:
                self._writer.result(request_id, self._completion(params))
                return None
            if method == "textDocument/definition" and is_request:
                self._writer.result(request_id, self._definition(params))
                return None
            if method == "textDocument/hover" and is_request:
                self._writer.result(request_id, self._hover(params))
                return None
        except (KeyError, TypeError, ChangeError) as error:
            if is_request:
                self._writer.error(request_id, INVALID_PARAMS, str(error))
            else:
                self._log_error(f"{method} ignored: {error}")
            return None

        if is_request:
            self._writer.error(
                request_id, METHOD_NOT_FOUND, f"unknown method: {method}"
            )
        return None


def run_session(
    input_stream: BinaryIO,
    output_stream: BinaryIO,
    *,
    document_factory: Callable[[str], Any] = _default_document_factory,
) -> int:
    writer = MessageWriter(output_stream)
    server = LanguageServer(writer, document_factory)
    while True:
        try:
            message = read_message(input_stream)
        except RecoverableProtocolError as error:
            writer.error(None, error.code, error.message)
            continue
        except FatalProtocolError:
            return 1
        if message is None:
            return 0 if server.shutdown_requested else 1
        exit_code = server.handle(message)
        if exit_code is not None:
            return exit_code


def main() -> int:
    return run_session(sys.stdin.buffer, sys.stdout.buffer)
