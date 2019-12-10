echo "ENABLE_WASM=1" > sdks/Make.config
echo "ENABLE_WINDOWS=1" >> sdks/Make.config
echo "ENABLE_WASM_DYNAMIC_RUNTIME=1" >> sdks/Make.config
echo "ENABLE_WASM_THREADS=1" >> sdks/Make.config

make -C sdks/builds provision-wasm

make -j -C sdks/builds configure-wasm NINJA=
make -j -C sdks/builds build-wasm     NINJA=
make -j -C sdks/builds archive-wasm   NINJA=

make -j -C sdks/wasm build
make -C sdks/wasm package