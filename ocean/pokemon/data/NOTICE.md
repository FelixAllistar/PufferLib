# Data provenance

The snapshot `smogon-gen1.json` contains moveset data from Smogon University
and its contributors, obtained via https://data.pkmn.cc/sets/gen1.json.
See `source.json` for retrieval time and SHA-256, and
https://pkmn.github.io/smogon/data/sets/ for the dataset documentation.

Smogon retains copyright in its set/analysis data. The pkmn/smogon API code's
MIT license does not license these datasets; no claim of an MIT license for
this data is made here. No strategy analysis prose is imported.

`catalog.json` records each generated variant's species, original format,
template, moves and source species. Slash alternatives are expanded, duplicate
move combinations removed, and every retained set is validated as a level-100
RBY OU set with pokemon-showdown@0.11.11. These are recommendations adapted
from multiple formats, not observed usage frequencies or guaranteed strong OU
sets. Original format levels/stat choices are not imported.

Formats considered, in preference order: OU, UU, NU, PU, ZU, LC Level 100,
LC, Middle Cup and Ubers (excluding Mew and Mewtwo). Duplicate sets retain
the first provenance. Stadium, tradebacks, rentals and altered-movepool
formats are excluded.

Metapod and Kakuna have no entries in this snapshot. Their entries inherit
the Caterpie/Weedle recommendations and pass OU validation, and are explicitly
marked `inherited: true`. They are not claimed to be Smogon analyses of those
two evolved species.

Regenerate deterministically with `node ocean/pokemon/import_sets.cjs` after
installing the pinned validation dependency (`make -C ocean/pokemon validate`).
`--check` validates the snapshot and verifies generated outputs without writing.
Only `--refresh` fetches a new snapshot; refreshing changes set semantics and
checkpoint compatibility and must be reviewed explicitly.
