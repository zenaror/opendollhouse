# Credits and licenses

OpenDollhouse is an independent C/C++ project using The Sims 1 data. It is not
an official Maxis or Electronic Arts product. Commercial game files are not
included; each player supplies their own installation.

## Origins and references

The incorporated engine comes from Sims3DS, with origins in FreeSims and FreeSO.
Original Simitone and the alexjyong fork are functional references. Consulting
a project does not mean all of its code or resources were included. The
commits below identify the consulted sources and preserve attribution.

| Project and authors | Revision | Use |
|---|---|---|
| [Simitone, by riperiperi and contributors](https://github.com/riperiperi/Simitone/tree/4906db3156616bfaffc10ce0963cf08062975fef) | `4906db3156616bfaffc10ce0963cf08062975fef` | TS1 frontend and integration. |
| [FreeSO pinned by this Simitone](https://github.com/riperiperi/FreeSO/tree/f86bbd34b2112296ed786741f640a0535886d180) | `f86bbd34b2112296ed786741f640a0535886d180` | Matching submodule; distinct from main FreeSO. |
| [Simitone fork by alexjyong and contributors](https://github.com/alexjyong/Simitone/tree/d87169d8db5f3f688849319d6dbf5f8eb7e5a3ed) | `d87169d8db5f3f688849319d6dbf5f8eb7e5a3ed` | Fixes and features compatible with the original experience, alongside original Simitone. |
| [FreeSO matching the selected fork](https://github.com/alexjyong/FreeSO/tree/a3c6c5aaa91e60b024d8e78dc7a925984f3c6562) | `a3c6c5aaa91e60b024d8e78dc7a925984f3c6562` | Selected fork submodule; do not replace with original Simitone’s submodule. |
| [Previously consulted Alex Simitone](https://github.com/alexjyong/Simitone/tree/e36e83f95d44b9f4b378e7665c4e2ecbaa88b4ab) | `e36e83f95d44b9f4b378e7665c4e2ecbaa88b4ab` | Provenance of the earlier ODH implementation; does not define milestone 1 alone. |
| [FreeSO from the earlier Alex reference](https://github.com/alexjyong/FreeSO/tree/7e76c1960252ddd033d8937d5edb47d326f5b69c) | `7e76c1960252ddd033d8937d5edb47d326f5b69c` | Reference consulted by the earlier implementation; not the selected submodule. |
| [Main FreeSO, by riperiperi and contributors](https://github.com/riperiperi/FreeSO/tree/4c6b3e8f5835b228723caea3c9f683c62f244f73) | `4c6b3e8f5835b228723caea3c9f683c62f244f73` | Initial format, VM, audio and implementation comparison reference. |
| [FSOMonoGame and contributors](https://github.com/riperiperi/FSOMonoGame/tree/74f7ef558111e62a7d0a2228b6f9714980e000fa) | `74f7ef558111e62a7d0a2228b6f9714980e000fa` | Reference for graphics and mathematical operation semantics. |
| [Sims3DS, by Ryan and contributors](https://github.com/Ryan3DSMods/Sims3DS/tree/1bb47534a1f27667d5517b2c06c783988bd5ee67) | `1bb47534a1f27667d5517b2c06c783988bd5ee67` | C++ codebase incorporated into the engine. |

## Component licenses

- **FreeSO:** the consulted revision declares the
  [Mozilla Public License 2.0](https://github.com/riperiperi/FreeSO/blob/7e76c1960252ddd033d8937d5edb47d326f5b69c/LICENSE.md).
  Adaptations must retain notices, provenance and applicable obligations for
  derived files. Credit belongs to upstream authors and contributors.
- **Simitone and the alexjyong fork:** retain the frontends' own credits.
  The FreeSO submodule's license does not establish the license of every
  frontend file. Check each component's notices before incorporating it.
- **FSOMonoGame:** the consulted revision declares the
  [Microsoft Public License, Ms-PL](https://github.com/riperiperi/FSOMonoGame/blob/74f7ef558111e62a7d0a2228b6f9714980e000fa/LICENSE.txt),
  crediting the MonoGame team. Consulting its interpolation operations does
  not make that managed runtime a dependency of the native renderer.
- **Sims3DS:** the engine declares
  [MPL-2.0](https://github.com/Ryan3DSMods/Sims3DS/blob/1bb47534a1f27667d5517b2c06c783988bd5ee67/LICENSE)
  and preserves credits to FreeSims, FreeSO and their contributors. Its
  [project credits](https://github.com/Ryan3DSMods/Sims3DS/blob/1bb47534a1f27667d5517b2c06c783988bd5ee67/CREDITS.md)
  distinguish font, logo and other resource licenses. The engine license does
  not replace those attributions or cover commercial game data.
- **Other libraries and tools:** their licenses remain separate. Reused code,
  shaders or resources must record author, license and revision. A code license
  does not grant rights to commercial game data.

Authorized original contributions use [MPL-2.0](../LICENSE).
The [origins and license notice](../NOTICE.md) explains each component's scope;
the engine's [original credits](../engine/sims3ds/CREDITS.md) are preserved.
When sharing a package, keep its licenses, notices and corresponding sources.
