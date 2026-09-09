import argparse
import http.client
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import urllib.request

from release_version import TAG_PATTERN


def route(event, ref, default_branch):
    if event == "push" and ref.startswith("refs/tags/"):
        return "dispatch"
    if event == "workflow_dispatch" and ref == f"refs/heads/{default_branch}":
        return "prepare"
    raise ValueError("Run Prerelease from the default branch and supply release_tag.")


def git(*arguments):
    return subprocess.check_output(["git", *arguments], text=True).strip()


def resolve_source(tag, expected_sha=""):
    if not TAG_PATTERN.fullmatch(tag):
        raise ValueError("release_tag must match pre-release-v<base>-<revision> (1..99)")
    if expected_sha and not re.fullmatch(r"[0-9a-fA-F]{40}", expected_sha):
        raise ValueError("source_sha must be a complete commit SHA")
    ref = f"refs/tags/{tag}"
    git("fetch", "--no-tags", "--depth=1", "origin", f"+{ref}:{ref}")
    source_sha = git("rev-parse", "--verify", f"{ref}^{{commit}}")
    if expected_sha and source_sha != expected_sha.lower():
        raise ValueError(f"Tag {tag} resolves to {source_sha}, not {expected_sha}; refusing to release")
    return source_sha


def dispatch(repository, default_branch, tag, source_sha):
    server = os.environ.get("GITHUB_SERVER_URL", "https://github.com")
    api = os.environ.get("GITHUB_API_URL", "https://api.github.com")
    request = urllib.request.Request(
        f"{api}/repos/{repository}/actions/workflows/pre-release.yml/dispatches",
        data=json.dumps({
            "ref": default_branch,
            "inputs": {"release_tag": tag, "source_sha": source_sha},
        }).encode(),
        headers={
            "Accept": "application/vnd.github+json",
            "Content-Type": "application/json",
            "Authorization": f"Bearer {os.environ['GH_TOKEN']}",
            "X-GitHub-Api-Version": "2026-03-10",
        },
        method="POST",
    )
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            result = json.load(response)
        run_id = result["workflow_run_id"]
        url = result["html_url"]
        if type(run_id) is not int or run_id <= 0 or url != f"{server}/{repository}/actions/runs/{run_id}":
            raise ValueError("Unexpected dispatch response")
    except (OSError, ValueError, KeyError, TypeError, http.client.HTTPException) as error:
        raise ValueError(
            "Dispatch failed or its result is uncertain. Check Actions before retrying; "
            "the request was not automatically repeated."
        ) from error
    return url


def append_summary(lines):
    with Path(os.environ["GITHUB_STEP_SUMMARY"]).open("a", encoding="utf-8") as output:
        output.write("\n".join(lines) + "\n")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=["dispatch", "prepare"])
    args = parser.parse_args()
    try:
        default_branch = os.environ["DEFAULT_BRANCH"]
        event = os.environ["GITHUB_EVENT_NAME"]
        ref = os.environ["GITHUB_REF"]
        if route(event, ref, default_branch) != args.command:
            raise ValueError("This event must not execute this release stage")
        tag = ref.removeprefix("refs/tags/") if args.command == "dispatch" else os.environ["RELEASE_TAG"]
        expected = git("rev-parse", "HEAD") if args.command == "dispatch" else os.environ.get("SOURCE_SHA", "")
        source_sha = resolve_source(tag, expected)
        lines = ["## Prerelease source", "", f"- Workflow ref: `{ref}`",
            f"- Release tag: `{tag}`", f"- Source commit: `{source_sha}`"]
        if args.command == "dispatch":
            url = dispatch(os.environ["GITHUB_REPOSITORY"], default_branch, tag, source_sha)
            lines.extend([f"- [Actual build and release]({url})", "",
                "Dispatch accepted. Follow the linked run for the build and publication result."])
        else:
            with Path(os.environ["GITHUB_OUTPUT"]).open("a", encoding="utf-8") as output:
                output.write(f"source_sha={source_sha}\n")
        append_summary(lines)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(f"::error::{error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
