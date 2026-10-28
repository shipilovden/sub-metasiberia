local layout = [[{"type":"column","children":[{"type":"text","text":"Привет, Метасибирь!","style":"heading"},{"type":"button","id":"say_hello","label":"Сказать привет"}]}]]

ui.registerPanel("main", "Hello Metasiberia", layout, function(action_id)
	if action_id == "say_hello" then
		ui.showNotification("Hello Metasiberia", "Привет, Метасибирь!")
	end
end)
