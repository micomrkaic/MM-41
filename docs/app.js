/* MM-41 in the browser: the page's side.
 *
 * The calculator is docs/mm41.wasm (the same C program, see web/). This file
 * plays the terminal: it prints what the calculator writes, reads a line,
 * keeps the history, asks the calculator for completions, and keeps the
 * calculator's configuration directory in the browser's storage (IndexedDB)
 * so that words, registers and settings survive a reload.
 */
(function () {
  'use strict';

  var CONFIG = '/home/web_user/.config/mm41';
  var WORK = '/work';
  var HISTORY_KEY = 'mm41.history';
  var MAX_LINES = 6000;

  var EXAMPLES = [
    ['First steps', [
      ['2 3 +', 'two numbers, then the operation'],
      ['dup *', 'the top of the stack, squared'],
      ['1 3 /'],
      ['clst', 'clear the stack'],
      ['help', 'one screen of everything'],
      ['tutor', 'a tour you type along with; q leaves it'],
    ]],
    ['Money, in exact decimals', [
      ['0.1d 0.2d +', 'the suffix d makes a decimal: exactly 0.3'],
      ['clrtvm 360 n 0.5 i% 300000 pv 0 fv ?pmt', 'the payment on a 30-year loan at 6%'],
      ['[6 1 $ -1000 300 300 300 300 300] 10 npv'],
    ]],
    ['Gross and net pay', [
      ['"si" usetax 2000 payslip', 'Slovenia, 2,000 gross a month'],
      ['1500 gross', 'the gross pay for 1,500 net'],
      ['"us" usetax "mfj" filing 2 children 90000 payslip', 'United States, federal'],
      ['"si" usetax 0 children 0 250 8000 rrange dup mtr plot', 'the marginal rate against monthly pay'],
    ]],
    ['Matrices', [
      ['[1 2; 3 4] inv pm', 'pm prints the matrix on top'],
      ['[2 2 $ 4 1 2 3] eig pm'],
      ['[3 3 $ 2 1 1 1 3 2 1 0 0] inv [3 1 $ 4 5 6] * pm', 'a linear system: A inverse times b'],
    ]],
    ['Functions of your own', [
      [': sq dup * ;', 'a new word'],
      ['7 sq'],
      ['0 pi @sin integrate', 'any word can be integrated, solved, plotted'],
      ['-3 3 @sq fplot'],
    ]],
    ['Statistics and graphs', [
      ['42 seed 1000 0 1 normrnd dup mean swap sd'],
      ['1000 0 1 normrnd 20 hplot', 'a histogram of a thousand normal draws'],
      ['0 0.1 6.3 rrange dup sin plot'],
    ]],
    ['Lifetimes and dates', [
      ['"gompertz" [0.0001 0.09] setlaw 65 elife', 'expectation of life at 65'],
      ['"us" usehol "1.1.2026" "31.12.2026" workdays'],
      ['today "Europe/Ljubljana" tz_offset'],
    ]],
  ];

  var $ = function (id) { return document.getElementById(id); };
  var screen = $('screen'), input = $('line'), promptEl = $('prompt'), form = $('entry'), go = $('go');
  var mod = null, lineFn = null, busy = false, persistent = false, syncTimer = 0;
  var history = [], historyAt = 0, draft = '';
  var lastTab = { text: null, at: 0 };

  // ---- the screen -----------------------------------------------------------------------

  var C16 = ['#000000', '#cd3131', '#0dbc79', '#b5890b', '#2472c8', '#bc3fbc', '#11a8cd', '#b0b0b0',
             '#767676', '#f14c4c', '#23d18b', '#d7ba3d', '#3b8eea', '#d670d6', '#29b8db', '#ffffff'];
  function color256(n) {
    if (n < 16) return C16[n];
    if (n >= 232) { var g = 8 + 10 * (n - 232); return 'rgb(' + g + ',' + g + ',' + g + ')'; }
    n -= 16;
    var v = [0, 95, 135, 175, 215, 255];
    return 'rgb(' + v[Math.floor(n / 36)] + ',' + v[Math.floor(n / 6) % 6] + ',' + v[n % 6] + ')';
  }

  /* One line of the calculator's output as an element; the escape sequences
     it uses are bold, underline and the 256 colours of the plots. */
  function render(text, cls) {
    var div = document.createElement('div');
    div.className = 'l' + (cls ? ' ' + cls : '');
    if (text.indexOf('\x1b') < 0) { div.textContent = text; return div; }
    var re = /\x1b\[([0-9;]*)m/g, last = 0, m, state = { bold: false, ul: false, fg: null };
    var put = function (s) {
      if (!s) return;
      if (!state.bold && !state.ul && !state.fg) { div.appendChild(document.createTextNode(s)); return; }
      var span = document.createElement('span');
      span.className = (state.bold ? 'bold ' : '') + (state.ul ? 'ul' : '');
      if (state.fg) span.style.color = state.fg;
      span.textContent = s;
      div.appendChild(span);
    };
    while ((m = re.exec(text))) {
      put(text.slice(last, m.index));
      last = re.lastIndex;
      var p = m[1] === '' ? [0] : m[1].split(';').map(Number);
      for (var i = 0; i < p.length; i++) {
        var c = p[i];
        if (c === 0) state = { bold: false, ul: false, fg: null };
        else if (c === 1) state.bold = true;
        else if (c === 4) state.ul = true;
        else if (c === 22) state.bold = false;
        else if (c === 24) state.ul = false;
        else if (c === 39) state.fg = null;
        else if (c >= 30 && c <= 37) state.fg = C16[c - 30];
        else if (c >= 90 && c <= 97) state.fg = C16[c - 90 + 8];
        else if (c === 38 && p[i + 1] === 5) { state.fg = color256(p[i + 2] || 0); i += 2; }
        else if (c === 48 && p[i + 1] === 5) { i += 2; }
      }
    }
    put(text.slice(last).replace(/\x1b\[[0-9;?]*[A-Za-z]/g, ''));
    return div;
  }

  var pending = null;
  function append(node) {
    if (!pending) {
      pending = document.createDocumentFragment();
      requestAnimationFrame(flush);
    }
    pending.appendChild(node);
  }
  function flush() {
    if (!pending) return;
    screen.appendChild(pending);
    pending = null;
    while (screen.childNodes.length > MAX_LINES) screen.removeChild(screen.firstChild);
    screen.scrollTop = screen.scrollHeight;
  }
  function print(text) { append(render(text, '')); }
  function printErr(text) { append(render(text, /^Warning/.test(text) ? 'warn' : 'err')); }
  function note(html) {
    var div = document.createElement('div');
    div.className = 'l note';
    div.innerHTML = html;
    append(div);
  }
  function echo(promptText, text) {
    var div = document.createElement('div');
    div.className = 'l echo';
    var b = document.createElement('b');
    b.textContent = promptText;
    div.appendChild(b);
    div.appendChild(document.createTextNode(text));
    append(div);
  }
  function escapeHtml(s) { return s.replace(/[&<>"]/g, function (c) { return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]; }); }

  // ---- the calculator ---------------------------------------------------------------------

  function promptText() { return mod.UTF8ToString(mod.ccall('mm41_web_prompt', 'number', [], [])); }
  function showPrompt() { promptEl.textContent = promptText().replace(/\s+$/, ''); }

  function syncDown() {
    return new Promise(function (resolve) {
      try { mod.FS.syncfs(true, function (err) { resolve(!err); }); } catch (e) { resolve(false); }
    });
  }
  function syncUpSoon() {
    if (!persistent) return;
    clearTimeout(syncTimer);
    syncTimer = setTimeout(function () { try { mod.FS.syncfs(false, function () {}); } catch (e) { /* storage is gone: carry on */ } }, 400);
  }

  function exists(path) { try { mod.FS.stat(path); return true; } catch (e) { return false; } }

  /* What `make install` would have put in the configuration directory. */
  function copyDefaults(from, to) {
    if (!exists(from)) return;
    if (!exists(to)) mod.FS.mkdirTree(to);
    mod.FS.readdir(from).forEach(function (name) {
      if (name === '.' || name === '..') return;
      var a = from + '/' + name, b = to + '/' + name;
      if (mod.FS.isDir(mod.FS.stat(a).mode)) copyDefaults(a, b);
      else if (!exists(b)) mod.FS.writeFile(b, mod.FS.readFile(a));
    });
  }

  function boot(first) {
    var build = window.MM41_BUILD || {};
    return MM41({
      print: print,
      printErr: printErr,
      locateFile: function (path) { return path + (build.id ? '?v=' + build.id : ''); },
    }).then(function (m) {
      mod = m;
      lineFn = mod.cwrap('mm41_web_line', 'number', ['string']);
      mod.FS.mkdirTree(CONFIG);
      mod.FS.mkdirTree(WORK);
      try { mod.FS.mount(mod.IDBFS, {}, CONFIG); persistent = true; } catch (e) { persistent = false; }
      return persistent ? syncDown() : false;
    }).then(function (ok) {
      if (persistent && !ok) persistent = false;
      copyDefaults('/defaults', CONFIG);
      mod.FS.chdir(WORK);
      restoreWork();
      mod.ccall('mm41_web_start', null, [], []);
      if (first) {
        note('Type a line and press Enter, or pick one from the examples. <b>help</b> shows everything on one screen, ' +
             '<b>tutor</b> is a tour you type along with, and the <a href="mm41-manual.pdf" target="_blank" rel="noopener">manual</a> has the rest.' +
             (persistent ? '' : ' This browser does not let the page keep anything, so words and registers last until you close it.'));
      }
      syncUpSoon();
      showPrompt();
      listFiles();
      input.disabled = false;
      go.disabled = false;
      input.focus();
    });
  }

  /* The files of the working directory are kept with the configuration, so
     that a table you added is still there tomorrow. */
  function restoreWork() {
    var keep = CONFIG + '/work';
    if (!exists(keep)) return;
    mod.FS.readdir(keep).forEach(function (name) {
      if (name === '.' || name === '..') return;
      try { mod.FS.writeFile(WORK + '/' + name, mod.FS.readFile(keep + '/' + name)); } catch (e) { /* not a plain file */ }
    });
  }
  function saveWork() {
    if (!persistent) return;
    var keep = CONFIG + '/work';
    try {
      if (!exists(keep)) mod.FS.mkdirTree(keep);
      var now = {};
      workFiles().forEach(function (f) { now[f.name] = true; mod.FS.writeFile(keep + '/' + f.name, mod.FS.readFile(WORK + '/' + f.name)); });
      mod.FS.readdir(keep).forEach(function (name) { if (name !== '.' && name !== '..' && !now[name]) mod.FS.unlink(keep + '/' + name); });
    } catch (e) { /* the copy is a convenience */ }
  }

  function run(text) {
    if (busy || !mod) return;
    busy = true;
    input.disabled = true;
    echo(promptText(), text);
    flush();
    if (text.trim()) remember(text);
    // let the echo be painted before a long calculation holds the page
    setTimeout(function () {
      var again = true, failure = null;
      try {
        if (/^\s*manual\s*$/.test(text)) {
          note('The manual is a PDF: <a href="mm41-manual.pdf" target="_blank" rel="noopener">open it in a new tab</a>. Here, ' +
               '<b>manual finance</b> prints a chapter and <b>manual npv</b> the place a word is first explained.');
        } else {
          again = lineFn(text) !== 0;
        }
      } catch (e) {
        failure = e;
      }
      flush();
      if (failure) {
        printErr('The calculator stopped: ' + (failure && failure.message ? failure.message : failure));
        note('A new session is starting. Saved words, registers and settings are kept; the stack is not.');
        restart();
        return;
      }
      if (!again) {
        note('The session has ended. A new one is starting.');
        restart();
        return;
      }
      saveWork();
      syncUpSoon();
      showPrompt();
      listFiles();
      busy = false;
      input.disabled = false;
      input.focus();
    }, 0);
  }

  function restart() {
    mod = null;
    boot(false).then(function () { busy = false; }, fatal);
  }

  function fatal(e) {
    printErr('MM-41 could not start in this browser: ' + (e && e.message ? e.message : e));
    note('It needs WebAssembly, which every current browser has. If the page was opened from a file, serve it over http instead.');
  }

  // ---- history ----------------------------------------------------------------------------

  function loadHistory() {
    try { history = JSON.parse(localStorage.getItem(HISTORY_KEY) || '[]'); } catch (e) { history = []; }
    if (!Array.isArray(history)) history = [];
    historyAt = history.length;
  }
  function remember(text) {
    if (history[history.length - 1] !== text) history.push(text);
    if (history.length > 500) history = history.slice(-500);
    historyAt = history.length;
    try { localStorage.setItem(HISTORY_KEY, JSON.stringify(history)); } catch (e) { /* no storage: the session still has it */ }
  }
  function recall(step) {
    if (historyAt === history.length) draft = input.value;
    var at = Math.max(0, Math.min(history.length, historyAt + step));
    if (at === historyAt) return;
    historyAt = at;
    input.value = at === history.length ? draft : history[at];
    input.setSelectionRange(input.value.length, input.value.length);
  }

  // ---- completion --------------------------------------------------------------------------

  function complete() {
    var text = input.value, cursor = input.selectionStart;
    var p = mod.ccall('mm41_web_complete', 'number', ['string', 'number'], [text, cursor]);
    if (!p) return;
    var parts = mod.UTF8ToString(p).split('\n');
    mod._free(p);
    var start = Number(parts[0]), names = parts.slice(1).filter(function (n, i, a) { return n && a.indexOf(n) === i; });
    if (!names.length) return;
    var common = names[0];
    names.forEach(function (n) { var k = 0; while (k < common.length && n[k] === common[k]) k++; common = common.slice(0, k); });
    var word = text.slice(start, cursor);
    var insert = names.length === 1 ? names[0] + ' ' : common;
    if (insert.length > word.length || names.length === 1) {
      input.value = text.slice(0, start) + insert + text.slice(cursor);
      var at = start + insert.length;
      input.setSelectionRange(at, at);
      lastTab.text = null;
      return;
    }
    // nothing more to add: a second Tab lists the candidates, as readline does
    if (lastTab.text === text && lastTab.at === cursor) {
      var width = names.reduce(function (w, n) { return Math.max(w, n.length); }, 0) + 2;
      var per = Math.max(1, Math.floor(100 / width)), line = '';
      names.sort().forEach(function (n, i) {
        line += n + ' '.repeat(width - n.length);
        if ((i + 1) % per === 0) { print(line); line = ''; }
      });
      if (line) print(line);
      lastTab.text = null;
    } else {
      lastTab.text = text;
      lastTab.at = cursor;
    }
  }

  // ---- files ---------------------------------------------------------------------------------

  function workFiles() {
    var out = [];
    try {
      mod.FS.readdir(WORK).forEach(function (name) {
        if (name === '.' || name === '..') return;
        var st = mod.FS.stat(WORK + '/' + name);
        if (mod.FS.isFile(st.mode)) out.push({ name: name, size: st.size });
      });
    } catch (e) { /* no directory yet */ }
    return out.sort(function (a, b) { return a.name < b.name ? -1 : 1; });
  }

  function listFiles() {
    var ul = $('files');
    ul.textContent = '';
    workFiles().forEach(function (f) {
      var li = document.createElement('li'), a = document.createElement('a'), size = document.createElement('span');
      a.textContent = f.name;
      a.href = '#';
      a.title = 'Download ' + f.name;
      a.addEventListener('click', function (ev) {
        ev.preventDefault();
        var blob = new Blob([mod.FS.readFile(WORK + '/' + f.name)], { type: 'application/octet-stream' });
        var url = URL.createObjectURL(blob), link = document.createElement('a');
        link.href = url;
        link.download = f.name;
        document.body.appendChild(link);
        link.click();
        link.remove();
        setTimeout(function () { URL.revokeObjectURL(url); }, 1000);
      });
      size.className = 'size';
      size.textContent = f.size < 1024 ? f.size + ' B' : (f.size / 1024).toFixed(1) + ' kB';
      li.appendChild(a);
      li.appendChild(size);
      ul.appendChild(li);
    });
  }

  function addFiles(list) {
    if (!mod) return;
    Array.prototype.forEach.call(list, function (file) {
      var name = file.name.replace(/[^A-Za-z0-9._-]/g, '_');
      if (file.size > 20 * 1024 * 1024) { printErr(name + ': larger than 20 MB, not added'); return; }
      file.arrayBuffer().then(function (buf) {
        mod.FS.writeFile(WORK + '/' + name, new Uint8Array(buf));
        note('<b>' + escapeHtml(name) + '</b> (' + file.size + ' bytes) is in the calculator\'s directory: <b>"' + escapeHtml(name) +
             '" readmat</b> reads a table, <b>"' + escapeHtml(name) + '" load</b> runs a file of lines.');
        saveWork();
        syncUpSoon();
        listFiles();
      });
    });
  }

  // ---- wiring ---------------------------------------------------------------------------------

  function buildExamples() {
    var box = $('examples');
    EXAMPLES.forEach(function (group) {
      var h = document.createElement('h3');
      h.textContent = group[0];
      box.appendChild(h);
      group[1].forEach(function (ex) {
        var b = document.createElement('button');
        b.type = 'button';
        b.className = 'ex';
        b.textContent = ex[0];
        if (ex[1]) { var s = document.createElement('small'); s.textContent = ex[1]; b.appendChild(s); }
        b.addEventListener('click', function () {
          if (window.matchMedia('(max-width: 860px)').matches) setSide(false);
          run(ex[0]);
        });
        box.appendChild(b);
      });
    });
  }

  function setSide(open) {
    document.querySelector('.layout').classList.toggle('no-side', !open);
    $('toggle-side').setAttribute('aria-expanded', open ? 'true' : 'false');
  }

  form.addEventListener('submit', function (ev) {
    ev.preventDefault();
    var text = input.value;
    input.value = '';
    lastTab.text = null;
    run(text);
  });

  input.addEventListener('keydown', function (ev) {
    if (ev.key === 'ArrowUp') { ev.preventDefault(); recall(-1); }
    else if (ev.key === 'ArrowDown') { ev.preventDefault(); recall(1); }
    else if (ev.key === 'Tab' && !ev.shiftKey && !ev.altKey && !ev.ctrlKey && !ev.metaKey) {
      if (input.value.trim() === '') return;                 // an empty line: Tab moves on, as it does everywhere
      ev.preventDefault();
      complete();
    }
    else if (ev.key === 'l' && ev.ctrlKey) { ev.preventDefault(); screen.textContent = ''; }
    else if (ev.key === 'c' && ev.ctrlKey && input.selectionStart === input.selectionEnd) { ev.preventDefault(); echo(promptText(), input.value + '^C'); input.value = ''; }
    if (ev.key !== 'Tab') lastTab.text = null;
  });

  // a click on the screen that is not a selection puts the cursor in the line
  screen.addEventListener('mouseup', function () {
    var sel = window.getSelection();
    if (!sel || sel.isCollapsed) input.focus();
  });

  var terminal = $('terminal');
  terminal.addEventListener('dragover', function (ev) { ev.preventDefault(); terminal.classList.add('dropping'); });
  terminal.addEventListener('dragleave', function () { terminal.classList.remove('dropping'); });
  terminal.addEventListener('drop', function (ev) {
    ev.preventDefault();
    terminal.classList.remove('dropping');
    if (ev.dataTransfer && ev.dataTransfer.files) addFiles(ev.dataTransfer.files);
  });
  $('file').addEventListener('change', function (ev) { addFiles(ev.target.files); ev.target.value = ''; });

  $('toggle-side').addEventListener('click', function () {
    setSide(document.querySelector('.layout').classList.contains('no-side'));
  });

  $('reset').addEventListener('click', function () {
    if (!window.confirm('Forget the saved words, registers, settings, files and the history of lines kept in this browser?')) return;
    try { localStorage.removeItem(HISTORY_KEY); } catch (e) { /* nothing to remove */ }
    var done = function () { window.location.reload(); };
    try {
      var req = indexedDB.deleteDatabase(CONFIG);
      req.onsuccess = req.onerror = req.onblocked = done;
    } catch (e) { done(); }
  });

  if (window.MM41_BUILD && window.MM41_BUILD.version) $('version').textContent = window.MM41_BUILD.version;
  if (window.matchMedia('(max-width: 860px)').matches) setSide(false);
  buildExamples();
  loadHistory();
  if (typeof MM41 !== 'function') fatal(new Error('mm41.js did not load'));
  else boot(true).catch(fatal);
})();
