# Discard cache and neighborhood reload tests

```sh
dotnet run --project tools/simitone/save-tests/DiscardTests.csproj -c Release
```

The project source-links both actual working TS1 providers. Real published
FSO.Content, FSO.Common and FSO.Files supply cache/base classes and IFF parsing
and writing. No discard algorithm or file-reader implementation is copied.

A `TS1ObjectProvider` subclass calls its ordinary `(null,null)` constructor and
exposes only its protected caches. Opaque GameObjectResource values use the
ordinary null-IFF constructor, and are never read by cache eviction. The catalog
is seeded through its private static field to verify it survives eviction.

The neighborhood case uses actual synthetic IFF files in a private temporary
directory and an uninitialized real Content object **only as a WorldObjects
service holder**, avoiding its game-resource initialization. It executes the
real constructor, InitSpecific, LoadCharacters and ReloadCurrent. It checks
disk-backed budget restoration, independently retained neighborhood ID/path,
fresh family/cross-lot state, uppercase IFF handling, ignored non-game files,
old GUID/resource eviction and new GUID indexing. It never runs the engine,
opens a UI, or reads commercial game files. This is not a full gameplay test.
