# Gesture Settings Persistence

## What Is Stored
- The per-user gesture list (including custom `.subanim` URLs), plus per-gesture flags (animate head / loop) and duration.
- The actual animation resources are stored via `ResourceManager` in the client cache resources directory (see `GUIClient::postConnectInitialise()`).

## When It Applies
- Gesture editing and gesture playback is allowed only for logged-in users.
- On login:
  - If local gesture settings exist, the client applies them (local settings win).
  - Otherwise, if the server provides non-empty `gesture_settings`, the client applies them and saves a local copy.
  - Otherwise the client uses default gesture settings.

Note: gesture settings are persisted locally only.  The client does not upload gesture settings to the server.

## Local Storage Location
The settings are stored per `(server_hostname, user_id)` under the application data directory:

`<appdata_path>/gesture_settings/<host>_user_<user_id>.bin`

On Windows, `appdata_path` is typically something like:

`%APPDATA%\\Cyberspace`
