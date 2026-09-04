# Surfacing charger fault detail, and a StopTransaction crash bug

Two findings from investigating a real `Faulted` incident on the charger
(latch failure, see `nissan_laadpaal` repo conversation for the full incident
timeline). Both confirmed by disassembling the charger's
`v2g-charge-controller-1.0.1.jar` (pulled over SSH) and this repo's own
`OCPPServerHandler.cpp` — not guesses.

## 1. Bug: StopTransaction confirmation crashes the charger's OCPP client

### Symptom

In the middle of an aborted session, the charger's log showed:
```
[WARN ] Session.java:186 Unable to process action
java.lang.NullPointerException
	at eu.chargetime.ocpp.Session$CommunicatorEventHandler.onCallResult(Session.java:169)
	at eu.chargetime.ocpp.Communicator$EventHandler.receivedMessage(Communicator.java:282)
	...
[ERROR] Communicator.java:225 An error occurred. Sending this information: uniqueId <uuid>: action: null, errorCore: FormationViolation, errorDescription: Unable to process action
```

### Root cause

`OCPPServerHandler::HandleStopTransaction` (`core/OCPPServerHandler.cpp`,
~line 628):

```cpp
json resp = json::array();
resp.push_back(3);
resp.push_back(msgId);
json result;
if (!idTag.empty()) {
    json idTagInfo;
    idTagInfo["status"] = "Accepted";
    result["idTagInfo"] = idTagInfo;
}
resp.push_back(result);
```

`json result;` default-constructs to nlohmann::json's **null** type, not an
empty object. If the incoming `StopTransaction.req` has no `idTag` (true for
an FSM-internal/aborted stop, not triggered by a badge tap — exactly the case
during the latch-failure incident), `result` is never assigned a key and stays
`null`. The reply sent is `[3, "<msgId>", null]` instead of `[3, "<msgId>", {}]`.

Disassembled the charger's OCPP client library (`eu.chargetime.ocpp`, bundled
in the jar) to confirm what that does downstream:

- `JSONCommunicator.unpackPayload()` is just `gson.fromJson(payload.toString(), type)`.
  Gson's documented behavior: `fromJson("null", SomeClass.class)` returns Java
  `null` rather than throwing.
- `Session$CommunicatorEventHandler.onCallResult()` then calls
  `confirmation.validate()` on that result **without a null check** — that's
  the exact line (`Session.java:169`) the stack trace names. This throws the
  NPE directly, caught by `onCallResult`'s own generic `catch (Exception e)`,
  which reports it back to us as `FormationViolation`.
- For reference, `StopTransactionConfirmation.validate()` itself null-checks
  `idTagInfo` fine (`ifnull` guard before validating it) — the crash is one
  level up, from `confirmation` itself being `null`, not from a null field on
  a real confirmation object.

So sending `{}` would have worked fine (the library's own validate() handles
a missing idTagInfo). Sending JSON `null` does not.

### Fix

In `HandleStopTransaction`, force an object even when empty:

```cpp
json result = json::object();   // instead of: json result;
if (!idTag.empty()) {
    json idTagInfo;
    idTagInfo["status"] = "Accepted";
    result["idTagInfo"] = idTagInfo;
}
```

Worth auditing other `Send*`/`Handle*` methods in this file for the same
`json result;`-then-conditionally-populate pattern — anywhere the condition
can be false, this bug reproduces. `HandleDataTransfer`'s `json result;` +
`result["status"] = "Accepted";` (~line 715) is fine since it's
unconditional; the risky ones are conditionals.

## 2. Getting fault detail to the OCPP backend

### What the user wants

When the connector latch failed to hold, the charger reported OCPP status
`Faulted`, but the only detail visible was the generic `errorCode`. The
underlying cause (CHAdeMO latch holding-circuit failure) was only visible by
SSHing into the unit and reading `/var/log/daemon.log`. Goal: surface that
detail through the normal OCPP channel instead.

### What the charger actually sends (confirmed via `V2GFSM.class` disassembly)

