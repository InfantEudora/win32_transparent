# Setting the charger's power setpoint (PBaseline)

## Symptom

A session on the Nissan/Heliox FE20 V2G charger (started via `Remote Start` in
`ApplicationOCPP.cpp`) sits in `Charging` state but delivers 0 W. Confirmed on
the charger side by sniffing its local MQTT bus during an active session:
`ame/v2g/charge-controller/state` reports `"power_setpoint":0.0`, and
`config.ini` on the unit has `p_baseline = 0`.

## Root cause

The UI's "Set Current Limit for Session" slider
(`OCPPConnectorState::server_current_limit` in `ApplicationOCPP.cpp`) drives
`OCPPServerHandler::SendSetChargingProfile()`, which sends an OCPP
**SmartCharging profile** `SetChargingProfile.req`.

The charger's firmware (`v2g-charge-controller-1.0.1.jar`, decompiled/
disassembled for this investigation — see the `nissan_laadpaal` repo,
`POWER_SETPOINT.md`) only registers an OCPP **Core** profile client handler
(`nu.ame.ocpp.V2GOcppManager$1`, implementing `ClientCoreEventHandler`):
`ChangeAvailability`, `GetConfiguration`, `ChangeConfiguration`, `ClearCache`,
`DataTransfer`, `RemoteStartTransaction`, `RemoteStopTransaction`, `Reset`,
`UnlockConnector`. There is no SmartCharging profile handler at all, so
`SetChargingProfile.req` is not implemented by this charger and has no effect
— that's why the current slider doesn't move the power.

Confirms the mechanism actually is real and used in the field: the comment
block above `HandleMessage()` in `OCPPServerHandler.cpp` (line ~124) already
has a captured real `DataTransfer` from the Heliox/Fermata backend showing
`p_baseline` driving `output_power` directly:
```
"data":"{\"local_mode\":false,\"p_baseline\":7000,...,\"output_power\":6678}"
```

## The actual mechanism: OCPP `ChangeConfiguration.req`

The charger's `handleChangeConfigurationRequest` looks up the `key` from the
request, maps it to an internal setting via a `camelToSnake` conversion, parses
`value` per that setting's declared type, and applies it — this is standard
OCPP 1.6 Core Profile, not vendor-specific.

The key naming is not a straight camelCase of the internal name. Disassembled
the charger's `Utils.snakeToCamel`: it does `_` → space, `WordUtils.capitalize`,
then strips spaces. For `p_baseline` that yields **`PBaseline`** (capital P) —
confirmed against the vendor's own Service Tool, whose slider is literally
labeled `Set 'PBaseline'`.

So the call to send is a plain OCPP `ChangeConfiguration.req`:

```json
[2, "<msgId>", "ChangeConfiguration", {"key": "PBaseline", "value": "5000"}]
```

- Positive = charge (import), negative = discharge/V2G (export). Vendor tool's
  slider range was -20000..20000 (Watts).
- `PowerLimit` (`power_limit` internally, currently `10000` on this unit) is a
  separate cap on the magnitude — same mechanism, `{"key": "PowerLimit", "value": "..."}`.
- Same pattern works for any other setting exposed over OCPP (`QBaseline`,
  `LocalBaseline`, `OcppAddress`, etc.) — PascalCase the snake_case name.

## Suggested implementation

Add a generic `SendChangeConfiguration` to `OCPPServerHandler`, matching the
existing `SendRemoteStartTransaction` pattern exactly (`core/OCPPServerHandler.h`
/ `.cpp`):

```cpp
// OCPPServerHandler.h, alongside the other Send* declarations:
bool SendChangeConfiguration(SOCKET clientSocket, const std::string& key, const std::string& value);
```

```cpp
// OCPPServerHandler.cpp, modeled on SendRemoteStartTransaction (~line 784):
bool OCPPServerHandler::SendChangeConfiguration(SOCKET clientSocket, const std::string& key, const std::string& value)
{
	static int nextMsgId = 4000;
	std::string msgId = std::to_string(nextMsgId++);

	json call = json::array();
	call.push_back(2); // Message type: CALL
	call.push_back(msgId);
	call.push_back("ChangeConfiguration");

	json payload;
	payload["key"] = key;
	payload["value"] = value;
	call.push_back(payload);

	std::string message = call.dump();
	ocpp_debug->Info("Sending ChangeConfiguration %s=%s\n", key.c_str(), value.c_str());
	ocpp_debug->Trace("ChangeConfiguration message: %s\n", message.c_str());

	bool result = m_sendMessage(clientSocket, message);
	if (result) {
		ocpp_debug->Ok("ChangeConfiguration sent successfully\n");
	} else {
		ocpp_debug->Err("Failed to send ChangeConfiguration\n");
	}

	return result;
}
```

Note: `HandleMessage()` currently only handles `msgType == 2` (CALL messages
*from* the charger); the CALLRESULT (`msgType == 3`) that will come back for
this outbound `ChangeConfiguration` — like the existing `RemoteStartTransaction`
/ `RemoteStopTransaction` calls — is not parsed anywhere and will just be
logged and dropped. That's fine for a fire-and-forget control action, same as
the existing Remote Start/Stop buttons; no change needed there unless you want
to surface `Accepted`/`Rejected` in the UI.

### UI hook

In `ApplicationOCPP.cpp::RenderOCPPServerUI()`, alongside the existing
"Set Current Limit for Session" slider and Remote Start/Stop buttons
(~line 135), add a direct PBaseline control, e.g.:

```cpp
static int pBaselineWatts = 0;
ImGui::SetNextItemWidth(120);
ImGui::InputInt("PBaseline (W)", &pBaselineWatts);
ImGui::SameLine();
if (ImGui::Button("Set Power")){
    http_server->ocpp.SendChangeConfiguration(client_socket, "PBaseline", std::to_string(pBaselineWatts));
}
```

(`client_socket` is already in scope in that loop, same as used by the
existing Remote Start/Stop buttons.)

### Optional: surface p_baseline in the UI

`HandleDataTransfer()` already parses the charger's `customMeterValues`
`DataTransfer` payload for `session_active` and `output_power` (~line 700).
That same payload also carries `p_baseline`, `q_baseline`, `p_max`, `p_min`,
etc. — worth pulling `p_baseline` out the same way if you want to show the
charger's actual applied setpoint (as opposed to what was last sent) in
`RenderOCPPServerUI()`.

## Background reference

Full reverse-engineering notes (jar extraction method, bytecode evidence,
SSH access to the unit) live in the sibling repo
`nissan_laadpaal/POWER_SETPOINT.md` and `nissan_laadpaal/LOCAL_CHARGING_MODE.md`.
