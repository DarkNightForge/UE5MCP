import os
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "scripts"))

import check_public_safety as cps

# Leaky fixture strings are assembled at runtime so this test file itself
# stays clean under the scanner.
HOME_PATH = "/ho" + "me/somebody/secret-project"
WIN_PATH = "C:\\" + "Users\\" + "somebody\\private-project"
EMAIL = "alice" + "@" + "private-corp.com"
EXAMPLE_EMAIL = "alice" + "@" + "example.com"
SSH_REMOTE = "git@" + "github.com:" + "someone/private-repo.git"
ENV_SECRET = "API_" + "KEY=sk-abcdef1234567890"
YAML_SECRET = "tok" + "en: ghp_abcdefABCDEF0123"
PRAGMA_LINE = "discusses /ho" + "me/ paths " + cps.PRAGMA


def categories(findings):
    return [f[2] for f in findings]


class LeakScanTests(unittest.TestCase):
    def test_home_path_detected(self):
        findings = cps.scan_leaks("f.md", f"see {HOME_PATH} for details", [])
        self.assertIn("home-path", categories(findings))

    def test_home_path_case_insensitive(self):
        findings = cps.scan_leaks("f.md", f"see {HOME_PATH.upper()}", [])
        self.assertIn("home-path", categories(findings))

    def test_windows_path_detected(self):
        findings = cps.scan_leaks("f.md", f"built from {WIN_PATH}", [])
        self.assertIn("win-path", categories(findings))

    def test_unquoted_secrets_detected(self):
        self.assertIn("secret", categories(cps.scan_leaks(".env", ENV_SECRET, [])))
        self.assertIn("secret", categories(cps.scan_leaks("c.yml", YAML_SECRET, [])))

    def test_email_detected_but_example_domain_exempt(self):
        findings = cps.scan_leaks("f.md", f"contact {EMAIL}", [])
        self.assertIn("email", categories(findings))
        findings = cps.scan_leaks("f.md", f"contact {EXAMPLE_EMAIL}", [])
        self.assertEqual(findings, [])

    def test_ssh_remote_detected(self):
        findings = cps.scan_leaks("f.md", f"clone {SSH_REMOTE}", [])
        self.assertIn("ssh-remote", categories(findings))

    def test_secret_assignment_detected(self):
        line = "api_" + "key" + ' = "abcdef123456789"'
        findings = cps.scan_leaks("f.py", line, [])
        self.assertIn("secret", categories(findings))

    def test_pragma_exempts_line(self):
        findings = cps.scan_leaks("f.md", PRAGMA_LINE, [])
        self.assertEqual(findings, [])

    def test_denylist_term_word_boundary(self):
        import re

        denylist = [("forge", re.compile(r"\bforge\b", re.IGNORECASE))]
        findings = cps.scan_leaks("f.md", "the Forge rises", denylist)
        self.assertIn("denylist-term", categories(findings))
        findings = cps.scan_leaks("f.md", "OrbitForge rises", denylist)
        self.assertEqual(findings, [])

    def test_load_denylist_missing_file(self):
        terms, path = cps.load_denylist("/nonexistent/denylist.txt")
        self.assertEqual(terms, [])


class LinkCheckTests(unittest.TestCase):
    def test_broken_and_valid_relative_links(self):
        with tempfile.TemporaryDirectory() as tmp:
            target = os.path.join(tmp, "real.md")
            with open(target, "w", encoding="utf-8") as fh:
                fh.write("# real\n")
            doc = os.path.join(tmp, "doc.md")
            text = "[ok](real.md) and [bad](missing.md) and [web](https://example.org)"
            with open(doc, "w", encoding="utf-8") as fh:
                fh.write(text)
            findings = cps.scan_links(doc, text)
            self.assertEqual(categories(findings), ["broken-link"])
            self.assertEqual(findings[0][3], "missing.md")

    def test_non_markdown_skipped(self):
        self.assertEqual(cps.scan_links("f.py", "[x](missing.md)"), [])


class HistoryScanTests(unittest.TestCase):
    def test_history_scan_flags_leaky_commit_and_author(self):
        with tempfile.TemporaryDirectory() as tmp:
            def git(*args):
                subprocess.run(
                    ["git", "-C", tmp, "-c", "commit.gpgsign=false", *args],
                    check=True,
                    capture_output=True,
                )

            git("init", "-q")
            with open(os.path.join(tmp, "notes.md"), "w", encoding="utf-8") as fh:
                fh.write(f"work happens at {HOME_PATH}\n")
            git("add", "notes.md")
            git(
                "-c", "user.name=Tester", "-c", "user.email=" + EMAIL,
                "commit", "-q", "-m", "init",
            )
            cats = categories(cps.scan_history(tmp, []))
            self.assertIn("history-author", cats)
            self.assertIn("history-home-path", cats)

    def test_history_scan_skips_non_repo(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(cps.scan_history(tmp, []), [])


class RepoIsCleanTest(unittest.TestCase):
    def test_working_tree_is_clean_with_generic_checks(self):
        findings, _, _ = cps.run(
            denylist_path="/nonexistent/denylist.txt", include_history=False
        )
        self.assertEqual(findings, [], f"repo has findings: {findings}")


if __name__ == "__main__":
    unittest.main()
