---
name: core-changes-other-apps-separately
description: "After a core change made for one app, do NOT build or run the other apps to check them - the user updates them separately"
metadata:
  node_type: memory
  type: feedback
  originSessionId: b1199cf9-4258-42bc-98ed-184bce0a1618
  modified: 2026-09-26T15:59:25.906Z
---

When working on one app (e.g. archer) and changing core/ or shared_assets/, don't build or run the
other apps to verify them. The user said (2026-09-26): "The other apps will be updated separately,
so no need to check them."

**Why:** other apps are maintained in their own passes (and other agents may be mid-change on
them); checking them costs time and turns up unrelated breakage.

**How to apply:** verify in the app being worked on only; mention in the reply which shared files
changed so the user knows what the other apps will pick up. Related: [[per-app-build-layout]].
