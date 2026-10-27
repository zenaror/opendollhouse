# Native family checkpoints

The C++ household runtime now connects managed family occupancy, live finances
and immutable session archives. This is a prerequisite for the full
create-family → buy → play → move out → buy elsewhere lifecycle. The subsequent
[portable transfer profile](native-family-transfer.md) enables verified managed
move-out and fresh-resident destinations; general relocation remains incomplete.

## Commit and restore rules

New purchases use chronological overlay v6 operations. Family identity refers
to the immutable source/creation record; an occupancy epoch increases on every
move-in. A checkpoint records the current house, epoch, budget and SHA-256 of
the exact session archive. Prior house/epoch/budget/architecture values must
match before an operation can replay. Legacy v1–v5 overlays remain readable.

`FamilySessionJournal` pins the complete expected overlay and the effective
family roster. The runtime uses that same snapshot instead of independently
adopting a newer overlay during preparation. Required `FAML` v1 binds a whole
household session to neighborhood identity, family identity, occupancy epoch
and family chunk ID. It requires the existing household and generic execution
profiles; a managed archive cannot silently become a legacy session.

All existing live-save requests converge on the same path. The runtime first
checks the complete restore profile, then publishes private immutable archive
bytes, flushes them, and commits the overlay with compare-and-exchange. The
whole overlay comparison rejects a stale process even within the same
occupancy. Identical saves reuse the archive without adding checkpoint records,
but still perform the comparison under the writer lock. There is no automatic
rebase after a conflict. An archive contains no parent-checkpoint digest, which
would make unchanged saves change their own digest.

Archives live next to the private overlay under
`player-state/sessions/<neighborhood-sha>/<archive-sha>.odhs`. Publication uses
exclusive creation and a no-replace hard link; existing bytes must match the
name and have private ownership/permissions. Published archives survive later
failures. If overlay replacement reports a failure after publication, an exact
reread can establish the logical commit while reporting that disk durability
was not confirmed. Unreferenced archives remain available for recovery.

On every residential visit, the launcher reloads player state and resolves the
managed archive from its authoritative checkpoint. Per-house cached paths are
only hints. Explicit archive loads and rollback paths must still match the
current checkpoint. Restore checks the exact archive SHA, lease, house, family,
complete roster, resource identity and committed budget before applying state.
A missing or damaged current archive stops launch instead of starting a fresh
lot. A historical checkpoint from another occupancy requires portable Sim
transfer; playing or saving fresh CAS defaults over that ticket is rejected.

## Finances and remaining family transfer

Pinned Simitone client `e36e83f95d44b9f4b378e7665c4e2ecbaa88b4ab`
(`Client/Simitone/Simitone.Client/UI/Screens/TS1GameScreen.cs`, `EvictLot`) refunds retained `ValueInArch` and
clears it. Its FreeSO revision `7e76c1960252ddd033d8937d5edb47d326f5b69c`
subtracts SIMI purchase value on buy. Newly created families start with
`ValueInArch = 0`; source inspection found no live assignment updating that
field. Therefore its verified baseline refund for such families is zero.
This does not establish original TS1 liquidation behavior or justify refunding
the purchase price. Native checkpoints preserve the live budget and retain the
current architecture field; they do not invent a liquidation formula.

The overlay remains bounded to 4 MiB and 4096 total records. Changed checkpoints
consume records; identical saves do not. Exhaustion rejects a new commit while
preserving the previous checkpoint and retaining the unpublished-head archive
for recovery. Future compaction must preserve occupancy and checkpoint authority.

Managed move-out now requires a current checkpoint and a compatible portable
profile. The [transfer contract](native-family-transfer.md) covers reviewed
personal data, needs and neighbor relationships into freshly initialized
residents. Original installed residents can be retired using the exact
[expired source-occupancy proof](native-occupancy-reconstruction.md); unproven
actors, unsupported held objects and incompatible unknown progress remain
guarded. Restoring the old house's World/positions/VMs into another lot
would be incorrect. Editing/renaming families, general liquidation, original UI
and expansion travel remain separate unfinished requirements.

## Verification

an archived validation record (not distributed) records ten passing checks covering
the codecs, private stores, journal and native CLI. The new CLI case exercises
a synthetic managed occupancy through first save, a new process, unchanged
save, continued play, stale-save rejection, copied current bytes, missing
authority, complete-household enforcement and House07 → House08 → House07.
The source fixture files remain unchanged. The journal unit case rejects an
internally valid archive belonging to another family and unconsumed historical
carry tickets. The overlay store also reopens a valid 225434-byte v6 history
above its former host limit. These checks do not prove UI rehome or original
game parity.

an archived validation record (not distributed) adds six completed native
processes using the external Complete Collection data. House07 runs for two
ticks, reopens unchanged, then continues to four ticks; its complete archive
and all 13 sections equal a continuous four-tick control. Loading the earlier
checkpoint returns exit 2 without changing the current archive or overlay.
House07 → House05 → House07 resolves the current checkpoint after the prior
save. Thirty protected original files have matching before/after hashes, and
all temporary profiles were removed; no display was created.

This real-data gate explicitly seeds private managed authority for an existing
family. Its test setup refunds the retained source architecture field and
re-enters at a zero purchase price, with epoch 2. It does not exercise family creation,
the purchase UI, a real property transaction or portable Sim carryover, and it
does not establish that those test finances match original TS1 behavior.
