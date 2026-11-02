# Canonical release workflow

KLookup has a single CI/CD workflow at [`.github/workflows/ci.yml`](https://github.com/arran4/klookup/blob/main/.github/workflows/ci.yml). It validates PRs, `main`, and monthly scheduled checks, and publishes developer documentation from `main`.

**Release readiness:** [Issue #73](https://github.com/arran4/klookup/issues/73) tracks outstanding public-release checks, including maintainer sign-off, secrets/history and asset provenance audits, production script credentials, and live Plasma verification. Do not initiate a public release until these checks are approved. Merely enabling a release mode is not release approval.

## Manual actions

Open **Actions → CI/CD → Run workflow**, select branch `main`, and choose a mode:

| Mode | Effect |
| --- | --- |
| `build` | Validate sources, docs, licensing, normal tests and ASan/UBSan; no tag or software release |
| `monthly-maintenance` | Run the same verification manually (also performed on the monthly schedule) |
| `release-patch` | Prepare and publish the next patch version |
| `release-minor` | Prepare and publish the next minor version |
| `release-major` | Prepare and publish the next major version |
| `release-test` | Prepare the next test prerelease |
| `release-rc` | Prepare the next release candidate |
| `release-alpha` | Prepare the next alpha prerelease |
| `publish-tag` | Revalidate and publish **an existing `v*` tag**, not a new version |

Release modes use [`arran4/git-tag-inc`](https://github.com/arran4/git-tag-inc) for version arithmetic. The optional `release_version_override` applies to `release-*` modes only. It selects an explicit `vMAJOR.MINOR.PATCH` (optionally with a prerelease suffix) rather than calculating a version. The workflow never moves a pre-existing tag. To recover a failed publication, dispatch **`publish-tag` from the intended tag**, rather than rerunning auto-increment and potentially selecting a different version.

## Release gates and publication

1. The workflow runs normal and sanitizer CTest, REUSE, Actionlint and documentation validation.
2. The normal CMake build is installed into a staging prefix and archived as `klookup-linux-amd64.tar.gz` with a checksum file. This is a **Linux installation-layout archive requiring installed Qt6/KF6 dependencies**, not an AppImage or standalone portable binary.
3. A separate job downloads those exact artifacts, verifies their checksum, and inspects the staged executables, provider assets and ELF format.
4. A release-quality gate requires the complete validation aggregate and artifact-smoke job to succeed.
5. For `release-*` modes, a serialized job verifies the validated commit is still `origin/main`, calculates the tag, checks for an existing exact-SHA tag, rechecks `main` immediately before pushing a new immutable tag, then dispatches the same workflow against that tag.
6. On tag-context validation, a single publisher verifies the tag and ancestry, then creates the GitHub Release or resumes an existing exact-target release by uploading the validated artifacts. Prerelease tags are marked as prereleases.

GitHub token access defaults to read-only. Only tag preparation has `contents:write` / `actions:write`, and the publisher has `contents:write`. Release actions must be permitted by repository GitHub Actions settings. Publication does not use a second release action, a release-event listener, or an independent rebuild.

## Recovery and limitations

- A failure **before tagging** does not create a release. Review the failing test or build instead of manually tagging the old commit.
- A failure **after the tag exists** is recovered from that exact tag with `publish-tag`.
- An existing release that targets a different commit is rejected; the workflow never silently moves or replaces release identity.
- External `v*` tag pushes enter the same validation/publisher path, provided the tagged commit is on `main`.
- The packaged archives are not an endorsement of installation on arbitrary Linux distributions. Release verification still requires a live supported KDE Plasma installation.

The workflow supports only **Linux amd64** release candidates. Other operating systems and packaging formats need implementation and testing before being added.
