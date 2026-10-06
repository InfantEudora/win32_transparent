---
name: object-setmesh-releases-old
description: Object::SetMesh releases the old mesh and the last release deletes it - swapping meshes between shared Mesh* frees them unless you Retain; shows as GL invalid-VAO errors then heap corruption
metadata:
  node_type: memory
  type: project
  originSessionId: f74358c8-36a9-48cf-a846-52036d5d2ea0
  modified: 2026-10-06T10:02:05.536Z
---

`Object::SetMesh(new)` Retains the new mesh and Releases the old; the last Release deletes the mesh. When objects swap among a shared pool of meshes (e.g. chasm's worker figures, `ApplicationChasmWorkersView.cpp`), a pool mesh is freed once every object has left it. The renderer still writes `mesh->batch_index` (`SetMeshBatchIndex`) into the freed mesh, and an object swapping back draws a deleted VAO.

**Why:** found 2026-10-06. Symptoms were GL_INVALID_OPERATION "VAO names must be generated" spam, then a crash. Under gdb it showed as "Free Heap block modified after it was freed", caught in unrelated frees, which sent c4 hunting in the economy sim.

**How to apply:** call `mesh->Retain()` once on every mesh you keep in a pool and hand out with SetMesh. Suspect this first when GL "invalid VAO/buffer" errors start right after objects change meshes.
