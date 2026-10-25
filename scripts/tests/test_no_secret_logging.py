#!/usr/bin/env python3
"""Reject secret-bearing expressions in C++ diagnostic/logging calls."""

from __future__ import annotations

import re
import sys
from pathlib import Path


LOGGER = re.compile(
    r"\b(?:conPrintF?|qDebug|qInfo|qWarning|qCritical|qFatal|"
    r"LOG_(?:TRACE|DEBUG|INFO|WARN|WARNING|ERROR|FATAL)|"
    r"appendDiagnostic|logLifecycle(?:Start|Timing)|"
    r"std::(?:cout|cerr|clog))\b"
)
SECRET = re.compile(
    r"\b(?:password|passwd|token|secret|api[_]?key|access[_]?key|"
    r"refresh[_]?token|authorization|credential)\b",
    re.IGNORECASE,
)


def mask_comments_and_literals(source: str) -> str:
    """Blank comments and literals while retaining line positions and code."""
    output = list(source)
    i = 0
    state = "code"
    while i < len(source):
        ch = source[i]
        nxt = source[i + 1] if i + 1 < len(source) else ""
        if state == "code":
            if ch == "/" and nxt == "/":
                output[i] = output[i + 1] = " "
                i += 2
                state = "line_comment"
                continue
            if ch == "/" and nxt == "*":
                output[i] = output[i + 1] = " "
                i += 2
                state = "block_comment"
                continue
            if ch == '"':
                output[i] = " "
                i += 1
                state = "string"
                continue
            if ch == "'":
                output[i] = " "
                i += 1
                state = "char"
                continue
        elif state == "line_comment":
            if ch == "\n":
                state = "code"
            else:
                output[i] = " "
        elif state == "block_comment":
            if ch == "*" and nxt == "/":
                output[i] = output[i + 1] = " "
                i += 2
                state = "code"
                continue
            if ch != "\n":
                output[i] = " "
        else:
            if ch == "\\" and i + 1 < len(source):
                if ch != "\n":
                    output[i] = " "
                if source[i + 1] != "\n":
                    output[i + 1] = " "
                i += 2
                continue
            if (state == "string" and ch == '"') or (state == "char" and ch == "'"):
                output[i] = " "
                state = "code"
            elif ch != "\n":
                output[i] = " "
        i += 1
    return "".join(output)


def call_end(source: str, open_paren: int) -> int | None:
    depth = 0
    for index in range(open_paren, len(source)):
        if source[index] == "(":
            depth += 1
        elif source[index] == ")":
            depth -= 1
            if depth == 0:
                return index + 1
    return None


def scan_source(source: str, path_label: str) -> list[str]:
    code = mask_comments_and_literals(source)
    findings: list[str] = []
    for match in LOGGER.finditer(code):
        sink = match.group(0)
        pos = match.end()
        while pos < len(code) and code[pos].isspace():
            pos += 1
        if sink.startswith("std::"):
            end = code.find(";", pos)
            expression = code[match.start() : end if end >= 0 else len(code)]
        else:
            if pos >= len(code) or code[pos] != "(":
                continue
            end = call_end(code, pos)
            if end is None:
                continue
            expression = code[match.start() : end]
            # Qt logging commonly streams values after qDebug().
            tail = code[end:]
            stream = re.match(r"\s*<<.*?;", tail, re.DOTALL)
            if stream:
                expression += stream.group(0)
        secret = SECRET.search(expression)
        if secret:
            line = code.count("\n", 0, match.start()) + 1
            findings.append(f"{path_label}:{line}: {sink} references sensitive identifier {secret.group(0)}")
    return findings


def scan_file(path: Path, root: Path) -> list[str]:
    source = path.read_text(encoding="utf-8", errors="replace")
    return scan_source(source, str(path.relative_to(root)))


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_no_secret_logging.py REPOSITORY_ROOT", file=sys.stderr)
        return 2
    root = Path(sys.argv[1]).resolve()
    positive_fixture = 'void sample() { conPrint("password: " + password); }'
    negative_fixture = '// conPrint("password: " + password);\nconst char* label = "password";'
    if not scan_source(positive_fixture, "scanner-self-test"):
        print("Secret-logging scanner self-test did not detect a sensitive logger argument.", file=sys.stderr)
        return 1
    if scan_source(negative_fixture, "scanner-self-test"):
        print("Secret-logging scanner self-test treated a comment/literal as a logged value.", file=sys.stderr)
        return 1
    source_roots = [root / "gui_client", root / "neurocontrol"]
    files = [
        path
        for base in source_roots
        if base.exists()
        for path in base.rglob("*")
        if path.suffix.lower() in {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp"}
    ]
    findings = [finding for path in files for finding in scan_file(path, root)]
    if findings:
        print("Potential secret-bearing log expressions found:")
        print("\n".join(findings))
        return 1
    print(f"No secret-bearing expressions found in logging calls across {len(files)} C/C++ source files.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
