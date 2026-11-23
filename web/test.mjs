// Tests of the browser build, run under node:  make web-test
//
// 1. The golden files of the native build (tests/golden/NAME.rpn -> NAME.out)
//    through the WebAssembly calculator in batch mode, each in a fresh
//    instance with a fresh configuration directory. The output must be the
//    same to the character, except for the files listed in EXPECTED below,
//    which do something a browser cannot.
// 2. The session interface the page uses: start, lines, prompt, completion.
//
// Exit status 1 if anything fails.
import { createRequire } from 'module';
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const require = createRequire(import.meta.url);
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const MM41 = require(path.join(root, 'docs', 'mm41.js'));
const wasmBinary = fs.readFileSync(path.join(root, 'docs', 'mm41.wasm'));     // node has no URL to fetch it from

// golden files whose output legitimately differs in the browser: name -> why, and how many lines may differ
const EXPECTED = {
  sharedmod: ['it loads a compiled module, a shared object, which a browser cannot', 40],
};

function copyTree(m, from, to) {
  m.FS.mkdirTree(to);
  for (const e of fs.readdirSync(from, { withFileTypes: true })) {
    const a = path.join(from, e.name), b = to + '/' + e.name;
    if (e.isDirectory()) copyTree(m, a, b);
    else if (e.isFile()) m.FS.writeFile(b, fs.readFileSync(a));
  }
}

// Standard output and error as a pipe would carry them (a terminal, which is what the page gives the
// calculator, gets coloured plots): bytes, made into lines here, in the order they were written.
function pipe(lines) {
  const bytes = [];
  return (c) => {
    if (c === null || c === 10) { lines.push(Buffer.from(bytes).toString('utf8')); bytes.length = 0; }
    else bytes.push(c < 0 ? c + 256 : c);
  };
}

async function instance(lines) {
  const m = await MM41({ wasmBinary, preRun: [(mod) => { const w = pipe(lines); mod.FS.init(null, w, w); }] });
  // the files a test may name, where it names them: relative to the repository
  copyTree(m, path.join(root, 'tests', 'golden'), '/repo/tests/golden');
  copyTree(m, path.join(root, 'data'), '/repo/data');
  copyTree(m, path.join(root, 'examples'), '/repo/examples');
  m.FS.chdir('/repo');
  return m;
}

function exitStatus(e) {
  if (e && typeof e === 'object' && 'status' in e) return e.status;     // exit() inside the program
  throw e;
}

process.setMaxListeners(0);          // every instance of the calculator adds its own
let failed = 0, passed = 0;
const note = [];

// ---- 1. golden files ---------------------------------------------------------------------
const dir = path.join(root, 'tests', 'golden');
const only = process.argv.slice(2);
for (const f of fs.readdirSync(dir).filter(f => f.endsWith('.rpn')).sort()) {
  const name = f.slice(0, -4);
  if (only.length && !only.includes(name)) continue;
  const lines = [];
  const m = await instance(lines);
  let rc;
  try { rc = m.ccall('mm41_web_main', 'number', ['string'], [`-n\n-f\n/repo/tests/golden/${f}`]); }
  catch (e) { rc = exitStatus(e); }
  const got = lines.join('\n') + `\n[exit ${rc}]\n`;
  const want = fs.readFileSync(path.join(dir, name + '.out'), 'utf8');
  if (got === want) { passed++; continue; }
  const g = got.split('\n'), w = want.split('\n');
  let differing = Math.abs(g.length - w.length);
  for (let i = 0; i < Math.min(g.length, w.length); i++) if (g[i] !== w[i]) differing++;
  if (EXPECTED[name] && differing <= EXPECTED[name][1]) {
    passed++;
    note.push(`  ${name}: ${differing} line(s) differ, as expected (${EXPECTED[name][0]})`);
    continue;
  }
  failed++;
  console.log(`FAIL: ${name} (${differing} line(s) differ)`);
  let shown = 0;
  for (let i = 0; i < Math.max(g.length, w.length) && shown < 8; i++)
    if (g[i] !== w[i]) { console.log(`  line ${i + 1}\n    native: ${(w[i] ?? '').slice(0, 150)}\n    web:    ${(g[i] ?? '').slice(0, 150)}`); shown++; }
}
console.log(`golden (web): ${passed} passed, ${failed} failed`);
for (const n of note) console.log(n);

