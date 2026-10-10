# Frozen window parts receipt

Received 2026-10-10 from [DataDyne art thread](https://datadynedesigngroup.slack.com/archives/C0C5TN3AA8K/p1791620758827679). [Drive backup](https://drive.google.com/file/d/1pWreDajSeGAigINlS2sJ1rxMNy7QuJnP/view). Original CC0 batch 15 addendum; original art and license retained inside the packet.

Archive SHA-256: ca55eccfd4e33103980b43176dd5d19ca09d9dcaef653b4426b6ae5064203364

All 24 payload hashes in SHA256SUMS.txt verified. Original runtime GLBs match the packet's original hashes. C importer output independently checked against the supplied primitive-local triangle mappings and transformed source bounds: four panes, 44 triangles each, tolerance 2 micrometres. No source geometry, materials or UVs edited. Archival Python sources are retained but were not executed.

The 18 mm room and 25 mm lobby solid envelopes are modeled proxies, not fabrication gauges. Curtains intersect panes; frame paint does not establish substrate, and joints/gaskets are unspecified. The engine separates only the glass. Unknown frames and curtain folds retain the existing aggregate behavior; curtain-covered openings remain blocked. The next art version must resolve those limitations separately.

Reproduce the generated pane header from ocean/swat with:

```sh
cc -O2 tools/import_motel_panes.c -lm -o /tmp/import_motel_panes
/tmp/import_motel_panes > motel_panes_data.h
```
