import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import release_dispatch as release


ROOT = Path(__file__).resolve().parents[2]


class ReleaseSourceTests(unittest.TestCase):
    def setUp(self):
        output = ROOT / "out/ci-validation"
        output.mkdir(parents=True, exist_ok=True)
        directory = tempfile.TemporaryDirectory(dir=output)
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.origin = self.root / "origin"
        self.checkout = self.root / "checkout"
        self.run_git("init", "-q", "--initial-branch=main", str(self.origin))
        self.run_git("-C", str(self.origin), "config", "user.name", "Test")
        self.run_git("-C", str(self.origin), "config", "user.email", "test@example.invalid")
        self.run_git("-C", str(self.origin), "-c", "commit.gpgsign=false", "commit", "-qm", "source", "--allow-empty")
        self.sha = self.run_git("-C", str(self.origin), "rev-parse", "HEAD")
        self.lightweight = "pre-release-v7.2.6-1"
        self.annotated = "pre-release-v7.2.6-2"
        self.run_git("-C", str(self.origin), "-c", "tag.gpgsign=false", "tag", self.lightweight)
        self.run_git("-C", str(self.origin), "-c", "tag.gpgsign=false", "tag", "-a", self.annotated, "-m", "release")
        self.run_git("clone", "-q", "--no-tags", str(self.origin), str(self.checkout))
        self.git_patch = patch.object(release, "git", lambda *args: self.run_git("-C", str(self.checkout), *args))
        self.git_patch.start()
        self.addCleanup(self.git_patch.stop)

    def run_git(self, *args):
        return subprocess.check_output(["git", *args], text=True, stderr=subprocess.PIPE).strip()

    def test_lightweight_and_annotated_tags_resolve_to_commit(self):
        for tag in (self.lightweight, self.annotated):
            with self.subTest(tag=tag):
                self.assertEqual(self.sha, release.resolve_source(tag, self.sha))

    def test_main_advancing_does_not_change_release_source(self):
        self.run_git("-C", str(self.origin), "-c", "commit.gpgsign=false", "commit", "-qm", "next", "--allow-empty")
        self.assertEqual(self.sha, release.resolve_source(self.lightweight))

    def test_moved_tag_is_rejected(self):
        self.run_git("-C", str(self.origin), "-c", "commit.gpgsign=false", "commit", "-qm", "next", "--allow-empty")
        self.run_git("-C", str(self.origin), "-c", "tag.gpgsign=false", "tag", "-f", self.lightweight)
        with self.assertRaisesRegex(ValueError, "refusing to release"):
            release.resolve_source(self.lightweight, self.sha)

    def test_invalid_tag_or_short_sha_is_rejected_before_fetch(self):
        with patch.object(release, "git") as git:
            for tag, sha in (("main", ""), (self.lightweight, "abcdef"), ("pre-release-v7.2.6-0", "")):
                with self.subTest(tag=tag, sha=sha), self.assertRaises(ValueError):
                    release.resolve_source(tag, sha)
            git.assert_not_called()

    def test_missing_tag_is_rejected(self):
        with self.assertRaises(subprocess.CalledProcessError):
            release.resolve_source("pre-release-v7.2.6-99")


class DispatchTests(unittest.TestCase):
    def test_event_routing(self):
        self.assertEqual("dispatch", release.route("push", "refs/tags/pre-release-v7.2.6-1", "main"))
        self.assertEqual("prepare", release.route("workflow_dispatch", "refs/heads/main", "main"))
        for event, ref in (("push", "refs/heads/main"), ("workflow_dispatch", "refs/heads/topic"),
                ("workflow_dispatch", "refs/tags/pre-release-v7.2.6-1"), ("pull_request", "refs/heads/main")):
            with self.subTest(event=event, ref=ref), self.assertRaises(ValueError):
                release.route(event, ref, "main")

    @patch.dict(os.environ, {"GH_TOKEN": "test-token", "GITHUB_API_URL": "https://api.github.com",
        "GITHUB_SERVER_URL": "https://github.com"})
    def test_dispatch_posts_once_and_returns_actual_run(self):
        url = "https://github.com/owner/repo/actions/runs/123"
        response = io.BytesIO(json.dumps({"workflow_run_id": 123, "html_url": url}).encode())
        with patch.object(release.urllib.request, "urlopen", return_value=response) as send:
            self.assertEqual(url, release.dispatch("owner/repo", "main", "pre-release-v7.2.6-1", "a" * 40))
        send.assert_called_once()
        request = send.call_args.args[0]
        self.assertEqual("POST", request.method)
        self.assertEqual({"ref": "main", "inputs": {"release_tag": "pre-release-v7.2.6-1", "source_sha": "a" * 40}},
            json.loads(request.data))
        self.assertEqual("2026-03-10", request.get_header("X-github-api-version"))

    @patch.dict(os.environ, {"GH_TOKEN": "test-token"})
    def test_uncertain_dispatch_is_not_retried(self):
        for result in (OSError("timeout"), release.http.client.IncompleteRead(b"{"),
                io.BytesIO(b"{}"), io.BytesIO(b"not json")):
            with self.subTest(result=result):
                options = {"side_effect": result} if isinstance(result, Exception) else {"return_value": result}
                with patch.object(release.urllib.request, "urlopen", **options) as send:
                    with self.assertRaisesRegex(ValueError, "not automatically repeated"):
                        release.dispatch("owner/repo", "main", "pre-release-v7.2.6-1", "a" * 40)
                    send.assert_called_once()

    def run_stage(self, command, event, ref, error=None):
        environment = {"GITHUB_EVENT_NAME": event, "GITHUB_REF": ref, "DEFAULT_BRANCH": "main",
            "RELEASE_TAG": "pre-release-v7.2.6-1", "SOURCE_SHA": "a" * 40,
            "GITHUB_REPOSITORY": "owner/repo", "GITHUB_OUTPUT": os.devnull}
        with patch.dict(os.environ, environment), patch("sys.argv", ["release_dispatch.py", command]), \
                patch.object(release, "git", return_value="a" * 40), \
                patch.object(release, "resolve_source", return_value="a" * 40, side_effect=error), \
                patch.object(release, "append_summary"), patch.object(release, "dispatch") as dispatch, \
                patch("sys.stderr", new_callable=io.StringIO):
            result = release.main()
            return result, dispatch.call_count

    def test_push_dispatches_once(self):
        self.assertEqual((0, 1), self.run_stage("dispatch", "push", "refs/tags/pre-release-v7.2.6-1"))

    def test_main_dispatch_only_prepares(self):
        self.assertEqual((0, 0), self.run_stage("prepare", "workflow_dispatch", "refs/heads/main"))

    def test_invalid_branch_or_sha_stops_pipeline(self):
        self.assertEqual((1, 0), self.run_stage("prepare", "workflow_dispatch", "refs/heads/topic"))
        self.assertEqual((1, 0), self.run_stage("prepare", "workflow_dispatch", "refs/heads/main", ValueError("SHA mismatch")))
        self.assertEqual((1, 0), self.run_stage("dispatch", "workflow_dispatch", "refs/heads/main"))


if __name__ == "__main__":
    unittest.main()
