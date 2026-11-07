"""Offline behavioral tests for the Gitee mirror's write boundaries."""

import importlib.util
from pathlib import Path
import subprocess
import unittest
from unittest.mock import Mock


SCRIPT = Path(__file__).resolve().parents[1] / "sync_gitee.py"
SPEC = importlib.util.spec_from_file_location("sync_gitee", SCRIPT)
sync = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(sync)

A, B, C = "a" * 40, "b" * 40, "c" * 40


def source_pr(**changes):
    result = {
        "number": 153, "state": "open", "title": "Fix mesh refresh",
        "body": "A source description", "user": {"login": "contributor"},
        "head": {"sha": A}, "base": {"ref": "develop"},
    }
    result.update(changes)
    return result


def destination_pr(source=None, **changes):
    source = source or source_pr()
    result = {
        "number": 8, "state": "open", "merged_at": None,
        "title": "[Gitee !153] Fix mesh refresh", "body": sync.pr_body(source),
        "head": {"ref": "gitee/pr/153", "repo": {"full_name": sync.DESTINATION}},
        "base": {"ref": "gitee/branches/develop", "repo": {"full_name": sync.DESTINATION}},
    }
    result.update(changes)
    return result


def mirror():
    instance = sync.Mirror(Path(__file__).parent / "unused-object-store", "unit-test-token")
    instance.gitee = Mock()
    instance.github = Mock()
    instance.git = Mock(return_value=subprocess.CompletedProcess([], 0, "", ""))
    instance.log = Mock()
    return instance


class PaginationTests(unittest.TestCase):
    def test_later_page_failure_never_returns_a_partial_result(self):
        api = sync.Api("https://example.invalid")
        api.request = Mock(side_effect=[[{"number": n} for n in range(100)], sync.SyncError("page 2 unavailable")])
        with self.assertRaisesRegex(sync.SyncError, "page 2"):
            api.pages("/pulls?state=open")
        self.assertEqual(api.request.call_args.args, ("GET", "/pulls?state=open&per_page=100&page=2"))

    def test_non_array_page_is_a_failure_not_an_empty_page(self):
        api = sync.Api("https://example.invalid")
        api.request = Mock(return_value={"message": "rate limit"})
        with self.assertRaisesRegex(sync.SyncError, "refusing a partial sync"):
            api.pages("/pulls")

    def test_failed_source_or_destination_listing_cannot_close_any_mirror(self):
        for failed_side in ("gitee", "github"):
            with self.subTest(failed_side=failed_side):
                instance = mirror()
                instance.gitee.pages.return_value = []
                instance.github.pages.return_value = [destination_pr()]
                getattr(instance, failed_side).pages.side_effect = sync.SyncError("pagination failed")
                instance.initialize = Mock()
                instance.sync_source = Mock()
                instance.close_finished = Mock()
                with self.assertRaisesRegex(sync.SyncError, "pagination failed"):
                    instance.run()
                instance.initialize.assert_not_called()
                instance.sync_source.assert_not_called()
                instance.close_finished.assert_not_called()
                instance.github.request.assert_not_called()


class OwnershipTests(unittest.TestCase):
    def test_valid_marker_and_both_repository_identities_are_required(self):
        self.assertEqual(sync.managed_number(destination_pr()), 153)
        mutations = [
            lambda pr: pr.update(body="User text\n" + pr["body"]),
            lambda pr: pr.update(body=pr["body"].replace("#153", "#154", 1)),
            lambda pr: pr["head"].update(ref="feature/user-owned"),
            lambda pr: pr["head"]["repo"].update(full_name="someone/ZenithGrid"),
            lambda pr: pr["base"]["repo"].update(full_name="someone/ZenithGrid"),
            lambda pr: pr["base"].update(ref="develop"),
            lambda pr: pr.update(head={"ref": "gitee/pr/153", "repo": None}),
        ]
        for mutate in mutations:
            candidate = destination_pr()
            mutate(candidate)
            with self.subTest(candidate=candidate):
                self.assertIsNone(sync.managed_number(candidate))

    def test_repository_name_comparison_is_case_insensitive(self):
        candidate = destination_pr()
        candidate["head"]["repo"]["full_name"] = sync.DESTINATION.upper()
        self.assertEqual(sync.managed_number(candidate), 153)

    def test_duplicate_managed_prs_stop_before_any_git_write(self):
        instance = mirror()
        instance.gitee.pages.return_value = [source_pr()]
        instance.github.pages.return_value = [destination_pr(), destination_pr(number=9)]
        instance.initialize = Mock()
        with self.assertRaisesRegex(sync.SyncError, "Duplicate mirrors"):
            instance.run()
        instance.initialize.assert_not_called()
        instance.github.request.assert_not_called()


class RefWriteTests(unittest.TestCase):
    def test_atomic_push_uses_exact_old_sha_leases_and_omits_unchanged_refs(self):
        instance = mirror()
        base = "refs/heads/gitee/branches/develop"
        head = "refs/heads/gitee/pr/153"
        tag = "refs/tags/gitee/v1"
        instance.refs = {base: A, tag: C}
        instance.push({base: B, head: A, tag: C})
        args = instance.git.call_args.args
        self.assertEqual(args[:2], ("push", "--atomic"))
        self.assertIn(f"--force-with-lease={base}:{A}", args)
        self.assertIn(f"--force-with-lease={head}:", args)
        self.assertIn(f"{B}:{base}", args)
        self.assertIn(f"{A}:{head}", args)
        self.assertFalse(any(tag in arg for arg in args))
        self.assertNotIn("--force", args)

    def test_unmanaged_ref_rejects_the_entire_mixed_push(self):
        for unmanaged in ("refs/heads/develop", "refs/heads/gh-pages", "refs/tags/v1", "refs/heads/gitee/other"):
            with self.subTest(unmanaged=unmanaged):
                instance = mirror()
                with self.assertRaisesRegex(sync.SyncError, "unmanaged ref"):
                    instance.push({"refs/heads/gitee/pr/153": A, unmanaged: B})
                instance.git.assert_not_called()
                self.assertEqual(instance.refs, {})

    def test_failed_lease_does_not_advance_the_remembered_remote_sha(self):
        instance = mirror()
        ref = "refs/heads/gitee/pr/153"
        instance.refs[ref] = A
        instance.git.side_effect = sync.SyncError("stale info")
        with self.assertRaisesRegex(sync.SyncError, "stale info"):
            instance.push({ref: B})
        self.assertEqual(instance.refs[ref], A)


