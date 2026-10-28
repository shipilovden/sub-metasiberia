# Metasiberia Plugin SDK (prototype)

This source SDK helps developers create and validate `.mspkg` packages for the Metasiberia Qt client. It includes a Luau Hello panel example, a read-only Scene Information example, and standard-library Python tools for packaging and validation. Creating a package does not require building the client.

## Requirements and commands

- Python 3.9 or newer; the SDK tools use only the standard library.
- The current execution runtime is supported by the Qt Windows client. Packaging and validation work cross-platform; Linux runtime support still requires a Linux client build and tests.
- From `plugin_system/sdk/tools`, run:

```powershell
python test_sdk.py
python package_plugin.py ..\..\examples\hello-metasiberia --output .\hello-metasiberia.mspkg
python validate_plugin.py .\hello-metasiberia.mspkg
```

To package the read-only scene example:

```powershell
python package_plugin.py ..\examples\scene-info --output .\scene-info.mspkg
python validate_plugin.py .\scene-info.mspkg
```

## Install and use

In Metasiberia, open **Plugins → + Add → Installation → Install** and choose a local `.mspkg`. Installation validates and stores the package but does not execute it. Review the package source, author, requested capabilities and content hash before explicitly trusting and enabling it.

After startup succeeds, the plugin appears in the top **Plugins** menu. The Hello example opens its host-rendered panel; its button sends an action to the worker and displays a notification. Closing the panel leaves the plugin enabled so its menu action can reopen it. Disabling removes the menu action and closes the panel. Use **Remove selected plugin** to uninstall it.

To update a package, increase its semantic version and install the new package with the same ID. The host validates and replaces the installed version; changed content requires fresh trust. Downgrades are refused.

## API examples

The Hello example registers one host-rendered panel and sends a bounded notification from its button callback:

```lua
local layout = [[{"type":"column","children":[
  {"type":"text","text":"Hello from the plugin","style":"heading"},
  {"type":"button","id":"say_hello","label":"Say hello"}
]}]]

ui.registerPanel("main", "Example plugin", layout, function(action_id)
    if action_id == "say_hello" then
        ui.showNotification("Example", "Hello from the plugin")
    end
end)
```

The Scene Information example uses the read-only `scene.getSummary()` API, which requires the `scene.read.summary` capability. It returns a bounded snapshot of the connection and camera position. It cannot inspect or modify objects or change the world.

API details, limits, lifecycle and trust behavior are described in [PLUGIN_API_V1.md](../PLUGIN_API_V1.md). Package format and validation limits are described in [PLUGIN_PACKAGE_FORMAT.md](../PLUGIN_PACKAGE_FORMAT.md). Machine-readable contracts are in `plugin_system/schemas/` and Luau editor hints are in `lua/metasiberia.d.luau`.

## Security and compatibility

Luau packages run in a supervised child process with a separate VM. The process boundary helps contain runtime crashes and lets the client stop a stuck worker, but it is not an operating-system sandbox: a worker has the same account permissions. Only run package code you have reviewed.

The runtime does not provide arbitrary file or network access, credentials, world editing, avatar control, server internals, arbitrary Qt widgets or a general 3D object API. Catalog packages remain inert because remote catalog downloads are not connected. Native DLL/SO and WASM adapters are not supplied.
