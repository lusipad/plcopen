"""Execute every Python example in the public getting-started guide."""

from __future__ import annotations

import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: docs_python_smoke.py GUIDE MODULE_DIR", file=sys.stderr)
        return 2

    guide = Path(sys.argv[1])
    module_dir = Path(sys.argv[2])
    blocks = re.findall(
        r"```python\n(.*?)\n```", guide.read_text(encoding="utf-8"), re.DOTALL
    )
    if not blocks:
        print("no Python examples found", file=sys.stderr)
        return 1

    environment = os.environ.copy()
    environment["PYTHONPATH"] = str(module_dir)
    with tempfile.TemporaryDirectory(prefix="plcopen-python-docs-") as workdir:
        for index, block in enumerate(blocks, 1):
            result = subprocess.run(
                [sys.executable, "-c", block],
                cwd=workdir,
                env=environment,
                check=False,
            )
            if result.returncode != 0:
                print(f"Python guide example {index} failed", file=sys.stderr)
                return result.returncode

    print(f"Python guide smoke test passed ({len(blocks)} examples)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
