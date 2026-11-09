# Asset loader dependencies

Vendored source; no runtime downloads or external DLLs.

- cgltf **v1.15**, MIT: https://github.com/jkuhlmann/cgltf/tree/v1.15
  - `cgltf.h`, `cgltf.LICENSE`
  - Compiled once by `GltfModel.cpp` via `CGLTF_IMPLEMENTATION`.
- nlohmann/json **v3.12.0**, MIT: https://github.com/nlohmann/json/tree/v3.12.0
  - `json.hpp`, `json.LICENSE`
  - Used by `AssetCatalog.cpp` to read `asset_manifest.json`.

glTF transforms, skinning, and animation interpolation follow the Khronos specification:
https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html

Keep these headers at their upstream formatting and retain their license files when redistributing.