// ---- 2. the session ----------------------------------------------------------------------
if (!only.length) {
  const out = [];
  const m = await MM41({ wasmBinary, print: s => out.push(s), printErr: s => out.push('! ' + s) });
  const line = m.cwrap('mm41_web_line', 'number', ['string']);
  const prompt = () => m.UTF8ToString(m.ccall('mm41_web_prompt', 'number', [], []));
  const complete = (text, cursor = text.length) => {
    const p = m.ccall('mm41_web_complete', 'number', ['string', 'number'], [text, cursor]);
    const s = m.UTF8ToString(p);
    m._free(p);
    const [start, ...names] = s.split('\n');
    return { start: Number(start), names };
  };
  let bad = 0;
  const check = (what, ok, detail = '') => { if (!ok) { bad++; console.log(`FAIL: session: ${what} ${detail}`); } };
  const say = (text) => { out.length = 0; const go = line(text); return { go, text: out.join('\n') }; };

  m.ccall('mm41_web_start', null, [], []);
  check('the splash screen', out.join('\n').includes('USER  RAD'), out.slice(0, 3).join('|'));
  check('the prompt', prompt() === 'MM-41>> ', prompt());
  let r = say('2 3 +');
  check('2 3 + shows 5 on the stack', /5\.0+/.test(r.text), r.text);
  r = say('nosuchword');
  check('an unknown word is an error on stderr', r.text.includes('! Error'), r.text);
  r = say('"si" usetax 2000 net');
  check('the salary words work', r.text.includes('1310.30'), r.text);
  r = say('clst [2 2 $ 4 1 2 3] det');
  check('GSL is there (a determinant)', /10\.0+/.test(r.text), r.text);
  r = say('clst 0.1d 0.2d +');
  check('decimals are exact', r.text.includes('0.3'), r.text);
  r = say('decimal');
  check('the prompt follows the number mode', prompt() === 'MM-41[dec]>> ', prompt());
  say('binary');
  let c = complete('usehol ');
  check('completion after usehol offers the built-in calendars', c.names.includes('us') && c.names.includes('si'), JSON.stringify(c));
  c = complete('1 2 lifem');
  check('completion of a command', c.start === 4 && c.names.includes('lifemean'), JSON.stringify(c));
  c = complete('usetax s');
  check('completion after usetax', c.names.includes('si') && c.names.includes('si-2026'), JSON.stringify(c));
  r = say('tutor 1');
  check('the tutor starts and changes the prompt', prompt().startsWith('tutor'), prompt());
  say('q');
  check('q leaves the tutor, not the session', prompt() === 'MM-41>> ', prompt());
  say('clst');
  say(': sq dup * ;');
  r = say('7 sq');
  check('a word defined at the prompt', /49\.0+/.test(r.text), r.text);
  r = say('clst "1.7.2026" "America/New_York" tz_offset');
  check('time zones come from the browser', /-4\.0+/.test(r.text), r.text);
  r = say('clst 2 atanh');
  check('atanh beyond 1 is on the right side of the cut', r.text.includes('(0.549306, 1.570796i)'), r.text);
  r = say('clst "x.so" load');
  check('a compiled module is refused, not fatal', r.text.includes('! Error'), r.text);
  r = say('q');
  check('q ends the session', r.go === 0);
  console.log(`session (web): ${bad ? bad + ' failed' : 'ok'}`);
  failed += bad;
}
process.exit(failed ? 1 : 0);
