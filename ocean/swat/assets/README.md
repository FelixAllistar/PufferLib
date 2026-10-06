# SWAT runtime art

This is the tracked source for the player assets: character animation banks and
textures, upper-body gear, rigid weapon revisions, environments, audio and UI.
Make, CMake and the Windows builder copy this directory beside their executables.
`build/swat/assets` is generated output, not the source of truth.

Original deliveries, editable Blender scenes, measurements, review captures and
source audio live in [../art_handoffs](../art_handoffs). Keep provenance and license
records with their assets. Character and weapon rendering retain the supplied
geometry; the consumer fits it to the simulation's poses and measured bindings.
