#!/usr/bin/env python3
"""Execute the repository-owned code cells of a notebook without Jupyter."""

from __future__ import annotations

import json
from pathlib import Path
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: notebook_smoke.py NOTEBOOK MODULE_DIR", file=sys.stderr)
        return 2

    notebook = Path(sys.argv[1])
    module_dir = Path(sys.argv[2])
    payload = json.loads(notebook.read_text(encoding="utf-8"))
    cells = payload.get("cells")
    if not isinstance(cells, list):
        print("notebook has no cells list", file=sys.stderr)
        return 1

    sys.path.insert(0, str(module_dir))
    namespace = {"__name__": "__main__"}
    executed = 0
    for index, cell in enumerate(cells, 1):
        if cell.get("cell_type") != "code":
            continue
        tags = cell.get("metadata", {}).get("tags", [])
        if "skip-smoke" in tags:
            continue
        source = "".join(cell.get("source", []))
        if not source.strip():
            continue
        exec(compile(source, f"{notebook.name}:cell-{index}", "exec"), namespace)
        executed += 1

    if executed == 0:
        print("notebook has no executable code cells", file=sys.stderr)
        return 1
    print(f"notebook smoke: {notebook} cells={executed}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
