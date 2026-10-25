# BCON native data ABI

This standalone library exposes the existing `odh::read_bcon` reader through a
small C ABI. It compiles `src/formats.cpp` unchanged. The caller owns the input
and output buffers; the wrapper retains no pointers. A borrowed seekable stream
lets `odh::View` read the supplied memory without copying the packet.

`odh_data_abi_version()` returns `1`. `odh_bcon_decode_v1()` returns status `0`
for success, `1` for invalid arguments, `2` for invalid BCON data, `3` when the
word buffer is too small, and `4` for an internal exception. All three metadata
outputs are required. They are zeroed on entry where pointers are available.
For valid but undersized output, the function returns the required word count,
raw flags, and trailing byte count without changing `words`.

Build and test on a desktop host:

```sh
cmake -S native/data -B reports/portability/native-data-abi/host-build -DCMAKE_BUILD_TYPE=Release
cmake --build reports/portability/native-data-abi/host-build
ctest --test-dir reports/portability/native-data-abi/host-build --output-on-failure
```

The shared library is `odh_data` (`.so`, `.dylib`, or `.dll` according to the
host). Its public header contains no C++ types. `ODH_DATA_STATIC` disables
shared-library annotations when the same wrapper is compiled into a static
target such as the 3DS probe. This is a data-reader boundary; it does not
establish a complete game runtime on any target.
