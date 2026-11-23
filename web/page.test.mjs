// The page itself, in a real browser:  node web/page.test.mjs [--shots DIR]
//
// Serves docs/ on a local port, opens it in Chromium (Playwright) and uses
// the calculator the way a visitor would: typing, the history, completion,
// the examples, a reload to see that words and settings were kept, a
// dropped file, a phone-sized window. Exit status 1 if a check fails; with
// no Playwright installed it says so and exits 0, so that `make web-test`
// works on a machine without a browser to drive.
import http from 'http';
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';
import { createRequire } from 'module';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const docs = path.join(root, 'docs');
const require = createRequire(import.meta.url);
let chromium;
try { ({ chromium } = require('playwright')); } catch (e) {
  console.log('page (web): skipped, Playwright is not installed');
  process.exit(process.env.MM41_REQUIRE_BROWSER ? 1 : 0);          // CI sets this: there, no browser is a failure
}

const shotsAt = process.argv.indexOf('--shots');
const shots = shotsAt > 0 ? process.argv[shotsAt + 1] : null;
if (shots) fs.mkdirSync(shots, { recursive: true });

const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript', '.css': 'text/css', '.wasm': 'application/wasm',
                '.woff2': 'font/woff2', '.pdf': 'application/pdf', '.txt': 'text/plain' };
const server = http.createServer((req, res) => {
  const name = decodeURIComponent(new URL(req.url, 'http://x').pathname);
  const file = path.join(docs, name === '/' ? 'index.html' : name);
  if (!file.startsWith(docs) || !fs.existsSync(file) || !fs.statSync(file).isFile()) { res.writeHead(404); res.end('not found'); return; }
  res.writeHead(200, { 'Content-Type': TYPES[path.extname(file)] || 'application/octet-stream' });
  fs.createReadStream(file).pipe(res);
});
await new Promise(r => server.listen(0, '127.0.0.1', r));
const url = `http://127.0.0.1:${server.address().port}/`;

let bad = 0, count = 0;
const check = (what, ok, detail = '') => { count++; if (!ok) { bad++; console.log(`FAIL: page: ${what} ${String(detail).slice(0, 300)}`); } };

const browser = await chromium.launch();
const context = await browser.newContext({ viewport: { width: 1280, height: 800 }, deviceScaleFactor: shots ? 2 : 1 });
const page = await context.newPage();
const problems = [];
page.on('console', m => { if (m.type() === 'error') problems.push(m.text()); });
page.on('pageerror', e => problems.push(String(e)));

const ready = () => page.waitForFunction(() => !document.getElementById('line').disabled, null, { timeout: 30000 });
const screenText = () => page.evaluate(() => document.getElementById('screen').innerText);
async function type(line) {
  await ready();
  const before = await page.evaluate(() => document.getElementById('screen').childNodes.length);
  await page.fill('#line', line);
  await page.press('#line', 'Enter');
  await page.waitForFunction(n => document.getElementById('screen').childNodes.length > n && !document.getElementById('line').disabled, before, { timeout: 30000 });
  await page.waitForTimeout(60);
}

await page.goto(url);
await ready();
await page.waitForFunction(() => document.getElementById('screen').innerText.includes('USER  RAD'), null, { timeout: 10000 }).catch(() => {});
check('the splash screen is on the screen', (await screenText()).includes('USER  RAD'));
check('the version is shown', /^\d+\.\d+/.test(await page.textContent('#version')), await page.textContent('#version'));
check('the prompt', (await page.textContent('#prompt')) === 'MM-41>>', await page.textContent('#prompt'));
check('the line has the focus', await page.evaluate(() => document.activeElement.id === 'line'));

await type('2 3 +');
check('2 3 + leaves 5', /\[0\] ℝ : 5\.000000/.test(await screenText()), (await screenText()).slice(-200));
await type('nosuchword');
check('an error is marked as one', await page.evaluate(() => [...document.querySelectorAll('#screen .err')].some(e => e.textContent.includes('Unknown word'))));

await page.press('#line', 'ArrowUp');
check('the up arrow recalls the last line', (await page.inputValue('#line')) === 'nosuchword', await page.inputValue('#line'));
await page.press('#line', 'ArrowUp');
check('and the one before it', (await page.inputValue('#line')) === '2 3 +', await page.inputValue('#line'));
await page.press('#line', 'ArrowDown');
await page.press('#line', 'ArrowDown');
check('the down arrow returns to the empty line', (await page.inputValue('#line')) === '', await page.inputValue('#line'));

