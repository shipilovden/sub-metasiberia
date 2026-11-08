-- Offline parser example. This fixture is historical test data, not live weather.
local fixture = [[
{"name":"Novokuznetsk","main":{"temp":-8.4,"feels_like":-13.2,"humidity":72},"weather":[{"main":"Clouds","description":"broken clouds"}],"wind":{"speed":4.1}}
]]

local function decodeJson(source)
	local position = 1
	local length = #source
	local nullValue = {}

	local function skipWhitespace()
		while position <= length do
			local ch = string.sub(source, position, position)
			if ch == " " or ch == "\n" or ch == "\r" or ch == "\t" then
				position += 1
			else
				break
			end
		end
	end

	local function parseString()
		if string.sub(source, position, position) ~= '"' then error("expected string") end
		position += 1
		local pieces = {}
		local start = position
		while position <= length do
			local ch = string.sub(source, position, position)
			if ch == '"' then
				pieces[#pieces + 1] = string.sub(source, start, position - 1)
				position += 1
				return table.concat(pieces)
			elseif ch == "\\" then
				pieces[#pieces + 1] = string.sub(source, start, position - 1)
				position += 1
				local escaped = string.sub(source, position, position)
				local simple = { ['"'] = '"', ["\\"] = "\\", ["/"] = "/", b = "\b", f = "\f", n = "\n", r = "\r", t = "\t" }
				if simple[escaped] then
					pieces[#pieces + 1] = simple[escaped]
					position += 1
				elseif escaped == "u" then
					local hex = string.sub(source, position + 1, position + 4)
					local code = tonumber(hex, 16)
					if not code then error("invalid unicode escape") end
					position += 5
					if code >= 0xD800 and code <= 0xDBFF and string.sub(source, position, position + 1) == "\\u" then
						local low = tonumber(string.sub(source, position + 2, position + 5), 16)
						if not low or low < 0xDC00 or low > 0xDFFF then error("invalid surrogate pair") end
						code = 0x10000 + (code - 0xD800) * 0x400 + low - 0xDC00
						position += 6
					end
					pieces[#pieces + 1] = utf8.char(code)
				else
					error("invalid string escape")
				end
				start = position
			elseif string.byte(ch) < 32 then
				error("control character in string")
			else
				position += 1
			end
		end
		error("unterminated string")
	end

	local parseValue
	local function parseArray()
		position += 1
		skipWhitespace()
		local array = {}
		if string.sub(source, position, position) == "]" then position += 1; return array end
		while true do
			array[#array + 1] = parseValue()
			skipWhitespace()
			local delimiter = string.sub(source, position, position)
			if delimiter == "]" then position += 1; return array end
			if delimiter ~= "," then error("expected array delimiter") end
			position += 1
			skipWhitespace()
		end
	end

	local function parseObject()
		position += 1
		skipWhitespace()
		local object = {}
		if string.sub(source, position, position) == "}" then position += 1; return object end
		while true do
			local key = parseString()
			skipWhitespace()
			if string.sub(source, position, position) ~= ":" then error("expected object separator") end
			position += 1
			skipWhitespace()
			object[key] = parseValue()
			skipWhitespace()
			local delimiter = string.sub(source, position, position)
			if delimiter == "}" then position += 1; return object end
			if delimiter ~= "," then error("expected object delimiter") end
			position += 1
			skipWhitespace()
		end
	end

	parseValue = function()
		skipWhitespace()
		local ch = string.sub(source, position, position)
		if ch == '"' then return parseString() end
		if ch == "{" then return parseObject() end
		if ch == "[" then return parseArray() end
		if string.sub(source, position, position + 3) == "true" then position += 4; return true end
		if string.sub(source, position, position + 4) == "false" then position += 5; return false end
		if string.sub(source, position, position + 3) == "null" then position += 4; return nullValue end
		local start = position
		while position <= length and string.match(string.sub(source, position, position), "[%d%.eE%+%-]") do position += 1 end
		if start == position then error("invalid value") end
		local number = tonumber(string.sub(source, start, position - 1))
		if not number then error("invalid number") end
		return number
	end

	local ok, decoded = pcall(function()
		local result = parseValue()
		skipWhitespace()
		if position <= length then error("trailing JSON data") end
		return result
	end)
	if not ok then return nil, tostring(decoded) end
	return decoded
end

ui.registerCommand("weather.fixture", "Weather fixture demo", function()
	local data, parseError = decodeJson(fixture)
	if not data then
		ui.showNotification("Weather fixture", "JSON parsing failed: " .. string.sub(parseError, 1, 300))
		return
	end
	local weather = data.weather[1]
	local summary = string.format("%s · %s\nTemperature: %.1f °C · feels like %.1f °C\nHumidity: %d%% · wind: %.1f m/s\nOFFLINE FIXTURE — not live weather",
		data.name, weather.description, data.main.temp, data.main.feels_like, data.main.humidity, data.wind.speed)
	ui.showNotification("Weather fixture · offline", summary)
end)