Every `Faulted` `StatusNotification.req` this charger ever sends uses the
same generic `errorCode`: **`InternalError`** (it's the only
`ChargePointErrorCode` value referenced anywhere in `V2GFSM.class` — this
firmware doesn't use more specific ones like `ConnectorLockFailure`). So
`errorCode` alone will never distinguish a latch failure from any other
fault — that part can't be improved without a firmware change we don't
control.

But the `info` field (a separate, free-text OCPP `StatusNotification.req`
field) **is already populated** with something useful — found the exact call
site:

```java
sendStatus(0, ChargePointStatus.Faulted,
    String.format("Stop reason: 0x%x, Error: 0x%x", stopReasonValue, errorValue),
    ChargePointErrorCode.InternalError);
```

i.e. `info` carries the raw numeric CHAdeMO stop-reason/error-cause codes,
e.g. `"Stop reason: 0x7, Error: 0x12"` (made-up example numbers — capture a
live one to get the real values). Note this is **not** the friendly string
(`ERROR_CAUSE_LATCH_FAILURE`) — that decoded string only exists in the
charger's own local log output (`Logger.warn("Error occurred with stop
reason: %s and error code: %s", stopReason, error)`), built from a separate
pair of `String` fields (`EVProtocolData.stopReason` / `.error`) that never
reach OCPP. Only the raw hex codes cross the wire today.

### What's missing on our side

`OCPPServerHandler::HandleStatusNotification` (~line 187) currently parses
`status`, `errorCode`, `timestamp`, `vendorId`, `vendorErrorCode` from the
payload — **it never reads `info`**, even though the full raw JSON is stashed
in a status variable (`ocpp_last_status_<path>/<connectorId>`) that isn't
rendered anywhere in `RenderOCPPServerUI()`.

### Suggested fix

1. Add an `info` field to `OCPPConnectorState` (`core/OCPPServerHandler.h`,
   alongside `errorCode`/`vendorErrorCode`).
2. In `HandleStatusNotification`, parse it like the other string fields:
   ```cpp
   std::string info;
   if (p.contains("info") && p["info"].is_string()) info = p["info"].get<std::string>();
   ...
   if (!info.empty()) conn.info = info;
   ```
3. Show it in `RenderOCPPServerUI()` (`ApplicationOCPP.cpp`, ~line 129,
   alongside the existing `Status`/`SOC`/`AC Voltage`/`Power` lines), e.g.:
   ```cpp
   if (!conn.info.empty()) ImGui::TextColored(ImVec4(1,0.6f,0,1), "Info: %s", conn.info.c_str());
   ```

That alone gets the numeric `"Stop reason: 0x.., Error: 0x.."` codes visible
in the UI for every future fault. To turn those into human names like
`ERROR_CAUSE_LATCH_FAILURE`, you'd need a lookup table — the CHAdeMO
protocol's stop-reason/error-cause codes are standardized (the numeric values
are protocol-level, not vendor-specific), so this is buildable once, without
needing anything further from the charger's firmware. Happy to pull the
code↔name mapping the `chademo-controller` binary uses on the device
(`chademo_triggers.c`) if useful — didn't do that yet since SSH access to the
unit dropped mid-investigation.

## 3. Clearing a `Faulted` state: Soft vs Hard Reset

Measured on the unit on 2026-09-03, after a CHAdeMO plug-button disconnect
left both connectors `Faulted` (`errorCode InternalError`,
`info "Stop reason: 0x8, Error: 0x14000"`). Sent via the Reset button in
`RenderOCPPServerUI()` (`OCPPServerHandler::SendReset`).

| Reset type | Accepted | Reconnect | Connector state after |
| --- | --- | --- | --- |
| `Soft` | yes | immediate | **still `Faulted`** |
| `Hard` | yes | considerably slower | **both cleared to `Available`** |

### What that tells us

Per OCPP 1.6, `Soft` restarts the charge point's OCPP application only, while
`Hard` is a full reboot of the unit. The observed split therefore locates the
fault latch: it lives **below** the OCPP layer, in the charge-controller /
CHAdeMO state machine (`V2GFSM`, the same class that calls `sendStatus(0,
ChargePointStatus.Faulted, ...)` — see section 2). A `Soft` reset restarts the
`eu.chargetime.ocpp` client, which is why the socket drops and comes straight
back, but the FSM keeps running with its fault state intact and simply
re-announces `Faulted` on the new connection. Only a `Hard` reset
re-initialises the FSM, which is what actually clears it.

This also matches the earlier observation that reconnecting the *vehicle* did
not clear the state either — nothing short of restarting the charge controller
does.

### Consequences

- **`Faulted` is expensive to clear on this unit.** There is no cheap
  OCPP-level recovery; a backend that wants to self-heal has to spend a full
  reboot (tens of seconds of downtime) on it.
- Because the firmware routes *every* session abort through `Faulted` +
  `InternalError` (section 2), including an ordinary plug-button stop, a naive
  backend that resets on `Faulted` would hard-reboot the charger every time a
  user unplugs. Decode `info` before deciding to act.

### Untested

`ChangeAvailability` (`Inoperative` → `Operative`) is the other Core-profile
lever this firmware registers and is much cheaper than a `Hard` reset. Whether
it reaches the FSM's fault state is genuinely unknown — the `Soft` result
proves the latch survives an OCPP-layer restart, but `ChangeAvailability` is
handled by the OCPP layer and *forwarded* to the FSM, so it is not the same
test. Worth trying before falling back to `Hard`.

## 4. `ChangeAvailability` is charge-point wide on this unit

Measured on 2026-09-03 with the charger in a healthy `Available` state, using
the per-connector Operative/Inoperative buttons in `RenderOCPPServerUI()`
(`OCPPServerHandler::SendChangeAvailability`).

| Sent | Result |
| --- | --- |
| connector **1** → `Inoperative` | **both** connectors go `Unavailable` |
| connector **1** → `Operative` | **both** connectors go `Available` |

So addressing connector 1 alone still moves connector 0. That is consistent
with the FE20 being a single-socket unit: connector 0 (the charge point) and
connector 1 (the only physical socket) are not independently availability-
controllable, so the firmware treats either address as the whole unit. Sending
to connector 0 is therefore equivalent, not broader.

Note this was measured from `Available`, **not** from `Faulted` — it does not
yet answer the open question from section 3 (whether `Operative` can clear a
`Faulted` state, which would be much cheaper than the `Hard` reset that is
currently the only known way). That test still needs the unit to fault again.

### Reproduced in the test client

`OCPPClient::HandleChangeAvailability` implements this: `connectorId` 0 always
applies to every connector, and a client emulating `FermataV2G` applies any
connectorId to every connector, matching the measurement above. A `GenericAC`
client changes only the connector it was addressed with. Either way it replies
`Accepted` and then sends a `StatusNotification` per changed connector, as a
real charge point does.
