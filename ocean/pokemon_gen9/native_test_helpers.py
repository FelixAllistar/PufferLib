"""Initial-state input and read-only snapshots for independent native fixtures."""


class EffectHeap:
    def __init__(self, lib, words):
        self.lib = lib
        self.words = words

    def reset(self, counter=0):
        self.words[0], self.words[32:35] = 40, [0, counter, 0]
        self.lib.pg9_execute(self.words)
        assert self.words[1] == 0

    def import_raw(self, reference, fields):
        # Only supply original pre-action state. Existing IDs are rejected by
        # the native importer, so this cannot replace post-action mutations.
        self.words[0], self.words[32:35] = 40, [8, reference, len(fields)]
        for index, field in enumerate(fields):
            self.words[8192 + index * 5:8197 + index * 5] = field
        self.lib.pg9_execute(self.words)
        assert self.words[1] == 0, (reference, fields, self.words[1])

    def inspect(self, reference):
        self.words[0], self.words[32:35] = 40, [7, reference, 0]
        self.lib.pg9_execute(self.words)
        assert self.words[1] == 0, (reference, self.words[1])
        fields = [list(self.words[8192 + i * 5:8197 + i * 5]) for i in range(self.words[19])]
        return list(self.words[16:19]), fields


def read_text(lib, words, identity):
    """Read an immutable primitive without importing oracle post-action state."""
    words[0], words[32:34] = 53, [0, identity]
    lib.pg9_execute(words)
    assert words[1] == 0, (identity, words[1])
    return list(words[8192:8192 + words[19]])
