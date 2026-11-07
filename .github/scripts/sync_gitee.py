#!/usr/bin/env python3
"""Mirror public Gitee code and PRs without merging PRs or invoking a model.

Only gitee/branches/*, gitee/pr/* and tags gitee/* are managed. The default
GitHub branch remains the trusted controller and is never overwritten.
"""

import argparse
import base64
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request


SOURCE = "ZenithGrid/ZenithGrid"
DESTINATION = "murreletfeather/ZenithGrid"
GITEE_GIT = f"https://gitee.com/{SOURCE}.git"
MARKER = re.compile(r"\A<!-- gitee-mirror:v1 ZenithGrid/ZenithGrid#([1-9][0-9]*) -->\n")


class SyncError(RuntimeError):
    pass


class Api:
    def __init__(self, root, token=None):
        self.root = root
        self.token = token

    def request(self, method, path, data=None):
        headers = {"User-Agent": "ZenithGrid-Gitee-Mirror", "Accept": "application/json"}
        if self.token:
            headers["Authorization"] = f"Bearer {self.token}"
            headers["X-GitHub-Api-Version"] = "2022-11-28"
        encoded = None if data is None else json.dumps(data).encode("utf-8")
        if encoded is not None:
            headers["Content-Type"] = "application/json"
        for attempt in range(3):
            request = urllib.request.Request(self.root + path, encoded, headers, method=method)
            try:
                with urllib.request.urlopen(request, timeout=45) as response:
                    body = response.read()
                    return json.loads(body) if body else None
            except urllib.error.HTTPError as exc:
                detail = exc.read().decode("utf-8", errors="replace")
                if method == "GET" and exc.code in (429, 500, 502, 503, 504) and attempt < 2:
                    time.sleep(2 ** (attempt + 1))
                    continue
                raise SyncError(f"{method} {path}: HTTP {exc.code}: {detail[:1500]}") from exc
            except (urllib.error.URLError, TimeoutError) as exc:
                # Writes may already have succeeded, so only retry reads.
                if method == "GET" and attempt < 2:
                    time.sleep(2 ** (attempt + 1))
                    continue
                raise SyncError(f"{method} {path}: {exc}") from exc
        raise SyncError("API retry exhausted")

    def pages(self, path):
        result = []
        separator = "&" if "?" in path else "?"
        for page in range(1, 1001):
            items = self.request("GET", f"{path}{separator}per_page=100&page={page}")
            if not isinstance(items, list):
                raise SyncError(f"Expected an array for {path}; refusing a partial sync")
            result.extend(items)
            if len(items) < 100:
                return result
        raise SyncError("Pagination did not finish; refusing a partial sync")


def managed_number(pr):
    match = MARKER.match(pr.get("body") or "")
    if not match:
        return None
    number = int(match.group(1))
    head = pr.get("head") or {}
    base = pr.get("base") or {}
    if head.get("ref") != f"gitee/pr/{number}":
        return None
    for branch in (head, base):
        if (branch.get("repo") or {}).get("full_name", "").lower() != DESTINATION.lower():
            return None
    if not base.get("ref", "").startswith("gitee/branches/"):
        return None
    return number


def pr_body(source_pr):
    number = int(source_pr["number"])
    return (
        f"<!-- gitee-mirror:v1 {SOURCE}#{number} -->\n"
        f"Mirror of [Gitee !{number}](https://gitee.com/{SOURCE}/pulls/{number}).\n\n"
        f"Source author: `{source_pr['user']['login']}` · Source state: **{source_pr['state']}**\n\n"
        "Code and lifecycle are managed on Gitee. Do not merge this GitHub PR. "
        "Review findings stay on GitHub; discussion and approvals are not copied. "
        "To request native Codex review, manually comment `@codex review` here.\n\n"
        "---\n\n" + (source_pr.get("body") or "")
    )[:60000]


