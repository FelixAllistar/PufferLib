# Character and dependency provenance

`body.c` and `body.h` began as local copies of
`ocean/shenaniguns3d/character.c` and `character.h` at repository commit
`6e3e0ef19`. Those files preserved the previously unversioned `pd64` C port of
Box3D's `samples/sample_character.cpp` (`RigidbodyCharacter`), which credits
s&box's `PlayerController`. The source comments retain that ancestry.

Original snapshot SHA-256 before the SWAT fork:

- `character.c`: `0db4023c191f649532de6ef343754b6cb98e4d92cd657c67fc9595231a24c01b`
- `character.h`: `411954928be6557ae29276133bf655e4275d25148a80f2964156c85c83517d26`

SWAT namespaces the API and adds collision-limited physical upper-body lean,
lean-aware standing clearance, consistent mass when leaning, and capped
horizontal acceleration that leaves vertical velocity to gravity/jumping.
Tactical control, weapons, scene, mission, rendering and the RL contract live
in SWAT's own modules. The old game's environment, custom encoder, and
checkpoint format are not dependencies of SWAT.

Physics dependency: `FelixAllistar/box3d` at
`c4a414fcfe612a704dcd06ce921348d441271fc7`, built as a separate static library.
The required public headers and library are loaded from a sibling checkout;
no dependency binary is committed. Presentation uses the repository's Raylib
5.5 dependency. The game adds no third-party game art or audio.

The Box3D copyright/license notice associated with the dependency and sample
ancestry is reproduced below. The surrounding repository's license continues
to apply to its own code.

```text
MIT License

Copyright (c) 2026 Erin Catto

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
