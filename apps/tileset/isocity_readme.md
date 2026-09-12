# Folder Contents

Holds classes for objects that use IsoTerrain to navigate around.

Added:
- `IsoCar` — simple car object that can be given a world-space target position and will move towards it at a set speed.
 - `IsoPath` — helper for building road-only paths between `IsoCell`s; used by `IsoCar` to follow roads to a target cell.
