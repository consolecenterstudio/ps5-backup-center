# Build 3.2.0 on macOS

The public source includes the frozen application and engine, vendored dependencies and pre-generated package assets. The source export adapts the internal compiler paths to environment variables. A clean build of this public wrapper is still to be validated.

Required tools are LLVM 18, the community PS5 payload SDK, ps5-native-tool, Python 3, zsh, make and Rust 1.100.0 with rust-src. Point SDK to the payload SDK root, LLVM to the LLVM bin directory and PS5_NATIVE_TOOL to the native linker/signer executable. On macOS, preserve the DYLD_LIBRARY_PATH needed by your LLVM installation.

The Rust cross-build uses build-std for FreeBSD 11. In a dedicated Rust 1.100.0 toolchain, apply engine/third_party/kraken-mount/ps5/prepare-rust-std.py with RUSTUP_TOOLCHAIN=1.100.0 before building. It modifies that toolchain's rust-src to adapt the directory-entry ABI. Use an isolated toolchain if other projects use Rust 1.100.0.

Run ./build.sh -j 4. It builds the injected modules, Rust static library, engine and TV app, then assembles out/PPSA99950.zip with the pre-generated assets. Building does not install or launch anything on a console. Official frozen release binaries are available on the GitHub Release.
