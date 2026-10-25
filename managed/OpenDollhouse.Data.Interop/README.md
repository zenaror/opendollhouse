# Native data bridge

This `netstandard2.0`, C# 7.3 library calls ABI 1 of the native `odh_data` library.
The host must supply a runtime that supports Cdecl native interop. This target
framework does not establish execution support on a particular console runtime.
There is no managed parser fallback when the library, entry point or ABI is missing.

Pass `-p:ODHNativeDataLibrary=/absolute/path/to/libodh_data.so` when building or
publishing a referencing application. On macOS use `libodh_data.dylib`; on Windows
use `odh_data.dll`. The supplied filename must be the loader basename, including
when it is a symlink to a versioned library. No platform path is selected by default.
The SDK copies this explicit asset into referencing build and publish outputs.
Use a library built for the destination OS and architecture.

`BconDecoder.Decode(byte[])` reports trailing-byte count and returns at most 255
unsigned constants. Input bytes are little endian; the C ABI returns typed native
`uint16_t` values. The call pins managed storage only for the duration of the call.

`BconDecoder.Read(Stream)` consumes exactly two header bytes plus twice the declared
count. It supports partial reads and nonseekable streams, does not consume a trailer
and leaves the stream open. Truncation throws `EndOfStreamException`. The FSO adapter
assigns fields only after successful decoding: on malformed input it does not
reproduce the old reader's partial `Flags`/`Constants` mutation. Valid output,
consumption, stream lifetime and truncation exception type are tested against the
actual old BCON reader and its original `IoBuffer` implementation.

Run the synthetic differential and ABI tests with:

```sh
dotnet run --project managed/OpenDollhouse.Data.Interop.Tests -c Release \
  -p:ODHNativeDataLibrary=/absolute/path/to/libodh_data.so
```

The test project links the actual adapted BCON and IO source. `OriginalBCON.cs`
preserves the pre-adapter reader/writer, with only its namespace changed and its
original SHA-256 recorded. Only unrelated IFF container types are stubbed. FreeSO
source licensing is recorded in [LICENSE.FreeSO.md](../LICENSE.FreeSO.md).
