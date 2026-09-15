---
name: engine-forward-is-minus-z
description: "Object forward is -Z; use object_get's world_forward to settle orientation instead of screenshots, which are unreliable while the user drives the camera"
metadata: 
  node_type: memory
  type: reference
  originSessionId: 8a9b69d2-99f4-4de3-a7fd-bee7ffca5edb
  modified: 2026-09-15T10:39:32.833Z
---

**An Object's forward axis is -Z.** Set `yaw_degrees` 0 and `object_get` reports
`world_forward` (0,0,-1); yaw t gives forward `(-sin t, 0, -cos t)`. So:
north(-Z)=0, west(-X)=90, south(+Z)=180, east(+X)=270.

**That is the ENGINE's convention and the ART may disagree.** The bomber character
([[bomber-app]]) faces +Z as exported - the exact opposite of the engine forward - so the yaw that
makes it LOOK in direction D is the one that puts the engine's forward at -D.

**How to settle it without burning screenshots:**

1. `object_get` returns `world_forward` and `world_position`. That is an instrument, not a picture,
   and it answers every question about the ENGINE half exactly.
2. The ART half needs exactly ONE picture: set a known yaw, put the camera on a known side, and
   see whether you get the face, the back or a profile.

**Screenshots are unreliable while the app is on screen and the user is at the mouse.** A
`camera_set` followed by `screenshot` gives no guarantee the camera was still there when the frame
was captured - the user orbiting drags it in between, and the result is a plausible-looking picture
of the wrong thing. This cost several rounds of "fixing" a facing table that was already right.
The fix is to bracket the capture: read `camera_get` before AND after the screenshot and retry
until both match the requested position. See also [[running-app-is-user-driven]].
