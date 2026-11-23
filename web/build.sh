#!/bin/sh
# Build MM-41 for the browser: docs/mm41.js and docs/mm41.wasm.
#
#   make web                 (which calls this with the generated sources)
#
# Needs Emscripten (emcc, emconfigure, emmake). The one library MM-41 needs
# that Emscripten does not bring is GSL, which is compiled here the first
# time, from its source, into build/web/gsl (a few minutes). The source is
# taken from GSL_TARBALL if that is set, else downloaded from a GNU mirror
# and checked against the sum below. Readline is not needed: the page reads
# the lines (web/shim). decNumber is in vendor/.
#
# Arguments: the generated C files and the version header of a native build
#   web/build.sh MODULE_TABLE TUTOR_TEXT TAX_TEXT GIT_HEADER
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
OUT=docs
WORK=build/web
GSL_VERSION=2.7.1
GSL_SHA256=dcb0fbd43048832b757ff9942691a8dd70026d5da0ff85601e52687f6deeb34b
GSL_URL=https://ftpmirror.gnu.org/gsl/gsl-$GSL_VERSION.tar.gz
JOBS=${JOBS:-4}

[ $# -eq 4 ] || { echo "usage: web/build.sh MODULE_TABLE TUTOR_TEXT TAX_TEXT GIT_HEADER (use: make web)" >&2; exit 2; }
MODULE_TABLE=$1; TUTOR_TEXT=$2; TAX_TEXT=$3; GIT_HEADER=$4
# The Emscripten packaged by Debian and Ubuntu keeps the JavaScript tools it
# needs outside node's search path, and a cache of system libraries that only
# root can add to: point it at a cache of our own. (An emsdk needs neither.)
if [ -d /usr/share/emscripten ]; then
  [ -d /usr/share/nodejs ] && export NODE_PATH="${NODE_PATH:+$NODE_PATH:}/usr/share/nodejs"
  export EM_CACHE="${EM_CACHE:-$HOME/.cache/emscripten}" EM_FROZEN_CACHE="${EM_FROZEN_CACHE:-}"
fi
command -v emcc >/dev/null 2>&1 || { echo "web/build.sh: emcc not found; install Emscripten (https://emscripten.org) and activate it" >&2; exit 1; }

mkdir -p "$WORK" "$OUT"

# ---- GSL, once ------------------------------------------------------------------
GSL=$ROOT/$WORK/gsl
if [ ! -f "$GSL/lib/libgsl.a" ]; then
  echo "== building GSL $GSL_VERSION for WebAssembly (once)"
  SRC=$WORK/gsl-src
  rm -rf "$SRC"; mkdir -p "$SRC"
  if [ -n "${GSL_TARBALL:-}" ]; then
    TARBALL=$GSL_TARBALL
  else
    TARBALL=$WORK/gsl-$GSL_VERSION.tar.gz
    [ -f "$TARBALL" ] || curl -fsSL -o "$TARBALL" "$GSL_URL"
    echo "$GSL_SHA256  $TARBALL" | sha256sum -c - >/dev/null 2>&1 \
      || { echo "web/build.sh: $TARBALL has the checksum $(sha256sum "$TARBALL" | cut -d' ' -f1), not the expected $GSL_SHA256" >&2; exit 1; }
  fi
  tar -xzf "$TARBALL" -C "$SRC" --strip-components=1
  (
    cd "$SRC"
    # GSL's configure does not know a WebAssembly host. A 32-bit Linux one
    # stands in for it: the same word size, byte order and IEEE doubles --
    # but without the x87's extended-precision registers, which configure
    # assumes for i686 and which is undone in config.h.
    emconfigure ./configure --host=i686-unknown-linux-gnu --disable-shared --enable-static CFLAGS="-O2" > configure.log 2>&1 \
      || { tail -20 configure.log >&2; exit 1; }
    sed -i.bak 's|^#define HAVE_EXTENDED_PRECISION_REGISTERS 1|/* #undef HAVE_EXTENDED_PRECISION_REGISTERS */|' config.h
    grep -q '^/\* #undef HAVE_EXTENDED_PRECISION_REGISTERS \*/' config.h || { echo "web/build.sh: config.h was not as expected" >&2; exit 1; }
    # every directory but doc, then the library made of them
    SUB=$(sed -n 's/^SUBDIRS = //p' Makefile | tr ' ' '\n' | grep -v '^doc$' | tr '\n' ' ')
    emmake make -j"$JOBS" SUBDIRS="$SUB" > make.log 2>&1 || { tail -30 make.log >&2; exit 1; }
  )
  mkdir -p "$GSL/lib" "$GSL/include/gsl"
  cp "$SRC/.libs/libgsl.a" "$SRC/cblas/.libs/libgslcblas.a" "$GSL/lib/"
  cp -L "$SRC"/gsl/*.h "$GSL/include/gsl/"
  rm -rf "$SRC"
fi

# ---- MM-41 ------------------------------------------------------------------------
echo "== compiling MM-41 to $OUT/mm41.js, $OUT/mm41.wasm"
INC="-Iweb/shim -Iinclude -Ivendor/decNumber -I$GSL/include"
for d in modules/*/; do INC="$INC -I${d%/}"; done
SRCS="$(ls src/*.c modules/*/*.c) vendor/decNumber/decNumber.c vendor/decNumber/decContext.c $MODULE_TABLE $TUTOR_TEXT $TAX_TEXT web/mm41_web.c web/shim/readline.c web/shim/catanh.c"

# main() is compiled under the name mm41_main and reached through
# mm41_web_main (web/mm41_web.c): the page never runs it, and not every
# Emscripten links a main that nothing calls at start.
#
# What the page and the tests find in the calculator's file system: the
# manual, where `manual` looks for it, and the files `make install` would
# have put in the configuration directory, which the page copies there.
emcc -O2 -std=c17 -Wall -Wextra -Wno-overlength-strings $INC \
  -include "$GIT_HEADER" -DMM41_DOCDIR='"/doc"' -DHAVE_STDBOOL_H=1 -DHAVE_STDINT_H=1 -Dmain=mm41_main \
  $SRCS "$GSL/lib/libgsl.a" "$GSL/lib/libgslcblas.a" \
  -s MODULARIZE=1 -s EXPORT_NAME=MM41 \
  -s ALLOW_MEMORY_GROWTH=1 -s INITIAL_MEMORY=67108864 -s TOTAL_STACK=8388608 \
  -s INVOKE_RUN=0 -s EXIT_RUNTIME=0 -s FORCE_FILESYSTEM=1 \
  -s EXPORTED_FUNCTIONS='["_malloc","_free"]' \
  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","FS","IDBFS","UTF8ToString"]' \
  -lidbfs.js \
  --embed-file doc/mm41-manual.txt@/doc/mm41-manual.txt \
  --embed-file data/modules@/defaults/modules \
  --embed-file data/holidays@/defaults/holidays \
  -o "$OUT/mm41.js"

# ---- what the page needs besides: the manual, and which build this is ----------------
cp doc/mm41-manual.pdf "$OUT/mm41-manual.pdf"
VERSION=$(sed -n 's/.*MM41_VERSION "\(.*\)".*/\1/p' include/globals.h)
# "id" names this very mm41.wasm: the page adds it to the address it fetches
# the file from, so that a browser never pairs a new page with an old copy
ID=$(sha256sum "$OUT/mm41.wasm" | cut -c1-12)
printf 'window.MM41_BUILD = { version: "%s", id: "%s" };\n' "$VERSION" "$ID" > "$OUT/build.js"
ls -l "$OUT/mm41.js" "$OUT/mm41.wasm"
