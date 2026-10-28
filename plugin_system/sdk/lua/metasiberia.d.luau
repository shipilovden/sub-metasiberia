-- Luau editor hints for the currently implemented Plugin API.
-- This file is SDK metadata; the host API is still enforced at runtime.

export type SceneSummary = {
	connected: boolean,
	server: { host: string, world: string },
	camera: { x: number, y: number, z: number },
}

declare ui: {
	registerCommand: (commandId: string, title: string, callback: () -> ()) -> boolean,
	registerPanel: (viewId: string, title: string, layoutJson: string, onAction: (actionId: string) -> ()) -> boolean,
	closeView: () -> (),
	showNotification: (title: string, body: string) -> (),
}

declare scene: {
	getSummary: () -> SceneSummary,
}
