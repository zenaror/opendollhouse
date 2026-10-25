# Eviction persistence regression

Run from the repository root using the installed .NET 10 SDK:

```sh
dotnet run --project tools/simitone/save-tests/EvictTests.csproj -c Release
```

The test selects the actual `TS1GameScreen.EvictLot` method through Roslyn's C#
syntax tree and compiles that method unchanged with synthetic UI/storage
collaborators. The bundled SDK supplies Roslyn; no package installation or game
build is required. `EvictOriginal.txt` preserves the old method as the failing
baseline. Derived frontend source licensing is recorded in
`managed/LICENSE.FreeSO.md`; this test makes no broader frontend licensing claim.

Four cases reproduce the original non-durable move-out, verify that the new
handler persists HouseNumber/budget/ValueInArch before refreshing the selected
house for both positive and zero ValueInArch, and verify IO failure propagation.
The fixture writes and reloads a synthetic state record. It does **not** model
the IFF format or replace the actual UI/save/restart gate.

The production fix adds only `SaveNeighbourhood(false)` after `MoveOut` and
before `SelectHouse`. The provider always writes `MainResource` to the selected
`UserPath/Neighborhood.iff`; `false` skips only dirty character-resource writes,
which are unnecessary for this FAMI-only operation. The refund formula is
unchanged. Like current lot Save, IO failures propagate; memory may already be
changed when persistence fails. Atomicity and rollback remain separate work.

Frontend patch `0006-save-and-discard.patch` retains its previous hunks and adds
the eviction call. Replay on Simitone
`e36e83f95d44b9f4b378e7665c4e2ecbaa88b4ab` after frontend patches `0001` and
`0002` matches all three touched files exactly. Hashes, replay commands and
test provenance are in
`reports/simitone-linux-evaluation/evict-persistence/report.json`.

This fix does not change valuation, character resources, old house FSOV contents,
family deletion or stale former-resident avatars. Existing profiles are not
repaired or modified by the test.
