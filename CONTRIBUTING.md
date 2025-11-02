# Contributing Guide

How to set up, code, test, review, and release so contributions meet our Definition of Done.

## Code of Conduct

This project adheres to the Oregon State University [Code of Conduct](https://scs.oregonstate.edu/sites/scs.oregonstate.edu/files/2025-09/code-of-conduct_-effective-9-24-25_0.pdf).
By participating, you are expected to uphold this code.

Please report any unacceptable behavior to the project [maintainers](#support-contact) and/or
[Oregon State University](https://scs.oregonstate.edu/reporting-incidents).

## Getting Started

> List prerequisites, setup steps, environment variables/secrets handling, and how to run the app locally.

### Prerequisites

- Linux (Ubuntu 22.04 LTS recommended)
- git, cmake, build-essential, clang, clang-tidy

### Setup

Clone repo and make sure you are on the main branch:

```bash

git clone https://github.com/nvme-kv-osu-2025/nvme-kv-engine.git
cd nvme-kv-engine

git fetch --all -p
```

## Branching & Workflow

**Workflow:** Trunk-based development with short-lived feature branches and Pull Requests.

**Default branch:** `main` (protected; no direct pushes)

### Branch Types & Naming

- **Feature:** `feat/<scope>-<desc>_<ticket_id>`
- **Fix:** `fix/<scope>-<desc>_<ticket_id>`
- **Chore:** `chore/<desc>_<ticket_id>`
- **Docs:** `docs/<desc>_<ticket_id>`
- **Refactor:** `refactor/<scope>_<ticket_id>`

Create a Branch (from `main`):

```bash
# Sync and ensure a clean trunk
git fetch origin -p
git checkout main
git pull origin main
git checkout -b feat/<desc>_<ticket_id>

# Work & commit
git add -p
git commit -m "feat(<scope>)_<ticket_id>: <message>"

# First push: set upstream
git push -u origin HEAD
```

### Rebase vs. merge

**Rebase:** Prefer rebasing for feature branches to keep history linear and conflicts small.

```bash
git fetch origin
git rebase origin/main
# resolve any merge conflicts -> git add <files -> git rebase --continue
git push --force-with-lease
# --force-with-lease instead of --force to avoid overwriting others' work
```

**Merge into `main`:**

- Squash & merge when commits are noisy or incremental.
- Rebase & merge when commits are already clean, reviewable units.

## Issues & Planning

- Use GitHub Issues for all work. Every PR must reference exactly one issue/ticket.

- Required fields: short title, clear "Definition of Done", acceptance criteria, labels (priority, related REQ-XXX ID), estimated size.

* Triage/assignment
  - The **Deadline manager** ensures issues align with Sprint/Canvas deadlines.
  - The **Meeting Manager** schedules any working sessions needed to resolve blockers.

## Commit Messages

We follow [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/) with the ticket ID appended to the type/scope.

```txt
<type>[optional scope]_<ticket_id>: <description>

[optional body]

[optional footer(s)]
```

- Reference the ticket/issue in the footer when not in the subject (e.g., `Closes #123`).
- Specify commits that introduce a breaking API change with `!` after type/scope and `BREAKING CHANGE: <description>` in footer.
- Keep subject <= 72 chars. Use body for rationale/additional context.

**Examples:**

Commit message with description and referenced issue in footer
```txt
feat(api)_<ticket_id>: add kv_put_async() with completion callbacks

Closes: #123
```

Commit message with body + breaking change description in footer.
```
feat(api)_<ticket_id>!: rename kv_delete() -> kv_del() and change return codes

Standardizes errno mapping across backends.

BREAKING CHANGE: function renamed; old symbol removed. Update callers.
```
  
## Code Style, Linting & Formatting

- **C/C++ style:** clang-tidy + clang-format

- Run locally:

  ```bash
  make lint     # clang-tidy
  make format   # clang-format
  ```

- Reviewers enforce "not over-documented nor under-documented" code; keep comments short but accurate

## Testing

- Test types required:
  - Unit tests
  - Integration tests
  - End-to-end tests

- Running Tests:

```bash
make build
make test
```

- New/changed code **must** include/update tests to cover new functionality or edge cases.
- CI must pass **all** tests before merge.

## Pull Requests & Reviews

**Two reviewers required for every PR:**

1. **Primary Reviewer** (accountable for correctness and breakages post-merge).
2. **Additional Reviewer** (logic clarity + documentation quality).

**PR rules:**

- PR title matches ticket scope; link the ticket
- Keep PRs small and focused. If > ~400 LOC or < ~10 files, split unless strongly justified.
- Individual who submitted PR cannot be reviewer.

**Approval & merge gates:**

- **CI Build gate:** builds clean on latest `main`.
- **CI Test gate:** all unit, integration, and e2e tests pass.
- **Code quality:** lint/format clean; consistent style.
- **Security gate:** reviewers scan for obvious vulnerabilities; any concerns must be resolved before merge.
- **Documentation gate:** code is understandable and appropriately documented.
- **Merge policy:** branch is rebased on `main` and squashed at merge wit ha clear final message including the ticket (e.g., `feat(enging): add async store path (#123)`).

## CI/CD

- CI runs on every PR and on `main`. Required checks:
  - Build
  - Tests
  - Lint/Format
- Re-run failed jobs via the PR Checks tab once the cause is addressed.
- Pipelines live under `.github/workflows/`.

## Security & Secrets

- **No secrets in the repo.** Use environment variables or local config files ignored by git.
- Report security issues privately to maintainers (see [Support & Contact](#support-contact)).
- Keep dependencies minimal; bump versions only with CI green and a short risk note in the PR.

## Documentation Expectations

- Update README, `docs/`, and in-code comments for any user-visible or API-visible change.

- If you add/modify public functions, include the following above the function definition:
  - Brief description of purpose.
  - Parameters with types and meanings.
  - Return values with types and meanings.
- Use inline comments for complex logic sections to explain intent, but keep them brief to keep code readable.

Example:
```C
/**
* Add two numbers to get sum.
* 
* @param a First number to add
* @param b Second number to add
* @return Sum of a and b
*/
int sum(int a, int b)
{
  return a + b;
}

```

## Release Process

- **Versioning:** pre-1.0 [SemVer](https://semver.org/) (0.y.z) during active development.
- **Tagging:** create an annotated tag on `main` for each release (e.g., `v0.1.0`).
- **Changelog:** maintain `CHANGELOG.md` with notable changes for each release.
- **Rollback:** revert the tag and the offending merge commit; open a "hotfix" issue and PR.

## Support & Contact

Maintainers can be reached via GitHub Issues or email and responses can typically be expected within 24 hours on weekdays:

- Owen Krause -- krauseo@oregonstate.edu
- Charles Tang -- tangcha@oregonstate.edu
- Cody Strehlow -- strehloc@oregonstate.edu
