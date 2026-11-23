# MM-41 in the browser

`docs/` is a web page that runs the calculator: the same C program,
compiled to WebAssembly, with the page in the place of the terminal. It is
what <https://micomrkaic.github.io/mm-41/> serves (GitHub Pages, from the
`docs/` folder of `main`).

```
make web         # docs/mm41.js, docs/mm41.wasm (and build.js, the manual)
make web-test    # the golden files through the WebAssembly build; the page in a browser
```

The built files are committed, so the page needs no build to be served;
`make web` is for after a change to the calculator.

## What is where

| | |
|---|---|
| `web/build.sh` | the build: GSL for WebAssembly (once), then MM-41 |
| `web/mm41_web.c` | what the page calls: start a session, run a line, the prompt, completions |
| `web/shim/` | readline's few names without a terminal; `catanh` (see below) |
| `web/test.mjs` | the golden files of the native build, run under node |
| `web/page.test.mjs` | the page in Chromium (Playwright): typing, history, completion, storage |
| `docs/index.html`, `app.js`, `style.css` | the page |
| `docs/mm41.js`, `mm41.wasm`, `build.js`, `mm41-manual.pdf` | built by `make web` |
| `docs/fonts/` | DejaVu Sans Mono, cut down to the characters the calculator prints |

## Building

Emscripten is the only tool needed (`apt install emscripten`, or the
emsdk). The dependencies:

- **GSL** is compiled from its source the first time, into `build/web/gsl`.
  The tarball is downloaded from a GNU mirror and checked against a
  checksum; `GSL_TARBALL=/path/to/gsl-2.7.1.tar.gz make web` uses a copy you
  already have. GSL's configure script does not know a WebAssembly host, so
  it is told the host is 32-bit Linux, which agrees with WebAssembly in
  everything GSL asks about except the x87's extended precision; that one
  answer is corrected in `config.h`.
- **Readline** is not compiled at all. The sources include
  `<readline/readline.h>` as always and find `web/shim/readline/`, which
  declares the ten names MM-41 uses; the page keeps the history and asks the
  calculator's own completion code for candidates.
- **decNumber** is in `vendor/`, as for the native build.

## How the page drives the calculator

`src/main.c` has the interactive session in steps (`include/session.h`):
start, a prompt and a line at a time, end. The native `repl()` is those
steps around readline; the page calls them through `web/mm41_web.c`. What
the calculator prints arrives a line at a time and is shown with its bold,
underline and colours. `main()` itself is compiled too, under the name
`mm41_main`, and is how the tests run the golden files in batch mode.

The configuration directory (`~/.config/mm41` in the calculator's own file
system) is kept in the browser's IndexedDB, so saved words, registers,
`config.txt`, tax schedules written by `taxfile` and files you add survive a
reload. The history of lines is in `localStorage`. Nothing is sent anywhere.

## What differs from the terminal

- `"x.so" load` is an error: nothing can be linked into the page. Word
  modules (`"geo" use`) work.
- `!command` has no shell to run in.
- `manual` alone points to the PDF; `manual finance` and `manual npv` print
  the chapter or the passage.
- `tz_offset` and the words that take a zone name use the browser's time
  zone database (`Intl`) instead of the system's; the answers are the same.
- A long calculation holds the page until it is done.
- `atanh` of a real number beyond 1: Emscripten's C library puts the result
  on the other side of the branch cut than C99 and glibc do
  (`2 atanh` would be `(0.549, -1.571i)`); `web/shim/catanh.c` replaces
  the function so that the browser agrees with the terminal.

Everything else is the same program. `make web-test` checks that: all the
golden files of `tests/golden` must print in the browser build what they
print natively, to the character (one, which loads a compiled module, is
listed as an expected difference).
