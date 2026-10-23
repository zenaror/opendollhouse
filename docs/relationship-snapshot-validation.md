# Relationship runtime snapshots

`RelationshipState::snapshot()` copies the complete mutable matrix into owned
values. It preserves neighbor and object source keys, object generations,
empty sources and pairs, raw signed target IDs, all values beyond index 255,
configured limits, and usage counters.

`restore_snapshot()` accepts only a fresh `RelationshipState`. It validates
matrix kinds, source/pair uniqueness, identity fields, targets, counters and
budgets, then builds a candidate map before publishing it. Allocation failures
leave the destination unchanged. Callback bindings and the related `World` are
not part of this snapshot; the host must rebuild those separately.

## Checks

The synthetic test covers snapshot independence, old object generations,
neighbor IDs including zero and negative values, tails longer than 255,
limit rejection, empty snapshots, and allocation failures during capture and
restore.

```sh
cmake --build BUILD_DIR --target odh-relationship-snapshot-tests
ctest --test-dir BUILD_DIR -R '^relationship_snapshot$' --output-on-failure
```

This is an in-memory component snapshot only. It does not establish complete
session saving or restart behavior.
