# Metasiberia Plugin API v1.2

This is the language-neutral host contract implemented by the Qt desktop client. A Luau adapter is available today. The host protocol is deliberately versioned independently of any future WASM or trusted-native adapter.

## Current support

- A `.mspkg` is validated and installed as inert files. It does not run until the user explicitly enables it and trusts the installed-content SHA-256.
- The Qt client supervises each Luau plugin in a child process started from the same client executable. JSON Lines IPC uses bounded messages; the parent owns Qt UI, icons, menu registrations, settings and package installation.
- Child-process isolation prevents a Luau VM fault or forced worker termination from directly taking down the Qt client. It is **not** an OS security sandbox: the worker has the same user account and OS permissions. Do not install code you do not trust.
- API v1.2 supports one primary menu action per plugin: a compatible legacy command (`ui.command`) or a host-rendered panel (`ui.panel`), bounded notifications (`ui.notification`), and a read-only current-scene snapshot (`scene.read.summary`). It provides no filesystem, network, credential, object/world mutation, avatar control, server, arbitrary Qt widget or 3D-object APIs.
- Worker messages carry protocol major/minor fields and use the language-neutral JSON Lines contract in `schemas/worker-protocol-v1.schema.json`. Host methods, DTOs and capabilities are listed in `schemas/plugin-api-v1.2.json`.
- Installing a newer package version for the same plugin ID stages and validates the new files, then atomically replaces the installed version with rollback on activation failure. Older-version downgrade and a changed package hash without a version increase are refused. A new package hash requires a fresh trust confirmation. Only one installed version per ID remains after a successful update.
- Runtime states are separate: `installed`, `enabled` (user preference), and `running` (worker has started and registered its primary action). Startup/code errors disable the preference and stop the worker. Disable removes host registrations and closes plugin panels immediately, then requests asynchronous shutdown; the supervisor force-kills a worker that exceeds its shutdown deadline.

## Manifest and runtime contract

Minimum package layout:

```text
plugin.json
main.luau
assets/                 # optional package-local assets
```

Example manifest:

```json
{
  "schemaVersion": 1,
  "id": "org.example.hello",
  "name": "Hello Metasiberia",
  "author": "Example author",
  "version": "1.0.0",
  "description": "Opens a host-rendered greeting panel.",
  "runtime": "luau",
  "entry": "main.luau",
  "icon": "assets/hello.svg",
  "api": { "major": 1, "minor": 2 },
  "permissions": ["ui.panel", "ui.notification", "scene.read.summary"],
  "categories": ["example"]
}
```

The manifest is package metadata, not proof of publisher identity. The installed-content hash is a local integrity/trust decision, not a digital signature.

## Luau binding

```lua
local layout = [[{"type":"column","children":[
  {"type":"text","text":"Привет, Метасибирь!","style":"heading"},
  {"type":"button","id":"say_hello","label":"Сказать привет"}
]}]]

ui.registerPanel("main", "Hello Metasiberia", layout, function(action_id)
    if action_id == "say_hello" then
        ui.showNotification("Hello Metasiberia", "Привет, Метасибирь!")
    end
end)
```

`ui.registerPanel(viewId, title, layoutJson, onAction)` registers the plugin's one primary menu action and its declarative panel. The host validates and renders a vertical `column` with bounded `text`, nested `column`, and `button` nodes. Text styles are `body`, `heading`, and `muted`. Button clicks send a bounded ID asynchronously through worker IPC; the Luau callback receives that ID in the worker. The view title comes from the plugin; its icon comes from the installed manifest. Limits: 64 nodes, depth 4, 4 KiB JSON, 1,000 characters per text and 80 characters per button label.

`ui.closeView()` asks the host to close the plugin's current panel. Closing the Qt dock from its title bar hides it without stopping the plugin, so its menu action can reopen it. Disabling unregisters the action and closes the panel. Luau never receives `QWidget*`, `QDockWidget*`, `MainWindow*`, or another C++ object. API v1.1 implements only a right-side panel; separate windows and 3D tools remain future extension points, not available capabilities.

Legacy command packages remain supported. `ui.registerCommand(id, title, callback)` can be registered once during startup instead of a panel, not in addition to one. The callback runs in the plugin worker. IDs use lowercase letters, digits, dots, underscores or hyphens and start with a letter; command IDs are limited to 64 characters and titles to 80.

