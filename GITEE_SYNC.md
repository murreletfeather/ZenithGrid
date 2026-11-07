# Gitee → GitHub synchronization

Gitee is the source of truth: <https://gitee.com/ZenithGrid/ZenithGrid>.
GitHub hosts original code and review copies of every open Gitee PR.

| GitHub ref | Purpose |
| --- | --- |
| `develop` | Trusted synchronization controller; never overwritten by the mirror |
| `gitee/branches/develop` | Exact Gitee develop commit; use this branch to browse current source |
| `gitee/branches/<branch>` | Other original Gitee branches |
| `gitee/pr/<number>` | Original Gitee PR head, including PRs from forks |
| tag `gitee/<tag>` | Original Gitee tag |

## Operation and cost

- Runs every four hours (UTC 00:43, 04:43, 08:43, 12:43, 16:43, 20:43), or manually from Actions. Scheduled runs may be delayed by GitHub.
- Uses the public Gitee API and the job's short-lived `GITHUB_TOKEN`; no Gitee token, model API key, PAT, server or paid runner is required for ordinary synchronization.
- Standard GitHub-hosted runners are free for public repositories. Recheck billing if the repository becomes private or the runner policy changes.
- Enable **Settings → Actions → General → Workflow permissions → Allow GitHub Actions to create and approve pull requests**. The script never approves or merges a PR.
- Enable `Sync ZenithGrid from Gitee` if the workflow was previously disabled.

## Native Codex review

Connect this GitHub repository in Codex's code review settings. Open a mirrored GitHub PR and manually comment `@codex review`. Repository-triggered GitHub reviews use Code Review usage according to OpenAI's current pricing documentation. The synchronization job never invokes a model or posts that comment automatically.

Do not merge these copies on GitHub. Merge and close PRs on Gitee. The next synchronization updates their code or closes the GitHub copy when Gitee reports it closed/merged. GitHub may display **Closed** rather than **Merged**, because the synchronizer does not manufacture a GitHub merge.

Discussions, comments, review decisions, CI checks, assignees and labels are not copied. Initial draft status is copied; later draft/ready transitions are not synchronized. Existing GitHub reviews remain on the same PR. Removed source branches/tags and closed-PR branches are retained; the job never deletes Git refs.

## Consistency and permissions

Only changed Git refs and PR metadata are written. Gitee force-pushes update only the managed namespace, using an explicit lease. API PR head SHAs are checked against the fetched original commits. Failed pagination cannot close missing PRs, and closure requires a separate authoritative Gitee detail request.

PRs target `gitee/branches/<target>`, so GitHub-only controller files do not pollute their diffs. The job never runs source PR code. Its Git pushes use the job token without a PAT fallback. GitHub may require approval for associated Actions runs; Codex is a separate GitHub App integration.

If GitHub refuses a source commit that changes workflows because of token restrictions, the run fails visibly. Do not work around this by giving arbitrary source PR code access to a persistent write token.

## Validation

Only Python's standard library and Git are needed:

```powershell
python .github/scripts/sync_gitee.py --repo-dir ./mirror-check.git --dry-run
python -m unittest discover -s .github/scripts/tests -v
```

The dry run downloads Git objects and validates source PR head SHAs without writing to GitHub.

References: [Actions billing](https://docs.github.com/en/actions/concepts/billing-and-usage), [job token behavior](https://docs.github.com/en/actions/concepts/security/github_token), [Codex GitHub review](https://learn.chatgpt.com/docs/third-party/github), [Code Review usage](https://learn.chatgpt.com/docs/pricing).