class Mirror:
    def __init__(self, repo_dir, token, dry_run=False):
        self.repo_dir = Path(repo_dir).resolve()
        self.token = token
        self.dry_run = dry_run
        self.gitee = Api("https://gitee.com/api/v5")
        self.github = Api("https://api.github.com", token)
        self.messages = []
        self.errors = []
        self.git_env = dict(os.environ, GIT_TERMINAL_PROMPT="0")
        if token:
            authorization = base64.b64encode(f"x-access-token:{token}".encode()).decode()
            offset = int(self.git_env.get("GIT_CONFIG_COUNT", "0"))
            self.git_env.update({
                "GIT_CONFIG_COUNT": str(offset + 1),
                f"GIT_CONFIG_KEY_{offset}": "http.https://github.com/.extraheader",
                f"GIT_CONFIG_VALUE_{offset}": f"AUTHORIZATION: basic {authorization}",
            })
        self.refs = {}

    def log(self, message):
        self.messages.append(message)
        print(message, flush=True)

    def git(self, *args, check=True):
        result = subprocess.run(
            ["git", "-C", str(self.repo_dir), *args], env=self.git_env,
            text=True, encoding="utf-8", errors="replace", capture_output=True, timeout=300,
        )
        if check and result.returncode:
            raise SyncError(f"git {' '.join(args)}: {result.stderr.strip()}")
        return result

    def initialize(self):
        self.repo_dir.mkdir(parents=True, exist_ok=True)
        if not (self.repo_dir / "HEAD").exists() and not (self.repo_dir / ".git").exists():
            self.git("init", "--bare")
        result = self.git("ls-remote", "--refs", f"https://github.com/{DESTINATION}.git",
                          "refs/heads/gitee/*", "refs/tags/gitee/*")
        for line in result.stdout.splitlines():
            sha, ref = line.split("\t", 1)
            self.refs[ref] = sha

    def push(self, updates):
        updates = {ref: sha for ref, sha in updates.items() if self.refs.get(ref) != sha}
        if not updates:
            return
        for ref in updates:
            if not ref.startswith(("refs/heads/gitee/branches/", "refs/heads/gitee/pr/", "refs/tags/gitee/")):
                raise SyncError(f"Refusing to write unmanaged ref {ref}")
        self.log(f"{'Would update' if self.dry_run else 'Updating'} {len(updates)} mirror ref(s)")
        if not self.dry_run:
            leases = [f"--force-with-lease={ref}:{self.refs.get(ref, '')}" for ref in updates]
            refspecs = [f"{sha}:{ref}" for ref, sha in updates.items()]
            self.git("push", "--atomic", *leases, f"https://github.com/{DESTINATION}.git", *refspecs)
        self.refs.update(updates)

    def sync_source(self):
        self.git("fetch", "--no-tags", GITEE_GIT,
                 "+refs/heads/*:refs/mirror-source/heads/*",
                 "+refs/tags/*:refs/mirror-source/tags/*")
        result = self.git("for-each-ref", "--format=%(objectname) %(refname)",
                          "refs/mirror-source/heads/", "refs/mirror-source/tags/")
        updates = {}
        for line in result.stdout.splitlines():
            sha, ref = line.split(" ", 1)
            if ref.startswith("refs/mirror-source/heads/"):
                destination = "refs/heads/gitee/branches/" + ref.removeprefix("refs/mirror-source/heads/")
            else:
                destination = "refs/tags/gitee/" + ref.removeprefix("refs/mirror-source/tags/")
            updates[destination] = sha
        if not any(ref.startswith("refs/heads/gitee/branches/") for ref in updates):
            raise SyncError("Gitee returned no branches")
        self.push(updates)

    def fetch_pr(self, source_pr):
        number = int(source_pr["number"])
        local_ref = f"refs/mirror-source/pr/{number}"
        for attempt in range(2):
            if attempt:
                source_pr = self.gitee.request("GET", f"/repos/{SOURCE}/pulls/{number}")
                if source_pr["state"] != "open":
                    raise SyncError(f"Gitee !{number} changed state; will retry next run")
            self.git("fetch", "--no-tags", GITEE_GIT, f"+refs/pull/{number}/head:{local_ref}")
            actual = self.git("rev-parse", f"{local_ref}^{{commit}}").stdout.strip()
            expected = source_pr["head"].get("sha", "")
            if re.fullmatch(r"[0-9a-f]{40}", expected) and actual == expected:
                return source_pr, actual
        raise SyncError(f"Gitee !{number}: API head SHA and Git head differ; refusing stale code")

    def sync_pr(self, source_pr, existing):
        source_pr, sha = self.fetch_pr(source_pr)
        number = int(source_pr["number"])
        target = source_pr["base"]["ref"]
        head, base = f"gitee/pr/{number}", f"gitee/branches/{target}"
        base_sha = self.refs.get(f"refs/heads/{base}")
        if not base_sha:
            raise SyncError(f"Gitee !{number}: target {target} not synchronized")
        self.push({f"refs/heads/{head}": sha})
        ancestry = self.git("merge-base", "--is-ancestor", sha, base_sha, check=False)
        if ancestry.returncode == 0:
            self.log(f"Gitee !{number}: already included in target; no new reviewable commits")
            return
        if ancestry.returncode != 1:
            raise SyncError(f"Gitee !{number}: cannot determine commit ancestry")
        title = f"[Gitee !{number}] {source_pr['title'].strip()}"[:250]
        body = pr_body(source_pr)
        if existing and existing.get("merged_at"):
            raise SyncError(f"GitHub mirror of Gitee !{number} was merged manually; cannot reuse it")
        if existing:
            patch = {k: v for k, v in {"title": title, "body": body}.items() if existing.get(k) != v}
            if existing["base"]["ref"] != base:
                patch["base"] = base
            if existing["state"] != "open":
                patch["state"] = "open"
            if patch:
                if not self.dry_run:
                    self.github.request("PATCH", f"/repos/{DESTINATION}/pulls/{existing['number']}", patch)
                self.log(f"{'Would update' if self.dry_run else 'Updated'} GitHub #{existing['number']} for Gitee !{number}")
            else:
                self.log(f"Gitee !{number}: GitHub #{existing['number']} is up to date ({sha[:12]})")
        else:
            created = None
            if not self.dry_run:
                created = self.github.request("POST", f"/repos/{DESTINATION}/pulls", {
                    "title": title, "body": body, "head": head, "base": base,
                    "draft": bool(source_pr.get("draft", False)),
                })
            self.log(f"{'Would create' if self.dry_run else 'Created'} GitHub PR for Gitee !{number}"
                     + (f": {created['html_url']}" if created else ""))

    def close_finished(self, number, existing):
        # A missing list entry is not proof of closure: query its authoritative state.
        source_pr = self.gitee.request("GET", f"/repos/{SOURCE}/pulls/{number}")
        if source_pr["state"] == "open":
            self.log(f"Gitee !{number}: still open; leaving mirror open")
            return
        if source_pr["state"] not in ("closed", "merged"):
            raise SyncError(f"Gitee !{number}: unknown state {source_pr['state']}")
        if not self.dry_run:
            self.github.request("PATCH", f"/repos/{DESTINATION}/pulls/{existing['number']}",
                                {"state": "closed", "body": pr_body(source_pr)})
        self.log(f"{'Would close' if self.dry_run else 'Closed'} GitHub #{existing['number']}: "
                 f"Gitee !{number} is {source_pr['state']}")

    def run(self):
        source_prs = self.gitee.pages(f"/repos/{SOURCE}/pulls?state=open")
        destination_prs = self.github.pages(f"/repos/{DESTINATION}/pulls?state=all")
        managed = {}
        for pr in destination_prs:
            number = managed_number(pr)
            if number is not None:
                if number in managed:
                    raise SyncError(f"Duplicate mirrors for Gitee !{number}; refusing ambiguous updates")
                managed[number] = pr
        self.log(f"Source: {SOURCE}; destination: {DESTINATION}; open Gitee PRs: {len(source_prs)}")
        self.initialize()
        self.sync_source()
        open_numbers = {int(pr["number"]) for pr in source_prs}
        for pr in source_prs:
            number = int(pr["number"])
            try:
                self.sync_pr(pr, managed.get(number))
            except (SyncError, subprocess.TimeoutExpired) as exc:
                self.errors.append(f"Gitee !{number}: {exc}")
                self.log(f"ERROR: {self.errors[-1]}")
        for number, pr in managed.items():
            if number not in open_numbers and pr["state"] == "open":
                try:
                    self.close_finished(number, pr)
                except SyncError as exc:
                    self.errors.append(str(exc))
                    self.log(f"ERROR: {exc}")
        if self.errors:
            raise SyncError(f"{len(self.errors)} PR(s) could not be synchronized; see errors above")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-dir", required=True, help="Dedicated Git object store or existing checkout")
    parser.add_argument("--dry-run", action="store_true", help="Fetch and validate without GitHub writes")
    args = parser.parse_args()
    token = os.environ.get("GITHUB_TOKEN")
    if not token and not args.dry_run:
        parser.error("GITHUB_TOKEN is required for writes")
    mirror = Mirror(args.repo_dir, token, args.dry_run)
    failed = False
    try:
        mirror.run()
    except (SyncError, subprocess.TimeoutExpired) as exc:
        mirror.log(f"FAILED: {exc}")
        failed = True
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as stream:
            stream.write("## Gitee mirror synchronization\n\n```\n")
            stream.write("\n".join(mirror.messages))
            stream.write("\n```\n")
    return int(failed)


if __name__ == "__main__":
    sys.exit(main())
