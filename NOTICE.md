# OpenDollhouse: origins and license scope

The root license (`LICENSE`) is the Mozilla Public License 2.0 for original
OpenDollhouse contributions whose distribution under those terms has been
authorized by the respective contributor. It does not replace third-party
notices, attribute their work to OpenDollhouse or grant rights on behalf of
unidentified authors. Do not infer a new license from a file's directory.

## Incorporated engine and references

- **Sims3DS**, by Ryan and contributors, revision
  `1bb47534a1f27667d5517b2c06c783988bd5ee67`: C++ codebase incorporated into
  `engine/sims3ds`, MPL-2.0, with the specific exceptions below. Preserve file
  notices, `engine/sims3ds/LICENSE`, `CREDITS.md` and `UPSTREAM.json`.
- **FreeSims**, by francot514 and contributors, and **FreeSO**, by riperiperi
  (Rhys Simpson) and contributors: sources of the Sims3DS translations and
  adaptations identified in the code; MPL-2.0 in the consulted notices.
  No authenticated FreeSims base SHA was established for the entire Sims3DS
  translation.
- **Original Simitone and the alexjyong fork**: functional references, each
  with its own FreeSO submodule. See `docs/references.md` for revisions and
  components. A submodule's license is not blanket permission for the frontend.
  Preserve provenance and check the applicable notice before copying or
  translating a component; studying behavior does not imply copying code.
- **FSOMonoGame/MonoGame**: a reference for mathematical and graphics operations,
  identified in `docs/references.md`. Its managed runtime does not accompany
  the native application. Do not apply one reference's Ms-PL license to all ODH.

## Components with their own notices

| Incorporated component | Authors and license | Files and notice |
| --- | --- | --- |
| xxHash32 via xxHashSharp | Seok-Ju Yun; original C implementation by Yann Collet; BSD 2-Clause | `engine/sims3ds/src/files/Hash.cpp` and `Hash.h`; full text in `engine/sims3ds/LICENSES/BSD-2-Clause-xxHashSharp.txt`. |
| minimp3 | lieff and contributors; CC0 1.0 dedication | `engine/sims3ds/src/third_party/minimp3/minimp3.h` and `LICENSE`. |
| Inter glyphs | The Inter Project Authors; SIL OFL 1.1 | `engine/sims3ds/src/app/FontData.h`; full text in `engine/sims3ds/LICENSES/OFL-1.1-Inter.txt`. Glyph data retains the font's license. |

The original Sims3DS credits also describe 3DS artwork, tools and a web builder
in its upstream repository. Mentioning these items does not include them in
this project; those links are not a Linux package inventory. Original notices
remain intact.

## Linux package and system libraries

The package includes the project license as `licenses/OpenDollhouse-MPL-2.0.txt`
and this notice as `licenses/OpenDollhouse-NOTICE.md`, alongside the engine's
specific notices. `source-snapshot.tar.gz` contains the local sources and these
texts; `BUILD.json` identifies the executable, required libraries and limits.

SDL2, zlib, Pango/PangoCairo, GLib/GObject, Cairo and the C/C++ runtime libraries
come from the system; their binaries are not included in this Linux package.
If a future distribution bundles them, check their versions, sources and notices
separately. Their absence from the archive does not promise compatibility with
every distribution.

The Sims and its expansions are works of Maxis/Electronic Arts. Their commercial
data, the player's installation and third-party patches are not included in
the package. This notice grants no rights to those works or trademarks and is
not a declaration that redistribution of the entire collection is approved.