`ui.showNotification(title, body)` can be called from a command or panel-action callback. The host checks the package's `ui.notification` permission and bounds title to 80 characters, body to 500 characters and notifications to three per invocation. Notifications are delivered asynchronously on the Qt UI thread. Early v1 aliases `metasiberia.registerCommand` and `metasiberia.notify` remain supported.

### Read-only scene summary (API v1.2)

Packages may declare `scene.read.summary` and call `scene.getSummary()` from startup or a callback. The returned plain table contains `connected`, `server.host`, `server.world`, and `camera.x/y/z`. The Qt host collects it on the UI thread and returns it over supervised worker IPC; each request has a 500 ms worker-side deadline, a 4 KiB DTO limit, and a limit of 120 requests per plugin run. The host checks the declared capability and plugin identity for every request.

This is a read-only snapshot of the local session. It returns no object identifiers or objects and cannot alter the camera, avatar, scene, network state or server. Coordinates are the current camera position in world coordinates. The worker receives copied scalar/string data, never engine pointers. The SDK `scene-info` example demonstrates the production worker call. This process boundary is not an OS sandbox.

## Lifecycle and update behavior

```text
discovered → validated → installed/disabled → explicit trust → starting
           → running → stopping → installed/disabled
                       ↘ error/crash/timeout → disabled with diagnostic
```

Installation never grants permission or executes source. Enabling an untrusted or changed package asks the user to review the exact hash and requested capabilities. The worker verifies the installed package identity again before reading the entrypoint. A worker error disables the saved preference. A successful update replaces all older versions of the same ID; a lower version is rejected. If the new package cannot be activated, the prior files are restored.

The client remembers the user-enabled preference for packages whose hash still matches and may restore them on the next launch. To recover a known-bad package, open Plugins, disable it (which clears the next-start preference), or remove it. Worker crashes are isolated to the worker process and shown in the Installed list.

## Future adapter contract (not implemented)

The stable boundary is a manifest plus bounded versioned DTOs and host services, not Metasiberia C++ classes. Future adapters may map the same capability and lifecycle contract to WASM/WASI or a separately packaged trusted C ABI module. They are not enabled by this API document.

| Capability | Status | Future shape |
| --- | --- | --- |
| `ui.command` | Implemented | Namespaced menu command with async dispatch and owner-scoped cleanup |
| `ui.notification` | Implemented | Bounded notification payload through the host event loop |
| `ui.panel` | Implemented in API v1.1 | One manifest-icon menu action; host-rendered panel, bounded button actions over worker IPC, close/reopen and owner-scoped cleanup |
| `scene.read.summary` | Implemented in API v1.2 | Read-only server/world and camera-position snapshot, copied into a bounded DTO |
| `world.read.selection` | Not implemented | Requires selection snapshot/permission design and stable opaque IDs |
| `assets.import` | Not implemented | Requires file-grant lifecycle and importer output validation through existing asset path |
| `network.https` | Not implemented | Requires HTTPS host allowlist, cancellation/quotas, and OS credential-store integration |
| `world.edit` / `scene.local` | Not implemented | Requires host-mediated local scene owner lifecycle or server-authoritative edit transactions and permission checks |
| `mcp.tools` | Not implemented | Requires capability-filtered tool registry, schemas, consent and cleanup |

## Platform and ABI policy

Luau package source and `.mspkg` are platform-neutral. The current runtime is only supported by the Qt Windows desktop build; Linux client support requires its own build/runtime validation. Future WASM may provide a portable adapter. Native Windows DLL/Linux SO support would use a separately versioned C ABI, opaque handles and matching platform builds, and would remain trusted in-process code or require its own process boundary. No arbitrary native binary is loaded today.

## SDK and examples

See [the SDK guide](sdk/README.md), the [Hello example](examples/hello-metasiberia), and the [Scene Information example](sdk/examples/scene-info). The SDK tools validate and package without third-party Python dependencies.

The package limits and ZIP profile are in [PLUGIN_PACKAGE_FORMAT.md](PLUGIN_PACKAGE_FORMAT.md).
