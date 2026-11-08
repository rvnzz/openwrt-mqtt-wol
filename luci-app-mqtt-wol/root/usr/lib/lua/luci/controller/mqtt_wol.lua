local io = io
local string = string

module("luci.controller.mqtt_wol", package.seeall)

local ACL = { "luci-app-mqtt-wol" }

function index()
	-- guard optional dependencies: an uncaught Lua error inside index()
	-- aborts the whole menu index build and 404s every LuCI page
	if not (nixio and nixio.fs and nixio.fs.access("/etc/config/mqtt-wol")) then
		return
	end

	local function register()
		-- firstchild() instead of alias(): on 25.12's ucode dispatcher an
		-- argument-less alias resolves to an empty path and 404s the page
		local page = entry({ "admin", "services", "mqtt-wol" }, firstchild(),
			_("MQTT Wake-On-LAN"))
		page.dependent = true
		page.acl = ACL
		page.order = 60

		local e = entry({ "admin", "services", "mqtt-wol", "settings" },
			cbi("mqtt_wol/settings"), _("Settings"), 10)
		e.acl = ACL

		e = entry({ "admin", "services", "mqtt-wol", "status" },
			template("mqtt_wol/status"), _("Status"), 20)
		e.acl = ACL

		e = entry({ "admin", "services", "mqtt-wol", "api" },
			call("api_status"), nil)
		e.acl = ACL
		e.leaf = true

		e = entry({ "admin", "services", "mqtt-wol", "wake" },
			post("action_wake"), nil)
		e.acl = ACL
		e.leaf = true

		e = entry({ "admin", "services", "mqtt-wol", "reload" },
			post("action_reload"), nil)
		e.acl = ACL
		e.leaf = true
	end

	local ok, err = pcall(register)
	if not ok then
		io.stderr:write("mqtt-wol controller failed: " .. tostring(err) .. "\n")
	end
end

-- restricted character whitelist, safe to embed into a single-quoted shell
-- argument and into a JSON string
local function sanitize_target(t)
	if not t or #t > 64 then
		return nil
	end
	return t:match("^([%w%s:%.%-_%+]+)$")
end

local function shell_quote(s)
	return "'" .. (s or ""):gsub("'", "'\\''") .. "'"
end

-- run `ubus [-S] call mqtt-wol <method> [json]`, return trimmed output
local function ubus_call(method, argjson, scriptmode)
	local cmd = "ubus " .. (scriptmode and "-S " or "") .. "-t 5 call mqtt-wol " .. method
	if argjson then
		cmd = cmd .. " " .. shell_quote(argjson)
	end

	local f = io.popen(cmd .. " 2>&1")
	if not f then
		return nil
	end

	local out = f:read("*a") or ""
	f:close()

	return (out:gsub("^%s*(.-)%s*$", "%1"))
end

-- normalize anything ubus printed into valid JSON for the browser
local function to_json(out, fallback_msg)
	if out and out:match("^{") then
		return out
	end

	local msg = (out and #out > 0 and out) or fallback_msg
	msg = msg:gsub('["\\\r\n]', " ")
	return string.format('{"result":"ERROR","message":"%s"}', msg)
end

function api_status()
	luci.http.prepare_content("application/json")

	local out = ubus_call("status", nil, true)
	if not out or out == "" or out:match("^Command failed") then
		luci.http.write('{"error":"daemon-unreachable"}')
		return
	end

	luci.http.write(out)
end

function action_wake()
	luci.http.prepare_content("application/json")

	local target = sanitize_target(luci.http.formvalue("target"))
	if not target then
		luci.http.write('{"result":"ERROR","message":"invalid target"}')
		return
	end

	local out = ubus_call("wake", string.format('{"target":"%s"}', target), true)
	luci.http.write(to_json(out, "ubus call failed: is mqtt-wol running?"))
end

function action_reload()
	luci.http.prepare_content("application/json")

	local out = ubus_call("reload", "{}", true)
	luci.http.write(to_json(out, "ubus call failed: is mqtt-wol running?"))
end
