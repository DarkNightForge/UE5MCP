#!/usr/bin/env python3
"""Public-safety scan for the OrbitForge repository.

Runs three checks:

1. Leak patterns over every text file: absolute home paths (POSIX and
   Windows/UNC), ssh-style remotes, email addresses, credential-looking
   assignments (quoted or unquoted), private key headers, and — when present —
   a maintainer term denylist kept OUTSIDE this repository (path taken from
   the ORBITFORGE_DENYLIST environment variable, falling back to
   ~/.config/orbitforge/denylist.txt).
2. Relative links in markdown files must resolve to existing files.
3. Git history: every commit reachable from any ref or the reflog is scanned —
   patch content against the same leak patterns and denylist, and author
   identities, which must use a noreply address.

A line containing the pragma marker (see PRAGMA below) is exempt from leak
patterns. The exemption is line-wide: a real secret sharing a line with the
pragma would be missed, so use it sparingly and only for lines that discuss
patterns rather than contain real values.

Exit codes: 0 clean, 1 findings, 2 usage error.
"""

import os
import re
import subprocess
import sys

# Assembled so this file never contains the literal marker it searches for.
PRAGMA = "public-safety:" + " allow"

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKIP_DIRS = {".git", ".venv", "venv", "node_modules", "__pycache__", ".pytest_cache"}
DEFAULT_DENYLIST = os.path.join(
    os.path.expanduser("~"), ".config", "orbitforge", "denylist.txt"
)

LEAK_PATTERNS = [
    ("home-path", re.compile(r"(?i)/(?:home|users)/[A-Za-z0-9._-]+")),
    ("win-path", re.compile(r"(?i)\b[A-Za-z]:\\(?:Users|home)\\[^\\\s]+")),
    ("unc-path", re.compile(r"\\\\[\w.-]+\\[\w$][\w$.-]*")),
    ("ssh-remote", re.compile(r"(?:git@[\w.-]+:|ssh:" + r"//)")),
    ("email", re.compile(r"\b[A-Za-z0-9._%+-]+@(?!example\.(?:com|org)\b)[A-Za-z0-9.-]+\.[A-Za-z]{2,}\b")),
    ("secret", re.compile(r"(?i)\b(?:api[_-]?key|secret|token|passwd|password)\b\s*[:=]\s*['\"]?[A-Za-z0-9._+/-]{8,}")),
    ("aws-key", re.compile(r"\bAKIA[0-9A-Z]{16}\b")),
    ("private-key", re.compile(r"-----BEGIN [A-Z ]*PRIVATE KEY-----")),
]

NOREPLY_SUFFIX = "users.noreply.github.com"

LINK_RE = re.compile(r"\[[^\]]*\]\(([^)\s]+)\)")
SKIP_LINK_PREFIXES = ("http://", "https://", "mailto:", "#")


def load_denylist(path=None):
    """Return compiled (term, regex) pairs from the external denylist, or []."""
    path = path or os.environ.get("ORBITFORGE_DENYLIST") or DEFAULT_DENYLIST
    terms = []
    if not os.path.isfile(path):
        return terms, path
    with open(path, encoding="utf-8") as fh:
        for raw in fh:
            term = raw.strip()
            if not term or term.startswith("#"):
                continue
            escaped = re.escape(term)
            if re.fullmatch(r"\w+", term):
                escaped = r"\b" + escaped + r"\b"
            terms.append((term, re.compile(escaped, re.IGNORECASE)))
    return terms, path


def _git_listed_files(root):
    """Files git would track or could add: cached + untracked-but-not-ignored.
    Returns absolute paths, or None when root is not a git repo. This deliberately
    EXCLUDES gitignored paths (build output like Binaries/Intermediate, caches) —
    they are never published, so scanning them only yields false positives."""
    if not os.path.isdir(os.path.join(root, ".git")):
        return None
    proc = subprocess.run(
        ["git", "-C", root, "ls-files", "--cached", "--others", "--exclude-standard", "-z"],
        capture_output=True, text=True,
    )
    if proc.returncode != 0:
        return None
    return [os.path.join(root, rel) for rel in proc.stdout.split("\0") if rel]


