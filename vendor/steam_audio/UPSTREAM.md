# Steam Audio public headers and notices

Source: https://github.com/ValveSoftware/steam-audio/tree/v4.8.1

Release: `v4.8.1`

SDK: https://github.com/ValveSoftware/steam-audio/releases/download/v4.8.1/steamaudio_4.8.1.zip

SDK SHA-256: `4a0aa5ec1176f38f0b0993a37c2259d9e86f27e22d5e24f83ec4c3cb9a1d5449`

`phonon.h`, `phonon_version.h` and `THIRDPARTY.md` are unmodified files from
that SDK archive. `LICENSE.md` is the unmodified Apache 2.0 license from the
tagged source repository. Upstream line endings are preserved.

SWAT dynamically loads the optional native runtime and uses its binaural HRTF
API. Runtime binaries are downloaded by `ocean/swat/setup_audio.py` into ignored
build directories; they are not vendored. The setup/build scripts copy these
license and third-party notices beside distributed runtime libraries.
