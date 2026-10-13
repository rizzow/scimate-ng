# Milestone 1 validation

Run locally on 2026-10-13 on Apple Silicon macOS (Darwin arm64), with
Apple Clang 21.0.0 and CMake 4.4.3:

| Check | Result |
| --- | --- |
| Native Debug configure and build | Passed |
| CTest parser and CLI suites | 2/2 passed |
| Core-only build, CLI and testing disabled | Passed |
| AddressSanitizer + UndefinedBehaviorSanitizer build and CTest | 2/2 passed |
| Local SCI0 template map comparison | All 60 rows matched |
| Windows and Linux builds | CI configured; not run locally or verified remotely |

Parser tests cover known byte sequences, all type bit patterns, minimum and
maximum field values, duplicates and ordering, a terminator-only map, every
truncation point in a valid map, absent/malformed terminators, and trailing data.
Assertions remain active in Release builds. CLI tests generate synthetic files
in the build directory and check exact tab-separated output, empty maps,
malformed/missing files, explicit format selection, usage errors, and help.
No test requires Qt, downloads, Python, or the legacy repository.

Sanitizer run:

```sh
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'
cmake --build build-sanitize
ctest --test-dir build-sanitize --output-on-failure
```

The additional local integration input was
`../SCICompanion/SCICompanion/Files/TemplateGame/SCI0/resource.map` at the
reference revision recorded in [provenance](provenance.md). Its 366 bytes
contain 60 records and a terminator. All CLI rows were compared to a separate
Python `struct.iter_unpack('<HI', raw[:-6])` decoder with the documented masks.
All entries refer to volume 1. First row: `script 0 1 0`; last row:
`cursor 997 1 110010`.

To inspect that local fixture again:

```sh
./build/scimate list --format sci0 \
  ../SCICompanion/SCICompanion/Files/TemplateGame/SCI0/resource.map
```

This checks map metadata only, not volume contents or Windows editor behavior.
CI uses synthetic fixtures on macOS, Ubuntu, and Windows. Platform support and
minimum OS versions remain unverified until those builds have run.
