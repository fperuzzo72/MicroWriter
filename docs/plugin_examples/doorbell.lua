-- doorbell.lua -- example SUMI plugin
--
-- Turns the X4/X3 into a wireless doorbell. Pair the device with
-- sumi.page/bridge's "Doorbell Notifier" handler and every press of
-- the Center button will fire a notification + chime on the laptop.
--
-- This is the canonical "Plugin Bridge" demo. Pair it with the HTML
-- snippet in docs/plugin_examples/doorbell.html on the site side.
--
-- Install:
--   1. In sumi.page, drag this file into File Upload -> transfers to
--      /custom/doorbell.lua on the device.
--   2. Home -> Apps -> Doorbell.
--   3. On sumi.page/bridge, paste doorbell.html into Custom Handler.

local label = "front door"
local ring_count = 0
local last_ring_ms = 0
local last_connected = false

function init(w, h)
    -- Deliberately NOT publishing a hello here -- the user typically opens
    -- the plugin before pairing, so the site isn't listening yet. update()
    -- detects the pair event below and publishes on the 0->1 transition.
end

function update()
    local now_connected = bridge.connected()

    if now_connected and not last_connected then
        -- Fresh pairing -- greet the site so it can label the device.
        bridge.publish("doorbell/hello", { name = label })
    end
    last_connected = now_connected

    -- Renew the keep-awake window every tick while paired so the device
    -- doesn't fall asleep and miss a press. 60s is well under the 1-hour
    -- per-call cap and easy for the power manager to bound.
    if now_connected then
        bridge.keep_awake(60)
    end
    return false
end

function draw()
    fillScreen(WHITE)
    drawHeader("Doorbell")

    setCursor(MARGIN, HEADER_H + 40)
    setTextColor(BLACK)

    if bridge.connected() then
        textLine("Paired: " .. label)
    else
        textLine("Waiting for pair...")
        textLine("Open sumi.page/bridge")
    end

    textLine("")
    textLine("Rings this session: " .. ring_count)

    local awake = bridge.remaining_awake()
    if awake > 0 then
        textLine("Awake " .. awake .. "s")
    end

    drawFooter("Back: exit", "Center: ring")
end

function onButton(btn)
    if btn == "center" then
        ring_count = ring_count + 1
        last_ring_ms = millis()
        bridge.publish("doorbell/ring", {
            at   = getTime(),
            name = label,
            seq  = ring_count,
        })
        return true
    end

    -- Let Back bubble up to the plugin host so the user can exit.
    if btn == "back" then return false end

    return true
end