await page.fill('#line', '65 lifem');
await page.press('#line', 'Tab');
check('Tab leaves a word alone when its completions already part', (await page.inputValue('#line')) === '65 lifem', await page.inputValue('#line'));
await page.fill('#line', '65 lifemea');
await page.press('#line', 'Tab');
check('Tab completes a command', (await page.inputValue('#line')) === '65 lifemean ', await page.inputValue('#line'));
await page.fill('#line', 'usetax s');
await page.press('#line', 'Tab');
check('Tab stops where the candidates part', (await page.inputValue('#line')) === 'usetax si', await page.inputValue('#line'));
await page.press('#line', 'Tab');
await page.press('#line', 'Tab');
await page.waitForTimeout(100);
check('a second Tab lists them', (await screenText()).includes('si-2026'), (await screenText()).slice(-200));
await page.fill('#line', '');

await type('clst');
await page.click('.ex:has-text("usetax 2000 payslip")');
await ready();
await page.waitForTimeout(100);
check('an example runs when clicked: the Slovenian payslip', (await screenText()).includes('1310.30'), (await screenText()).slice(-300));
await type('0 0.1 6.3 rrange dup sin plot');
check('a plot is drawn in colour', await page.evaluate(() => [...document.querySelectorAll('#screen span')].some(s => s.style.color && /[▀▄▌▐▖▗▘▝▚▞]/.test(s.textContent))));
await type('help');
check('help prints bold headings', await page.evaluate(() => document.querySelectorAll('#screen .bold').length > 0));
if (shots) await page.screenshot({ path: path.join(shots, 'desktop.png') });

// what a reload keeps: a word, a register, the tax schedule
await type('clst');
await type(': sq dup * ;');
await type('savewords');
await type('42 7 sto saveregs');
await type('"si" usetax');
await page.waitForTimeout(900);                       // the page writes to storage a moment after a line
await page.reload();
await ready();
await type('7 sq');
check('a saved word survives a reload', /\[0\] ℝ : 49\.000000/.test(await screenText()), (await screenText()).slice(-200));
await type('clst loadregs 7 rcl');
check('a saved register survives a reload', /42\.000000/.test((await screenText()).slice(-200)), (await screenText()).slice(-200));
await type('clst 2000 net');
check('the tax schedule is remembered', (await screenText()).slice(-200).includes('1310.30'), (await screenText()).slice(-200));
await page.press('#line', 'ArrowUp');
check('the history survives a reload', (await page.inputValue('#line')) === 'clst 2000 net', await page.inputValue('#line'));
await page.fill('#line', '');

// a file given to the calculator
await page.setInputFiles('#file', { name: 'table.txt', mimeType: 'text/plain', buffer: Buffer.from('1 2 3\n4 5 6\n') });
await page.waitForFunction(() => document.querySelectorAll('#files li').length > 0, null, { timeout: 10000 });
await type('clst "table.txt" readmat');
await type('pm');
check('an added file can be read as a matrix', /\|\s*4\S*\s+5\S*\s+6\S*\s*\|/.test((await screenText()).slice(-300)), (await screenText()).slice(-300));

// the tutor, and q
await type('tutor 1');
check('the tutor changes the prompt', (await page.textContent('#prompt')).startsWith('tutor'), await page.textContent('#prompt'));
await type('q');
check('q leaves the tutor', (await page.textContent('#prompt')) === 'MM-41>>', await page.textContent('#prompt'));
await type('q');
await ready();
await type('1 1 +');
check('q ends the session and a new one starts', (await screenText()).includes('A new one is starting') && /\[0\] ℝ : 2\.000000/.test((await screenText()).slice(-200)));

await type('manual');
check('manual points to the PDF', await page.evaluate(() => !!document.querySelector('#screen .note a[href="mm41-manual.pdf"]')));
await type('manual survival');
check('manual <chapter> prints it', (await screenText()).includes('SURVIVAL ANALYSIS'));

// a phone
await page.setViewportSize({ width: 390, height: 780 });
await page.reload();
await ready();
check('on a narrow screen the examples are tucked away', await page.evaluate(() => getComputedStyle(document.getElementById('side')).display === 'none'));
check('and the page does not scroll sideways', await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth));
await type('2 3 *');
if (shots) await page.screenshot({ path: path.join(shots, 'phone.png') });
await page.click('#toggle-side');
check('the button brings them back', await page.evaluate(() => getComputedStyle(document.getElementById('side')).display !== 'none'));
if (shots) await page.screenshot({ path: path.join(shots, 'phone-examples.png') });

check('nothing was reported in the console', problems.length === 0, problems.join(' | '));
await browser.close();
server.close();
console.log(`page (web): ${bad ? bad + ' of ' + count + ' failed' : count + ' checks ok'}`);
process.exit(bad ? 1 : 0);
