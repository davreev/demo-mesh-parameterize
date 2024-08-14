# Demo: Mesh Parameterize

![](https://github.com/davreev/demo-mesh-parameterize/actions/workflows/build.yml/badge.svg)

Source code for https://github.com/davreev/demo-mesh-parameterize

## Build

This project can be built to run natively or in a web browser. Build instructions vary slightly
between the two targets.

### Native Build

Build via `cmake`

> ⚠️ Currently only tested with Clang and GCC. MSVC is not supported.

```sh
mkdir build
cmake -S . -B ./build -G <generator>
cmake --build ./build [--config <config>]
```

### Web Build

Download the [Emscripten SDK](https://github.com/emscripten-core/emsdk) and dot source the
appropriate setup script

```sh
# Bash
EMSDK_DIR="absolute/path/of/emsdk/root"
. ./emsc-setup.sh

# Powershell
$EMSDK_DIR="absolute/path/of/emsdk/root"
. ./emsc-setup.ps1
```

Then build via `emcmake`

```sh
mkdir build 
emcmake cmake -S . -B ./build -DCMAKE_BUILD_TYPE=<config>
cmake --build ./build
```

Output can be served locally for testing e.g.

```sh
python -m http.server
```

### Dependencies

The following build-time dependencies are expected to be installed locally:

- `glslangValidator` (>= 12.2.*)
- `spirv-cross` ( >= 2021.01.15)

Remaining dependencies are fetched during CMake's configure step. See `cmake/deps` for a complete
list.
