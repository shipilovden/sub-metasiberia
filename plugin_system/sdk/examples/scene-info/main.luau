ui.registerCommand("show_scene_summary", "Scene Information", function()
	local snapshot = scene.getSummary()
	local location = snapshot.connected and "Connected" or "Not connected"
	local message = location .. "\nServer: " .. snapshot.server.host .. "\nWorld: " .. snapshot.server.world ..
		string.format("\nCamera: %.2f, %.2f, %.2f", snapshot.camera.x, snapshot.camera.y, snapshot.camera.z)
	ui.showNotification("Scene Information", message)
end)
