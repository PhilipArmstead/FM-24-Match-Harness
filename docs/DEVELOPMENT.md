# Development

## Prerequisites

Build on Windows 10 or newer using an MSYS2 Bash shell. Install:

- Git and MSYS2's 64-bit MinGW-w64 GCC toolchain.
- Native Windows CMake 3.20 or newer and Ninja, not the MSYS versions.
- For contributions: clang-format 22 or newer and MinGW-w64 clang-tidy.

Install the MSYS2 packages with:

```sh
pacman -S --needed git mingw-w64-x86_64-gcc \
  mingw-w64-x86_64-clang-tools-extra
```

The scripts default to `C:/msys64/mingw64` and the CMake/Ninja bundled with
Visual Studio Build Tools. See [`scripts/env.sh`](../scripts/env.sh) for
paths. Override `CMAKE`, `NINJA`, `CLANG_FORMAT` or `CLANG_TIDY` environment
variables if your tools are installed elsewhere. If GCC is elsewhere, also
set `MINGW_ROOT` in [`cmake/mingw64.cmake`](../cmake/mingw64.cmake) and
override `MINGW`.

## Clone and build

Run these commands in MSYS2 Bash:

```sh
git clone https://github.com/PhilipArmstead/FM-24-Match-Harness.git
cd FM-24-Match-Harness
scripts/build.sh
```

This builds the C17 project in Release mode, including regression tests.
Build files are in `build/`; stripped executables are in `bin/`:
`fm24ctl.exe` (with the harness DLL embedded) and `fm24_tests.exe`.
Close any running copies of these executables before rebuilding.

## Contribution guidelines

- Keep changes focused and add regression tests for changed behaviour.
- Use C17 and existing C/Windows APIs; avoid unnecessary dependencies.
- Follow `.clang-format`, `.clang-tidy` and `.editorconfig`: tabs for
  indentation, spaces for alignment, a 100-column code limit, attached
  braces and braces around control-flow bodies.
- Use `camelCase` for functions, variables and parameters, `CamelCase` for
  types, and `UPPER_CASE` for macros and enum constants. Follow existing
  `module_function` names for module APIs.
- Include the source file's own header first, then C library headers, then
  other project headers. Keep internal functions `static`.
- Prioritise runtime performance, then binary size. Reuse existing helpers
  and keep comments brief, explaining only non-obvious code.
- Add tests using `TEST(...)` and the assertions in `tests/test.h`, then
  register them in the suite's `register...Tests` function. Assertions do
  not stop execution, so guard failed setup before using its results.
- Keep compiler warnings and clang-tidy checks clean. Update relevant
  documentation when behaviour changes.

Before submitting, build and run:

```sh
scripts/format.sh          # Apply formatting; use --check to verify only
scripts/build.sh
scripts/tidy.sh            # Requires the build's compilation database
bin/fm24_tests.exe
```

To run a single test suite, pass its name, for example:

```sh
bin/fm24_tests.exe fixture_data
```
