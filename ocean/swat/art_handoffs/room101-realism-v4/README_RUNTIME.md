# Room101 v4 runtime delta

Eight render-only instance replacements over complete v3. Native engine v4 validation is pending.
Read RUNTIME_HANDOFF.md before binding. Verify RUNTIME_SHA256SUMS.

GLBs embed their textures; selected unpacked runtime maps and precise glTF factors are retained for inspection. No engine code, collider, new authority ID, transparent pane or light setup is supplied. Companion review and full source packages are delivered separately; image paths mentioned in the contracts live in those companions.

V3 remains the fallback. Original scene, high-resolution maps and deterministic build scripts are in the full source archive. All eight GLBs were rebuilt byte-for-byte in a separate directory.
