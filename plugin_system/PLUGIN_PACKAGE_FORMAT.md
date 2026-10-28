# Metasiberia plugin package format v1

`.mspkg` is a ZIP package containing `plugin.json`, one entry file and optional assets. Installation validates and stores files; it never executes code. Execution is a separate, explicit local-user action.

## Container

- Extension: `.mspkg`; single-disk ZIP, no ZIP64 or encryption.
- Compression: Store or Deflate.
- Root file: UTF-8 `plugin.json`, at most 64 KiB.
- Limits: 64 MiB archive and expanded size, 512 entries, 16 MiB per file, 200:1 expansion ratio.
- Paths must be relative, forward-slash separated, Unicode NFC and free of traversal, drive/UNC prefixes, reserved Windows names, symlinks and special/reparse entries. Duplicate paths are rejected case-insensitively after Unicode normalization.
- `.metasiberia-install.json` is reserved for the host's installed-file integrity metadata.

## Manifest

```json
{
  "schemaVersion": 1,
  "id": "org.example.hello",
  "name": "Hello",
  "author": "Example author",
  "version": "1.0.0",
  "runtime": "luau",
  "entry": "main.luau",
  "api": { "major": 1, "minor": 0 },
  "permissions": ["ui.command", "ui.notification"]
}
```

IDs use lowercase reverse-domain notation and versions use semantic-version form. The current client API is v1.2. A declared capability is an untrusted request, not a grant. The runtime recognizes `ui.command`, `ui.notification`, `ui.panel` and `scene.read.summary`; unsupported capabilities, runtimes and incompatible API versions cannot run. Optional fields include `description` (up to 2,000 characters), up to 16 bounded categories and an optional package-relative PNG/JPEG icon (maximum 512×512) or a safe SVG icon (maximum 64 KiB and 512×512). SVG is limited to basic vector geometry; scripts, event attributes, stylesheets, DTDs, external references and unsupported elements/attributes are rejected. Non-empty dependencies are rejected until dependency resolution exists.

## Current local Luau runtime

Only a package installed from a local `.mspkg` can run. Catalog-sourced packages remain inert. The user must explicitly trust the exact installed contents when enabling a package; later launches restore only a previously enabled package whose installed-content SHA-256 still matches that approval. A changed package requires a new trust confirmation.

The Qt client supervises a child worker process per plugin. The worker hosts a separate Luau VM and exchanges bounded, versioned JSON Lines messages with the UI process (protocol major 1). API v1.2 provides one primary registration: compatible legacy `ui.registerCommand(id, title, callback)` or `ui.registerPanel(viewId, title, layoutJson, onAction)`, bounded `ui.showNotification(title, body)`, and the read-only `scene.getSummary()` method when `scene.read.summary` was declared. Early v1 packages using `metasiberia.registerCommand` and `metasiberia.notify` remain supported. Qt registrations, manifest icons, host-rendered panels and notifications are owned by the UI process; Luau callbacks never execute on the Qt UI thread. Scene snapshots are copied scalar/string DTOs retrieved by a bounded host request; no engine pointers cross the process boundary. Panel descriptions are bounded JSON DTOs containing only vertical containers, text, and buttons; button action IDs return to the worker through asynchronous IPC. The VM has a 16 MiB memory cap, 256 KiB entrypoint cap, execution limits of 10,000 Luau safepoints and 750 ms per call, one primary action per plugin, a bounded action queue and at most three bounded notifications per call. Disabling removes host registrations and closes panels immediately, then sends an asynchronous stop request and force-terminates a worker that misses the shutdown deadline. Startup and action errors stop the plugin and appear in Plugin Manager without a blocking error dialog.

**A child process is a crash boundary, not an operating-system sandbox.** It runs with the same account permissions and is not protection against malicious code or operating-system access. Only enable local code whose publisher and contents you have reviewed. The explicit trust prompt displays the unverified manifest author, archive SHA-256, verified installed-content SHA-256 and requested capabilities. This prototype is not intended to run unknown public packages.

## Installation and removal

The client validates archive structure, CRCs, manifest, compatibility, sizes and paths, then writes verified files to a same-volume staging directory under the user's application-data `plugins` path and atomically activates the package. Installation alone never starts the entrypoint. The installed list keeps the package disabled until the user enables it. Installing a newer version for the same ID stops the old worker, stages the new version, then replaces the old version atomically; if activation fails, the prior package directory is restored. Downgrades are refused and parallel installed versions for one ID are removed on a successful update. Changed contents require fresh explicit trust. Removing a running package first disables it and unregisters its command; the user separately chooses whether to retain plugin settings.

Remote store publication, automatic downloads, dependencies, separate windows, world access, native modules and WASM are not implemented in this MVP. Host-rendered right-side panels and local `.mspkg` updates from reviewed files are implemented.

## Hello example

`examples/hello-metasiberia` is the source for the current Hello package (version 1.0.4). Its manifest icon is shipped inside the package, not the client. Installation leaves the package disabled and absent from the Plugins menu. After the user explicitly trusts and enables it, the menu contains exactly one **Hello Metasiberia** action with that icon. Choosing it opens the host-rendered right-side panel declared by `main.luau`; its heading is **Привет, Метасибирь!** and its button sends an action back to the Luau worker, which displays the greeting through the Qt host notification callback. Closing the panel keeps the plugin running so the menu can reopen it. Disabling removes the menu action and closes the panel while retaining the installed package. The package files remain outside the client executable.
