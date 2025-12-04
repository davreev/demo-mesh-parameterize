# Demo: Mesh Parameterize

![](https://github.com/davreev/demo-mesh-parameterize/actions/workflows/build.yml/badge.svg)

![](https://spatialslur.com/demos/mesh-parameterize-400.png)

Demo and reference implementations of mesh parameterization methods [^1] [^2].

Try it here: https://davreev.gitlab.io/demos/mesh-parameterize/

[^1]: [Least squares conformal maps](https://www.cs.jhu.edu/~misha/Fall09/Levy02.pdf)  
[^2]: [Spectral conformal parameterization](https://hal.inria.fr/inria-00334477/document)

## Build

This project can be built to run natively or in a web browser.

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
the provided script to initialize the Emscripten toolchain

```sh
# Bash
EMSDK_DIR="absolute/path/of/emsdk/root"
. ./emsc-init.sh

# Powershell
$EMSDK_DIR="absolute/path/of/emsdk/root"
. ./emsc-init.ps1
```

Then build via `emcmake`

```sh
mkdir build 
emcmake cmake -S . -B ./build -G <generator>
cmake --build ./build [--config <config>]
```

Output can be served locally for testing e.g.

```sh
python -m http.server
```

### Dependencies

The following build-time dependencies are expected to be installed locally:

- `slangc` (>= 2025.5.0)
- `spirv-cross` ( >= 2021.01.15)
- `spirv-tools` ( >= 2022.1)

Remaining build-time dependencies are fetched during CMake's configure step (see `cmake/deps`).