class HeadRaceTests(unittest.TestCase):
    def test_api_is_reread_and_new_head_metadata_used_after_sha_race(self):
        instance = mirror()
        fresh = source_pr(head={"sha": B}, title="Updated after list")
        instance.gitee.request.return_value = fresh
        fetched = iter((B, B))

        def git_result(*args, **kwargs):
            stdout = next(fetched) + "\n" if args[0] == "rev-parse" else ""
            return subprocess.CompletedProcess(args, 0, stdout, "")

        instance.git.side_effect = git_result
        result, sha = instance.fetch_pr(source_pr())
        self.assertIs(result, fresh)
        self.assertEqual(sha, B)
        instance.gitee.request.assert_called_once_with("GET", f"/repos/{sync.SOURCE}/pulls/153")
        self.assertEqual(sum(call.args[0] == "fetch" for call in instance.git.call_args_list), 2)

    def test_persistent_head_race_fails_before_pushing_a_pr_ref(self):
        instance = mirror()
        instance.gitee.request.return_value = source_pr(head={"sha": C})
        instance.git.side_effect = lambda *args, **kwargs: subprocess.CompletedProcess(args, 0, B + "\n", "")
        instance.push = Mock()
        with self.assertRaisesRegex(sync.SyncError, "refusing stale code"):
            instance.sync_pr(source_pr(), None)
        instance.push.assert_not_called()
        instance.github.request.assert_not_called()

    def test_pr_closing_during_retry_cannot_be_published_as_open(self):
        instance = mirror()
        instance.gitee.request.return_value = source_pr(state="closed")
        instance.git.return_value = subprocess.CompletedProcess([], 0, B + "\n", "")
        with self.assertRaisesRegex(sync.SyncError, "changed state"):
            instance.fetch_pr(source_pr())
        self.assertEqual(sum(call.args[0] == "fetch" for call in instance.git.call_args_list), 1)
        instance.github.request.assert_not_called()


class PullRequestLifecycleTests(unittest.TestCase):
    def test_closed_and_merged_sources_only_close_github_never_merge(self):
        for source_state in ("closed", "merged"):
            with self.subTest(source_state=source_state):
                instance = mirror()
                instance.gitee.request.return_value = source_pr(state=source_state)
                instance.close_finished(153, destination_pr())
                method, path, data = instance.github.request.call_args.args
                self.assertEqual((method, path), ("PATCH", f"/repos/{sync.DESTINATION}/pulls/8"))
                self.assertEqual(data["state"], "closed")
                self.assertIn(f"**{source_state}**", data["body"])
                self.assertEqual(instance.github.request.call_count, 1)
                instance.git.assert_not_called()

    def test_missing_open_list_entry_requires_authoritative_state_check(self):
        instance = mirror()
        instance.gitee.pages.return_value = []
        instance.github.pages.return_value = [destination_pr()]
        instance.gitee.request.return_value = source_pr(state="open")
        instance.initialize = Mock()
        instance.sync_source = Mock()
        instance.run()
        instance.gitee.request.assert_called_once_with("GET", f"/repos/{sync.SOURCE}/pulls/153")
        instance.github.request.assert_not_called()

    def test_failed_authoritative_state_check_keeps_mirror_open(self):
        instance = mirror()
        instance.gitee.pages.return_value = []
        instance.github.pages.return_value = [destination_pr()]
        instance.gitee.request.side_effect = sync.SyncError("Gitee unavailable")
        instance.initialize = Mock()
        instance.sync_source = Mock()
        with self.assertRaisesRegex(sync.SyncError, "could not be synchronized"):
            instance.run()
        instance.github.request.assert_not_called()

    def test_unchanged_existing_mirror_is_neither_recreated_nor_patched(self):
        instance = mirror()
        original = source_pr()
        instance.gitee.pages.return_value = [original]
        instance.github.pages.return_value = [destination_pr(original)]
        instance.initialize = Mock()
        instance.sync_source = Mock()
        instance.fetch_pr = Mock(return_value=(original, A))
        instance.refs = {"refs/heads/gitee/branches/develop": B, "refs/heads/gitee/pr/153": A}
        instance.git.return_value = subprocess.CompletedProcess([], 1, "", "")
        instance.run()
        instance.github.request.assert_not_called()
        instance.git.assert_called_once_with("merge-base", "--is-ancestor", A, B, check=False)

    def test_reopened_source_reuses_existing_github_pr(self):
        instance = mirror()
        original = source_pr()
        instance.fetch_pr = Mock(return_value=(original, A))
        instance.refs = {"refs/heads/gitee/branches/develop": B, "refs/heads/gitee/pr/153": A}
        instance.git.return_value = subprocess.CompletedProcess([], 1, "", "")
        instance.sync_pr(original, destination_pr(original, state="closed"))
        instance.github.request.assert_called_once_with(
            "PATCH", f"/repos/{sync.DESTINATION}/pulls/8", {"state": "open"},
        )


if __name__ == "__main__":
    unittest.main()