def iter_text_files(root):
    listed = _git_listed_files(root)
    if listed is not None:
        for path in listed:
            try:
                with open(path, encoding="utf-8") as fh:
                    yield path, fh.read()
            except (UnicodeDecodeError, OSError):
                continue
        return

    # Non-repo fallback: walk the tree, pruning known noise directories.
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for name in filenames:
            path = os.path.join(dirpath, name)
            try:
                with open(path, encoding="utf-8") as fh:
                    yield path, fh.read()
            except (UnicodeDecodeError, OSError):
                continue


def scan_leaks(path, text, denylist):
    findings = []
    rel = os.path.relpath(path, REPO_ROOT)
    for lineno, line in enumerate(text.splitlines(), 1):
        if PRAGMA in line:
            continue
        for category, pattern in LEAK_PATTERNS:
            match = pattern.search(line)
            if match:
                findings.append((rel, lineno, category, match.group(0)))
        for term, pattern in denylist:
            if pattern.search(line):
                findings.append((rel, lineno, "denylist-term", term))
    return findings


def scan_links(path, text):
    findings = []
    if not path.endswith(".md"):
        return findings
    rel = os.path.relpath(path, REPO_ROOT)
    base = os.path.dirname(path)
    for lineno, line in enumerate(text.splitlines(), 1):
        for target in LINK_RE.findall(line):
            if target.startswith(SKIP_LINK_PREFIXES):
                continue
            clean = target.split("#", 1)[0]
            if not clean:
                continue
            resolved = os.path.normpath(os.path.join(base, clean))
            if not os.path.exists(resolved):
                findings.append((rel, lineno, "broken-link", target))
    return findings


def scan_history(root, denylist):
    """Scan all commits (refs + reflog) for leak patterns and author identity."""
    findings = []
    if not os.path.isdir(os.path.join(root, ".git")):
        return findings

    def git(*args):
        proc = subprocess.run(
            ["git", "-C", root, *args], capture_output=True, text=True
        )
        return proc.stdout if proc.returncode == 0 else ""

    log_args = ("log", "--all", "--reflog")
    for line in git(*log_args, "--format=%h %an <%ae>").splitlines():
        line = line.strip()
        if line and not line.rstrip(">").endswith(NOREPLY_SUFFIX):
            findings.append(("git-history", 0, "history-author", line))

    for lineno, line in enumerate(
        git(*log_args, "-p", "--format=commit %h %s").splitlines(), 1
    ):
        if PRAGMA in line:
            continue
        for category, pattern in LEAK_PATTERNS:
            match = pattern.search(line)
            if match:
                findings.append(("git-history", lineno, "history-" + category, match.group(0)))
        for term, pattern in denylist:
            if pattern.search(line):
                findings.append(("git-history", lineno, "history-denylist", term))
    return findings


def run(root=None, denylist_path=None, include_history=True):
    root = root or REPO_ROOT
    denylist, used_path = load_denylist(denylist_path)
    findings = []
    for path, text in iter_text_files(root):
        findings.extend(scan_leaks(path, text, denylist))
        findings.extend(scan_links(path, text))
    if include_history:
        findings.extend(scan_history(root, denylist))
    return findings, denylist, used_path


def main(argv):
    if len(argv) > 1:
        print(__doc__)
        return 2
    findings, denylist, used_path = run()
    for rel, lineno, category, detail in findings:
        print(f"{rel}:{lineno}: [{category}] {detail}")
    if not denylist:
        print(f"note: no denylist at {used_path} — ran generic checks only")
    if findings:
        print(f"FAIL: {len(findings)} finding(s)")
        return 1
    print("OK: no leak patterns, no broken relative links")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
