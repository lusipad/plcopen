# ST Language Server and VS Code

D1 adds diagnostics, completion, go-to-definition, and hover for PLCopen
Structured Text. The C++ front end remains the language authority; a
zero-third-party Python package exposes an LSP 3.18 stdio server, and a thin
VS Code extension connects the official language client.

!!! warning "Source snapshot only"
    D1 is not part of the published `pyplcopen==0.20.0` package and the VS Code
    extension is not published to a marketplace. Build the wheel and VSIX from
    the same repository checkout, or download the VSIX artifact from a
    successful Language Tools workflow.

## Requirements

- Python 3.10+ with a C++17 build toolchain and CMake
- Node.js 20+ for local VSIX packaging
- VS Code 1.91+

## Build and install the server

Use an isolated interpreter, then build the wheel from the repository root:

```bash
python -m pip install build
python -m build --wheel --outdir dist
python -m pip install --force-reinstall --no-deps dist/pyplcopen-*.whl
```

On PowerShell, resolve the wheel without a shell glob:

```powershell
$Wheel = Get-ChildItem dist -Filter 'pyplcopen-*.whl' | Select-Object -First 1
python -m pip install --force-reinstall --no-deps $Wheel.FullName
```

Verify that this interpreter contains both parts:

```bash
python -c "import pyplcopen, plcopen_lsp; print(pyplcopen.__file__)"
```

The server is normally launched by an editor. Its direct entry point is
`python -m plcopen_lsp`; it communicates only through LSP
`Content-Length` frames on stdin/stdout.

## Build and install the VSIX

```bash
cd editors/vscode
npm ci --ignore-scripts
npm run verify:licenses
npm test
npm audit --omit=dev
npm run package
npm run verify:vsix
```

Install `plcopen-st-language-tools.vsix` with **Extensions: Install from
VSIX...**. Set the machine-scoped `plcopenSt.pythonPath` setting to the exact
interpreter used above. The extension deliberately does not discover or run a
workspace `.venv`.

Open a `.st` file, or choose the **PLCopen ST** language mode for an untitled
document. Both schemes connect to the same language client.

## Trust and failure behavior

- In an untrusted workspace, VS Code still registers `.st`, comments, and
  brackets, but the extension does not create a Python process.
- After workspace trust is granted, the extension starts
  `<python> -m plcopen_lsp`.
- A missing module or invalid interpreter is reported in the PLCopen ST output
  channel and an error notification, including the wheel-install command.
- The server treats document URIs as opaque. It analyzes only text sent by the
  client and does not read unopened files, access the network, execute ST, or
  run workspace commands.

## v1 scope

One open document may contain types, globals, functions, function blocks,
programs, and SFC. Diagnostics remain available for incomplete source, while
the tolerant index supplies the symbols it can still prove.

The first version does not provide cross-file projects, workspace symbols,
references, rename, formatting, semantic tokens, a debugger, online PLC
access, or a TextMate grammar. Definitions never leave the current URI.

The exact protocol, UTF-16 update, capacity, ambiguity, and dependency rules
are recorded in the
[D1 semantics matrix](https://github.com/lusipad/plcopen/blob/main/doc/compliance/st-language-server-semantics.md).
