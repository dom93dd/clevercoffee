/**
 * @file ReviewPage.h
 *
 * @brief HTML page written by roundsim --review: every scenario in German and English, numbered,
 *        with a note field per screen and a button that copies all notes as text. With --compare
 *        it marks the screens that changed since an earlier review and can show the old picture;
 *        --notes adds a line per screen about what was changed.
 *        Between kReviewHead and kReviewTail go SCREENS, STAMP, COMPARED and NOTES (JavaScript).
 */

#pragma once

namespace sim {

    constexpr const char* kReviewHead = R"HTML(<!doctype html>
<html lang="de">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Rund-Display Abnahme</title>
<style>
  :root {
    --bg: #f3f2ef; --card: #ffffff; --ink: #1d1d1f; --muted: #6b6b70; --line: #deddd8;
    --accent: #b8742a; --ok: #1f8a5b; --fix: #c2410c; --field: #faf9f7;
  }
  @media (prefers-color-scheme: dark) {
    :root:not([data-theme="light"]) {
      --bg: #141416; --card: #1e1e21; --ink: #ececee; --muted: #9a9aa0; --line: #2e2e33;
      --accent: #e0a35a; --ok: #3ccf8e; --fix: #fb7f45; --field: #17171a;
    }
  }
  * { box-sizing: border-box; }
  body { margin: 0; background: var(--bg); color: var(--ink); font: 15px/1.45 -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; }
  header { max-width: 1280px; margin: 0 auto; padding: 28px 16px 8px; }
  h1 { font-size: 24px; margin: 0 0 4px; letter-spacing: -0.01em; }
  .sub { color: var(--muted); margin: 0 0 14px; }
  .rules { background: var(--card); border: 1px solid var(--line); border-radius: 10px; padding: 12px 16px; margin: 0 0 16px; }
  .rules p { margin: 0 0 6px; } .rules ul { margin: 0; padding-left: 20px; } .rules li { margin: 2px 0; }
  .bar { position: sticky; top: 0; z-index: 2; background: var(--bg); border-bottom: 1px solid var(--line); }
  .bar-inner { max-width: 1280px; margin: 0 auto; padding: 10px 16px; display: flex; flex-wrap: wrap; gap: 8px 16px; align-items: center; }
  .seg { display: inline-flex; border: 1px solid var(--line); border-radius: 8px; overflow: hidden; }
  .seg button { border: 0; background: var(--card); color: var(--ink); padding: 6px 12px; font: inherit; cursor: pointer; }
  .seg button[aria-pressed="true"] { background: var(--ink); color: var(--bg); }
  label.check { display: inline-flex; gap: 6px; align-items: center; cursor: pointer; }
  .count { color: var(--muted); }
  .primary { margin-left: auto; border: 0; border-radius: 8px; background: var(--accent); color: #fff; padding: 8px 14px; font: inherit; font-weight: 600; cursor: pointer; }
  main { max-width: 1280px; margin: 0 auto; padding: 16px; display: grid; grid-template-columns: repeat(auto-fill, minmax(min(100%, 400px), 1fr)); gap: 16px; }
  .card { background: var(--card); border: 1px solid var(--line); border-radius: 12px; padding: 12px; display: flex; flex-direction: column; gap: 10px; }
  .card.fix { border-color: var(--fix); box-shadow: 0 0 0 1px var(--fix); }
  .card.ok { border-color: var(--ok); }
  .badge { font-size: 12px; font-weight: 600; color: var(--accent); border: 1px solid var(--accent); border-radius: 999px; padding: 1px 8px; white-space: nowrap; }
  .note { background: var(--field); border-left: 3px solid var(--accent); border-radius: 6px; padding: 6px 10px; font-size: 14px; }
  .flip { border: 1px solid var(--line); background: var(--field); color: var(--ink); border-radius: 8px; padding: 4px 10px; font: inherit; font-size: 13px; cursor: pointer; }
  .flip[aria-pressed="true"] { background: var(--accent); border-color: var(--accent); color: #fff; }
  figure.before img { opacity: .92; outline: 3px dashed var(--accent); outline-offset: 3px; }
  .head { display: flex; gap: 10px; align-items: baseline; }
  .num { font-weight: 700; font-variant-numeric: tabular-nums; color: var(--accent); min-width: 2.2em; }
  .cap { font-weight: 600; }
  .name { color: var(--muted); font: 12px ui-monospace, SFMono-Regular, Menlo, monospace; margin-left: auto; }
  .shots { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; }
  .shots.single { grid-template-columns: 1fr; justify-items: center; }
  figure { margin: 0; text-align: center; }
  figure img { width: 100%; max-width: 280px; height: auto; display: block; margin: 0 auto; border-radius: 50%; }
  figcaption { color: var(--muted); font-size: 12px; margin-top: 2px; }
  .actions { display: flex; gap: 8px; }
  .actions button { flex: 1; border: 1px solid var(--line); background: var(--field); color: var(--ink); border-radius: 8px; padding: 6px; font: inherit; cursor: pointer; }
  .actions button.on[data-v="ok"] { background: var(--ok); border-color: var(--ok); color: #fff; }
  .actions button.on[data-v="fix"] { background: var(--fix); border-color: var(--fix); color: #fff; }
  textarea { width: 100%; min-height: 44px; resize: vertical; border: 1px solid var(--line); border-radius: 8px; background: var(--field); color: var(--ink); padding: 6px 8px; font: inherit; }
  .hidden { display: none !important; }
  .toast { position: fixed; left: 50%; bottom: 20px; transform: translateX(-50%); background: var(--ink); color: var(--bg); padding: 10px 16px; border-radius: 8px; opacity: 0; transition: opacity .2s; pointer-events: none; }
  .toast.show { opacity: 1; }
  #out { width: 100%; min-height: 120px; }
</style>
</head>
<body>
<header>
  <h1>Rund-Display – alle Screens zur Abnahme</h1>
  <p class="sub" id="stamp"></p>
  <div class="rules">
    <p>Jeder Screen ist ein echtes Bild aus dem Simulator (derselbe Code wie auf dem ESP32), links Deutsch, rechts Englisch. Der automatische Abstands-Check (<code>pio test -e test</code>, test_layout) sichert auf allen Screens ab:</p>
    <ul>
      <li>Texte und Symbole halten mindestens 8 px Abstand zum Ring, zu Strichen, Markern und Statussymbolen und zum Glasrand.</li>
      <li>Die Ziffern der großen Zahl halten 12 px zur nächsten Zeile (nur das Komma ragt hinein, mindestens 7 px).</li>
      <li>Übereinanderliegende Texte halten 7 px, Zeilen desselben Absatzes 3 px.</li>
    </ul>
    <p style="margin-top:8px">Pro Screen „Passt“ oder „Verbessern“ wählen, bei Bedarf kurz notieren, was stört. „Feedback kopieren“ sammelt alles als Text für den Chat.</p>
    <p id="changedInfo" class="hidden" style="margin-top:8px"></p>
  </div>
</header>
<div class="bar"><div class="bar-inner">
  <div class="seg" role="group" aria-label="Sprache">
    <button data-lang="both" aria-pressed="true">DE + EN</button><button data-lang="de" aria-pressed="false">DE</button><button data-lang="en" aria-pressed="false">EN</button>
  </div>
  <label class="check"><input type="checkbox" id="onlyOpen"> nur noch nicht bewertete</label>
  <label class="check hidden" id="onlyChangedLabel"><input type="checkbox" id="onlyChanged"> nur geänderte</label>
  <span class="count" id="count"></span>
  <button class="primary" id="copy">Feedback kopieren</button>
</div></div>
<main id="grid"></main>
<div class="toast" id="toast">Feedback kopiert – einfach in den Chat einfügen</div>
<script>
const SCREENS = )HTML";

    constexpr const char* kReviewTail = R"HTML(;
const KEY = 'rd-review-' + STAMP; // every review round starts with empty marks
let state = {};
try { state = JSON.parse(localStorage.getItem(KEY) || '{}') || {}; } catch (e) { state = {}; }
const save = () => { try { localStorage.setItem(KEY, JSON.stringify(state)); } catch (e) {} };
const grid = document.getElementById('grid');
document.getElementById('stamp').textContent = STAMP;

SCREENS.forEach((s, i) => {
  const n = i + 1;
  const card = document.createElement('section');
  card.className = 'card';
  card.dataset.name = s.name;
  card.dataset.changed = s.changed ? '1' : '';
  card.innerHTML = `
    <div class="head"><span class="num">#${n}</span><span class="cap"></span>${s.changed ? '<span class="badge">geändert</span>' : ''}<span class="name">${s.name}</span></div>
    ${NOTES[s.name] ? '<div class="note"></div>' : ''}
    ${s.changed ? '<div><button class="flip" aria-pressed="false">Vorher zeigen</button></div>' : ''}
    <div class="shots">
      <figure data-lang="de"><img loading="lazy" src="${s.file}-de.png" alt=""><figcaption>Deutsch</figcaption></figure>
      <figure data-lang="en"><img loading="lazy" src="${s.file}-en.png" alt=""><figcaption>Englisch</figcaption></figure>
    </div>
    <div class="actions"><button data-v="ok">Passt</button><button data-v="fix">Verbessern</button></div>
    <textarea placeholder="Was stört? (optional)"></textarea>`;
  card.querySelector('.cap').textContent = s.caption;
  if (NOTES[s.name]) card.querySelector('.note').textContent = 'Umgesetzt: ' + NOTES[s.name];
  if (s.changed) {
    const flip = card.querySelector('.flip');
    flip.addEventListener('click', () => {
      const before = flip.getAttribute('aria-pressed') !== 'true';
      flip.setAttribute('aria-pressed', before ? 'true' : 'false');
      flip.textContent = before ? 'Nachher zeigen' : 'Vorher zeigen';
      card.querySelectorAll('figure').forEach(f => {
        const lang = f.dataset.lang;
        f.querySelector('img').src = (before ? 'previous/' : '') + `${s.file}-${lang}.png`;
        f.classList.toggle('before', before);
        f.querySelector('figcaption').textContent = (lang === 'de' ? 'Deutsch' : 'Englisch') + (before ? ' – vorher' : '');
      });
    });
  }
  card.querySelectorAll('img')[0].alt = s.caption + ' (Deutsch)';
  card.querySelectorAll('img')[1].alt = s.caption + ' (Englisch)';
  const st = state[s.name] || {};
  const area = card.querySelector('textarea');
  area.value = st.note || '';
  area.addEventListener('input', () => { state[s.name] = Object.assign(state[s.name] || {}, { note: area.value }); if (area.value && !state[s.name].v) setVerdict('fix'); save(); });
  const setVerdict = (v) => {
    state[s.name] = Object.assign(state[s.name] || {}, { v });
    card.classList.toggle('ok', v === 'ok');
    card.classList.toggle('fix', v === 'fix');
    card.querySelectorAll('.actions button').forEach(b => b.classList.toggle('on', b.dataset.v === v));
    save(); refresh();
  };
  card.querySelectorAll('.actions button').forEach(b => b.addEventListener('click', () => setVerdict(state[s.name] && state[s.name].v === b.dataset.v ? '' : b.dataset.v)));
  if (st.v) { card.classList.add(st.v); card.querySelector(`.actions button[data-v="${st.v}"]`).classList.add('on'); }
  grid.appendChild(card);
});

if (COMPARED) {
  const n = SCREENS.filter(s => s.changed).length;
  document.getElementById('onlyChangedLabel').classList.remove('hidden');
  const info = document.getElementById('changedInfo');
  info.classList.remove('hidden');
  info.textContent = `${n} von ${SCREENS.length} Screens haben sich seit der letzten Runde geändert (Markierung „geändert“). „Vorher zeigen“ blendet bei diesen Screens den alten Stand ein.`;
}

function refresh() {
  const only = document.getElementById('onlyOpen').checked;
  const onlyChanged = document.getElementById('onlyChanged').checked;
  let ok = 0, fix = 0;
  SCREENS.forEach(s => { const v = (state[s.name] || {}).v; if (v === 'ok') ok++; if (v === 'fix') fix++; });
  document.getElementById('count').textContent = `${ok} passt · ${fix} verbessern · ${SCREENS.length - ok - fix} offen`;
  document.querySelectorAll('.card').forEach(c => c.classList.toggle('hidden', (only && !!(state[c.dataset.name] || {}).v) || (onlyChanged && !c.dataset.changed)));
}

document.getElementById('onlyOpen').addEventListener('change', refresh);
document.getElementById('onlyChanged').addEventListener('change', refresh);
document.querySelectorAll('.seg button').forEach(b => b.addEventListener('click', () => {
  document.querySelectorAll('.seg button').forEach(x => x.setAttribute('aria-pressed', x === b ? 'true' : 'false'));
  const lang = b.dataset.lang;
  document.querySelectorAll('figure').forEach(f => f.classList.toggle('hidden', lang !== 'both' && f.dataset.lang !== lang));
  document.querySelectorAll('.shots').forEach(x => x.classList.toggle('single', lang !== 'both'));
}));

document.getElementById('copy').addEventListener('click', async () => {
  const fix = [], ok = [], open = [];
  SCREENS.forEach((s, i) => {
    const st = state[s.name] || {};
    const tag = `#${i + 1} ${s.name} (${s.caption})`;
    if (st.v === 'fix' || (st.note && st.v !== 'ok')) fix.push(`- ${tag}${st.note ? ': ' + st.note.trim() : ''}`);
    else if (st.v === 'ok') { ok.push(`#${i + 1}`); if (st.note) fix.push(`- ${tag} (passt, Anmerkung): ${st.note.trim()}`); }
    else open.push(`#${i + 1}`);
  });
  const text = ['Feedback Rund-Display (' + STAMP + ')', '', 'Verbessern:', fix.length ? fix.join('\n') : '- nichts', '',
    'Passt: ' + (ok.length ? ok.join(', ') : '-'), 'Offen: ' + (open.length ? open.join(', ') : '-')].join('\n');
  try { await navigator.clipboard.writeText(text); }
  catch (e) { const t = document.createElement('textarea'); t.value = text; document.body.appendChild(t); t.select(); document.execCommand('copy'); t.remove(); }
  const toast = document.getElementById('toast'); toast.classList.add('show'); setTimeout(() => toast.classList.remove('show'), 1800);
});

refresh();
</script>
</body>
</html>
)HTML";

} // namespace sim
