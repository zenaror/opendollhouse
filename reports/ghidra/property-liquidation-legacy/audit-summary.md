# Legacy executable property-liquidation static audit

The supplied Legacy Collection Sims.exe was imported into a new private Ghidra
project for this audit. It was not executed, patched or unpacked. No older
Ghidra project or reference checkout was opened or changed.

- SHA256 before and after: b7c8765ed2383cbda8c047694d54a21d97895c8dae57dcc87716ca6e78b342e3.
- PE machine: 0x014c; Ghidra language x86:LE:32:default; image base 0x00400000.
- Entry RVA: 0x006d4310; entry VA: 0x00ad4310, in .bind rather than .text.
- ASCII `evict lot`: VA 0x0098f914.
- ASCII `UI_Nhood_bdoze_evict`: VA 0x0098817c.
- Neither target had a discovered Ghidra reference or a direct little-endian
  32-bit address occurrence in the supplied file. No caller could be recovered
  through those strings.
- The .text raw bytes have approximately 8 bits/byte entropy. Together with
  the .bind entry, this is consistent with protected/encrypted code. This is
  an inference, not a recovered protection specification.

Ghidra completed the bounded import and analysis, but that success is not
semantic evidence about eviction. No original sell/filter/refund behavior,
StaysAfterEvict bit test or ValueInArch update was established. Strings alone
cannot resolve whether furnishings are liquidated or retained.

Analysis used Ghidra 12.1.3, max CPU 2, heap 2 GiB, and a per-file analysis limit
of 240 seconds. This import completed without a timeout. The new project,
script, analysis output and metadata are retained here; no decompilation body
or executable payload is intended for a source commit. metadata.json records
section positions and measured entropy.

The Java user.home and Ghidra settings were redirected into this directory.
Ghidra nevertheless logged its packed database cache at
an isolated temporary analysis cache. No follow-up action modified or cleaned
that shared cache. Logs retain the exact footprint observation.

The original-rule question remains open. Useful next evidence would be a
separately authorized readable code image or original-game before/after saves
for controlled eviction cases. This audit does not justify changing native
liquidation semantics.
