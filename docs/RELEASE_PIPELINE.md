# Metasiberia Release Pipeline

## Goal

For each release tag `vX.Y.Z`, we keep the Git history and release metadata in sync for the Windows Qt client.

The release artifact naming convention remains:

1. `MetasiberiaBeta-Setup-vX.Y.Z.exe`

## Current Manual Policy

The canonical release instruction is now:

1. Bump the client version in `shared/Version.h`.
2. Bump the Metasiberia version in `shared/MetasiberiaVersion.h`.
3. Add `docs/RELEASE_NOTES_vX.Y.Z.md`.
4. Commit the release changes.
5. Create an annotated git tag `vX.Y.Z`.
6. Push the commit and the tag to GitHub.
7. Build and verify the Windows Qt client / installer locally.
8. The Windows installer `.exe` is uploaded manually by the release owner after verification.

## Important Note About The Installer

- The Windows installer `.exe` is not uploaded automatically as part of the git/tag step.
- The release owner uploads the installer manually.
- If Codex prepares the release, Codex should stop after commit/tag/push and should not upload the Windows `.exe` asset.

## Historical Automation

There is a local helper script `scripts/publish_update.ps1`, but its fully automated publish path should be treated as historical reference, not the canonical process for the current manual-installer workflow.

In particular, do not rely on it to upload the final Windows installer asset without an explicit human decision.

## Normal Release Shape

The intended release shape remains simple:

1. Prepare the version bump and release notes.
2. Commit and tag the release in git.
3. Push the branch and release tag.
4. Build the Windows Qt output, for example via `C:\programming\qt_build.ps1`.
5. Build `MetasiberiaBeta-Setup-vX.Y.Z.exe`.
6. Upload the Windows installer manually to GitHub Release.

No Linux tarball is required, and no second mandatory artifact is required.
