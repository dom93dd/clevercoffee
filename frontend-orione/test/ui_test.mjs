// Browser test of frontend-orione/index.html against mock_esp32.py (start both with run.sh).
//
//   node ui_test.mjs [--base http://127.0.0.1:8099] [--busy http://127.0.0.1:8098] [--only name] [--headed]
//
// Each test gets a fresh mock state (POST /__reset) and a fresh page. The checks follow what the
// firmware needs from the page: one request at a time (src/webRequestGate.h), the right POSTs for
// every control, nothing sent by the reset buttons unless confirmed, tabs that switch without
// asking the ESP32 again. Screenshots of every tab land in out/.

import assert from "node:assert/strict";
import {mkdirSync} from "node:fs";
import {chromium} from "playwright";

const arg = (k, d) => { const i = process.argv.indexOf(k); return i > 0 ? process.argv[i + 1] : d; };
const BASE = arg("--base", "http://127.0.0.1:8099");
const BUSY = arg("--busy", "http://127.0.0.1:8098"); // mock that answers every 3rd GET /parameters with 503
const ONLY = arg("--only", null);
const OUT = new URL("./out/", import.meta.url).pathname;
mkdirSync(OUT, {recursive: true});

const tests = [];
const test = (name, fn, opt = {}) => tests.push({name, fn, ...opt});
const sleep = ms => new Promise(r => setTimeout(r, ms));

async function mock(base, path, body) {
  const r = await fetch(base + path, body === undefined ? {} : {method: "POST", redirect: "manual", body: typeof body === "string" ? body : JSON.stringify(body)});
  return r.headers.get("content-type")?.includes("json") ? r.json() : r.text();
}
const posts = base => mock(base, "/__posts");
const values = async base => (await mock(base, "/__state")).values;

// ---------- page helpers ----------
const contexts = []; // closed by the runner after each test, also when it failed halfway
async function open(browser, base, {width = 390, hash = "", route} = {}) {
  const ctx = await browser.newContext({viewport: {width, height: 844}, locale: "de-DE"});
  contexts.push(ctx);
  const page = await ctx.newPage();
  const errors = [], inflight = new Set();
  let maxInflight = 0;
  // the page itself is fully sent before its script runs; Playwright only reports it finished later
  const counted = r => !r.url().endsWith("/events") && !r.url().startsWith("data:") && !r.isNavigationRequest();
  page.on("pageerror", e => errors.push(e.message));
  page.on("console", m => { if (m.type() === "error" && !/503|Failed to load resource/.test(m.text())) errors.push(m.text()); });
  page.on("request", r => { if (counted(r)) { inflight.add(r); maxInflight = Math.max(maxInflight, inflight.size); } });
  page.on("requestfinished", r => inflight.delete(r));
  page.on("requestfailed", r => inflight.delete(r));
  if (route) await route(page);
  await page.goto(base + "/" + hash);
  await settle(page);
  return {ctx, page, errors, maxInflight: () => maxInflight, inflight};
}
// wait until the page's request chain has run dry (startup prefetch, debounced saves)
async function settle(page, quiet = 900) {
  let last = Date.now();
  const on = () => { last = Date.now(); };
  page.on("request", on); page.on("requestfinished", on); page.on("requestfailed", on);
  while (Date.now() - last < quiet) await sleep(100);
  page.off("request", on); page.off("requestfinished", on); page.off("requestfailed", on);
}
const view = page => page.locator("#main > div:not([hidden])");
const row = (page, label) => view(page).locator(".row", {has: page.locator(".lbl > div", {hasText: new RegExp("^" + label + "$")})});
async function tab(page, label) { // the switch runs on hashchange, after the click returned
  await page.locator("nav button", {hasText: label}).click();
  await page.locator("nav button.on", {hasText: label}).waitFor();
  await sleep(100);
}
async function toast(page, text) { await page.locator("#toast.show", {hasText: text}).waitFor({timeout: 4000}); }
async function noOverflow(page, what) {
  const [sw, w] = await page.evaluate(() => [document.scrollingElement.scrollWidth, innerWidth]);
  assert.ok(sw <= w, `${what}: page ${sw}px wide in a ${w}px window`);
}

// ---------- tests ----------
test("Start: Live-Werte, Soll, keine Fehler, eine Anfrage zur Zeit", async ({browser}) => {
  const p = await open(browser, BASE);
  await p.page.waitForFunction(() => !document.querySelector("#tNow").textContent.startsWith("–"));
  assert.match(await p.page.locator("#state").textContent(), /Heizt auf|Bereit/);
  assert.equal(await row(p.page, "Stoppen").count(), 0, "the next shot is set up on the brew tab only");
  assert.equal(await row(p.page, "Temperaturregelung").count(), 1);
  assert.equal(await view(p.page).locator(".step input").first().inputValue(), "95,0 °C");
  assert.match(await p.page.locator("#tSet").textContent(), /Soll 95,0 °C/);
  assert.equal(await p.page.locator("text=Version").count(), 0, "version only in the care tab");
  assert.equal(p.maxInflight(), 1, "the ESP32 answers one request at a time");
  assert.deepEqual(p.errors, []);
  assert.deepEqual(await posts(BASE), [], "loading the page must not change anything");
  await p.page.screenshot({path: OUT + "status.png", fullPage: true});
  await p.ctx.close();
});

test("Soll-Temperatur: +/− und Eingabe speichern einmal, gerundet und begrenzt", async ({browser}) => {
  const {page, ctx} = await open(browser, BASE);
  const step = view(page).locator(".hero .step");
  await step.locator("button", {hasText: "+"}).click();
  await step.locator("button", {hasText: "+"}).click();
  await toast(page, "Gespeichert");
  await settle(page);
  assert.deepEqual((await posts(BASE)).map(x => x.body), ["brew.setpoint=96.0"], "two clicks, one debounced save");
  await step.locator("input").fill("93,7");
  await step.locator("input").press("Enter");
  await settle(page);
  assert.equal((await values(BASE))["brew.setpoint"], 93.5, "rounded to the 0.5 step");
  await step.locator("input").fill("150");
  await step.locator("input").press("Enter");
  await settle(page);
  assert.equal((await values(BASE))["brew.setpoint"], 110, "clamped to the firmware maximum");
  assert.equal(await step.locator("input").inputValue(), "110,0 °C");
  await ctx.close();
});

test("Stoppen: von Hand / nach Zeit / nach Gewicht setzt Modus und Schalter", async ({browser}) => {
  const {page, ctx} = await open(browser, BASE, {hash: "#brew"});
  const stop = row(page, "Stoppen");
  await stop.locator("button", {hasText: "nach Zeit"}).click();
  await row(page, "Bezugszeit").waitFor();
  let v = await values(BASE);
  assert.equal(v["brew.mode"], 1); assert.equal(v["brew.by_time.enabled"], 1); assert.equal(v["brew.by_weight.enabled"], 0);
  assert.equal(await row(page, "Bezugszeit").locator("input").inputValue(), "25,0 s");
  await row(page, "Bezugszeit").locator("button", {hasText: "+"}).click();
  await settle(page);
  assert.equal((await values(BASE))["brew.by_time.target_time"], 25.5);

  await row(page, "Stoppen").locator("button", {hasText: "nach Gewicht"}).click();
  await row(page, "Zielgewicht").waitFor();
  v = await values(BASE);
  assert.equal(v["brew.by_weight.enabled"], 1); assert.equal(v["brew.by_time.enabled"], 0);
  assert.equal(await row(page, "Bezugszeit").count(), 0);

  await row(page, "Stoppen").locator("button", {hasText: "von Hand"}).click();
  await settle(page);
  assert.equal((await values(BASE))["brew.mode"], 0);
  assert.equal(await row(page, "Zielgewicht").count(), 0);
  assert.equal(await row(page, "Stoppen").locator("button.on").textContent(), "von Hand");

  await page.reload(); // the choice comes back from the machine
  await settle(page);
  assert.equal(await row(page, "Stoppen").locator("button.on").textContent(), "von Hand");
  await ctx.close();
});

test("Reiter: Wechsel ohne neue Anfragen, Scrollposition bleibt", async ({browser}) => {
  const {page, ctx} = await open(browser, BASE, {width: 360});
  let gets = 0;
  page.on("request", r => { if (r.url().includes("/parameters") || r.url().endsWith("/")) gets++; });
  for (const [label, file] of [["Bezug", "brew"], ["Einstellungen", "settings"], ["Wartung", "care"], ["Maschine", null]]) {
    await tab(page, label);
    await sleep(150);
    await noOverflow(page, label);
    if (file) await page.screenshot({path: OUT + file + ".png", fullPage: true});
  }
  assert.equal(gets, 0, "all tabs were fetched in the background after the start");
  await tab(page, "Einstellungen");
  await view(page).locator("summary", {hasText: "Regler"}).click(); // longer page
  const at = await page.evaluate(() => { scrollTo(0, 300); return scrollY; });
  assert.ok(at > 100, "settings page scrollable: " + at);
  await tab(page, "Maschine");
  assert.equal(await page.evaluate(() => scrollY), 0);
  await tab(page, "Einstellungen");
  assert.equal(await page.evaluate(() => scrollY), at);
  assert.equal(await view(page).locator("details[open]").count(), 1, "the opened section stays open");
  assert.match(page.url(), /#settings$/);
  await page.reload(); // the hash keeps the tab across a reload
  await settle(page);
  assert.equal(await page.locator("nav button.on").textContent(), "Einstellungen");
  await ctx.close();
});

test("Waage aus: Neustart-Hinweis, neu starten, Seite lädt danach neu", async ({browser}) => {
  const {page, ctx} = await open(browser, BASE, {hash: "#settings"});
  assert.equal(await row(page, "Timer der Waage mitlaufen lassen").count(), 1);
  await row(page, "Bluetooth-Waage").locator(".sw").click();
  await page.locator("#rebootBar:not([hidden])", {hasText: "Wirkt nach einem Neustart"}).waitFor();
  await settle(page);
  assert.equal((await values(BASE))["hardware.sensors.scale.enabled"], 0);
  assert.equal(await row(page, "Timer der Waage mitlaufen lassen").count(), 0);
  await tab(page, "Bezug");
  assert.equal(await row(page, "Stoppen").locator("button").count(), 2);
  assert.match(await row(page, "Stoppen").textContent(), /erst Waage einschalten/);
  await page.screenshot({path: OUT + "restart-bar.png"});
  await page.locator("#rebootBtn", {hasText: "Jetzt neu starten"}).click();
  await page.waitForFunction(() => performance.getEntriesByType("navigation")[0]?.type === "reload", null, {timeout: 15000});
  await settle(page);
  const st = await mock(BASE, "/__state");
  assert.equal(st.restarts, 1); assert.equal(st.pageLoads, 2);
  assert.equal(await page.locator("#rebootBar").isHidden(), true);
  assert.deepEqual(await view(page).locator(".chips button").allTextContents(), ["Espresso25 s", "Doppio30 s", "Lungo45 s"], "scale off since the start");
  await ctx.close();
});

test("Schalter mit abhängigen Zeilen: Standby-Zeit erst bei Standby an", async ({browser}) => {
  const {page, ctx} = await open(browser, BASE, {hash: "#settings"});
  assert.equal(await row(page, "Nach").count(), 0);
  await row(page, "Standby").locator(".sw").click();
  await row(page, "Nach").waitFor();
  assert.equal(await row(page, "Nach").locator("input").inputValue(), "35 min");
  await settle(page);
  assert.equal((await values(BASE))["standby.enabled"], 1);
  await ctx.close();
});

test("Backflush: Zustand wird zurückgelesen, Hinweis auf der Startseite", async ({browser}) => {
  const {page, ctx} = await open(browser, BASE, {hash: "#care"});
  const sw = row(page, "Backflush-Modus").locator(".sw");
  await sw.click();
  await settle(page);
  assert.equal((await values(BASE))["BACKFLUSH_ON"], 1, JSON.stringify(await posts(BASE)));
  assert.match(await sw.getAttribute("class"), /\bon\b/);
  await tab(page, "Maschine");
  assert.equal(await page.locator(".note", {hasText: "Backflush-Modus aktiv"}).count(), 1);
  await mock(BASE, "/toggleBackflush", ""); // the machine ended the mode by itself, the page still shows it on
  await tab(page, "Wartung");
  await row(page, "Backflush-Modus").locator(".sw").click(); // "off": a blind toggle would switch it on again
  await settle(page);
  assert.equal((await values(BASE))["BACKFLUSH_ON"], 0);
  assert.equal((await posts(BASE)).filter(x => x.path === "/toggleBackflush").length, 2, "the page toggled once, the test once");
  assert.doesNotMatch(await row(page, "Backflush-Modus").locator(".sw").getAttribute("class"), /\bon\b/);
  await tab(page, "Maschine");
  assert.equal(await page.locator(".note", {hasText: "Backflush-Modus aktiv"}).count(), 0);
  assert.equal((await mock(BASE, "/__state")).pageLoads, 1, "the toggle's redirect to / must not load the page again");
  await ctx.close();
});

test("Tarieren: nur ein Auftrag, solange einer offen ist", async ({browser}) => {
  const {page, ctx} = await open(browser, BASE, {hash: "#brew"});
  const b = view(page).locator("button", {hasText: "Waage tarieren"});
  await b.click(); await settle(page);
  await b.click(); await settle(page);
  const tares = (await posts(BASE)).filter(x => x.path === "/toggleTareScale");
  assert.equal(tares.length, 1);
  assert.equal((await values(BASE))["TARE_ON"], 1);
  await ctx.close();
});

test("Sprache: Englisch und zurück, nur DE/EN angeboten", async ({browser}) => {
  const {page, ctx} = await open(browser, BASE, {hash: "#settings"});
  const lang = row(page, "Sprache");
  assert.deepEqual(await lang.locator("button").allTextContents(), ["Deutsch", "English"]);
  await lang.locator("button", {hasText: "English"}).click();
  await page.locator("nav button", {hasText: "Machine"}).waitFor();
  assert.equal(await page.evaluate(() => document.documentElement.lang), "en");
  await row(page, "Language").locator("button", {hasText: "Deutsch"}).click();
  await page.locator("nav button", {hasText: "Maschine"}).waitFor();
  await settle(page);
  assert.equal((await values(BASE))["display.language"], 0);
  await ctx.close();
});

test("Text: Gerätename speichern mit Neustart-Hinweis", async ({browser}) => {
  const {page, ctx} = await open(browser, BASE, {hash: "#settings"});
  await view(page).locator("summary", {hasText: "System"}).click();
  const inp = row(page, "Gerätename").locator("input");
  assert.equal(await inp.inputValue(), "orione");
  assert.equal(await row(page, "Update-Passwort").locator("input").getAttribute("type"), "password");
  await inp.fill("orione-test");
  await inp.press("Enter");
  await page.locator("#rebootBar:not([hidden])", {hasText: "Wirkt nach einem Neustart"}).waitFor();
  assert.equal((await values(BASE))["system.hostname"], "orione-test");
  await ctx.close();
});

test("Sicherung einspielen: Upload, dann Neustart übernimmt die Werte", async ({browser}) => {
  const {page, ctx} = await open(browser, BASE, {hash: "#care"});
  await view(page).locator("input[type=file]").setInputFiles({name: "config.json", mimeType: "application/json",
    buffer: Buffer.from(JSON.stringify({"brew.setpoint": 93, "standby.enabled": 1}))});
  await toast(page, "Eingespielt, startet neu");
  await settle(page);
  const s = await mock(BASE, "/__state");
  assert.equal(s.restarts, 1, "the firmware applies an uploaded config only at the next start");
  assert.equal(s.values["brew.setpoint"], 93);
  assert.deepEqual((await posts(BASE)).map(x => x.path), ["/upload/config", "/restart"]);
  await ctx.close();
});

test("Gerät: Zurücksetzen nur nach Bestätigung, Version nur in Wartung", async ({browser}) => {
  const {page, ctx} = await open(browser, BASE, {hash: "#care"});
  await view(page).locator(".ver", {hasText: "Version 4.0.4+mock"}).waitFor();
  page.on("dialog", d => d.dismiss());
  for (const label of ["WLAN zurücksetzen", "Werkseinstellungen", "Neu starten"]) await view(page).locator("button", {hasText: label}).click();
  await settle(page);
  assert.deepEqual(await posts(BASE), [], "dismissed dialogs send nothing");
  page.removeAllListeners("dialog");
  page.on("dialog", d => d.accept());
  await view(page).locator("button", {hasText: "Neu starten"}).click();
  await toast(page, "Startet neu");
  assert.deepEqual((await posts(BASE)).map(x => x.path), ["/restart"]);
  await ctx.close();
});

test("Laufender Bezug und Verbindungsverlust in der Statusanzeige", async ({browser}) => {
  await mock(BASE, "/__live", {state: 20, brewTime: 12.3});
  const {page, ctx} = await open(browser, BASE);
  await page.locator("#state", {hasText: "Bezug 12,3 s"}).waitFor({timeout: 3000});
  await ctx.close();
  const off = await open(browser, BASE, {route: pg => pg.route("**/events", r => r.abort())});
  await off.page.locator("#state", {hasText: "Keine Verbindung"}).waitFor({timeout: 3000});
  await off.ctx.close();
});

test("Letzte Bezüge: Zeit, Gewicht, wann; neuer Bezug erscheint von selbst", async ({browser}) => {
  const now = Math.floor(Date.now() / 1000);
  const hm = at => new Date(at * 1000).toLocaleTimeString("de-DE", {hour: "2-digit", minute: "2-digit"});
  await mock(BASE, "/__shot", {s: 24.8, g: null, at: now - 86400 - 60}); // yesterday, no scale
  await mock(BASE, "/__shot", {s: 25.3, g: 36.1, at: now - 600});
  const {page, ctx} = await open(browser, BASE, {hash: "#brew"});
  const list = view(page).locator("#shotList .shot");
  await list.first().waitFor();
  const rows = await list.evaluateAll(rs => rs.map(r => [...r.children].map(c => c.textContent)));
  // a day and a minute ago is "gestern", except around a change of daylight saving time
  const day = new Date((now - 86460) * 1000), older = day.toDateString() === new Date(Date.now() - 864e5).toDateString() ? "gestern" : day.toLocaleDateString("de-DE", {day: "2-digit", month: "2-digit"});
  assert.deepEqual(rows, [["25,3 s", "36,1 g", "heute " + hm(now - 600)], ["24,8 s", "–", older + " " + hm(now - 86460)]]);
  await page.screenshot({path: OUT + "brew-shots.png", fullPage: true});

  // a shot runs and ends: the page asks again once the firmware has counted the drops
  await mock(BASE, "/__live", {state: 20, brewTime: 12});
  await page.locator("#state", {hasText: "Bezug"}).waitFor();
  await mock(BASE, "/__shot", {s: 26.1, g: 37.4, at: now});
  await mock(BASE, "/__live", {});
  await page.waitForFunction(() => document.querySelectorAll("#shotList .shot").length === 3, null, {timeout: 9000});
  assert.equal(await list.first().locator("b").textContent(), "26,1 s");
  await ctx.close();
});

test("Schnellwahl: Tipp setzt Stopp-Art und Ziele (mit Waage nach Gewicht)", async ({browser}) => {
  const {page, ctx} = await open(browser, BASE, {hash: "#brew"});
  const chips = view(page).locator(".chips button");
  assert.deepEqual(await chips.allTextContents(), ["Espresso36 g", "Doppio45 g", "Lungo80 g"]);
  assert.equal(await view(page).locator(".chips button.on").count(), 0, "by hand: no preset active");
  await chips.nth(1).click();
  await row(page, "Zielgewicht").waitFor();
  await settle(page);
  const v = await values(BASE);
  assert.equal(v["brew.mode"], 1); assert.equal(v["brew.by_weight.enabled"], 1); assert.equal(v["brew.by_time.enabled"], 0);
  assert.equal(v["brew.by_weight.target_weight"], 45); assert.equal(v["brew.by_time.target_time"], 30);
  assert.equal(await view(page).locator(".chips button.on").textContent(), "Doppio45 g");
  assert.equal(await row(page, "Zielgewicht").locator("input").inputValue(), "45,0 g");
  await page.screenshot({path: OUT + "brew-presets.png", fullPage: true});
  await ctx.close();
});

test("Schnellwahl ohne Waage: nach Zeit", async ({browser}) => {
  await mock(BASE, "/parameters", "hardware.sensors.scale.enabled=0");
  await mock(BASE, "/restart", ""); // the scale settings go with the next start, as in the firmware
  const {page, ctx} = await open(browser, BASE, {hash: "#brew"});
  const chips = view(page).locator(".chips button");
  assert.deepEqual(await chips.allTextContents(), ["Espresso25 s", "Doppio30 s", "Lungo45 s"]);
  await chips.nth(2).click();
  await row(page, "Bezugszeit").waitFor();
  await settle(page);
  const v = await values(BASE);
  assert.equal(v["brew.mode"], 1); assert.equal(v["brew.by_time.enabled"], 1); assert.equal(v["brew.by_time.target_time"], 45);
  assert.equal(await view(page).locator(".chips button.on").textContent(), "Lungo45 s");
  await ctx.close();
});

test("Schnellwahl: Werte ändern und behalten", async ({browser}) => {
  const {page, ctx} = await open(browser, BASE, {hash: "#settings"});
  const espresso = row(page, "Espresso");
  assert.deepEqual(await espresso.locator("input").evaluateAll(xs => xs.map(x => x.value)), ["25,0 s", "36,0 g"]);
  await espresso.locator(".step").nth(1).locator("button", {hasText: "+"}).click();
  await settle(page);
  assert.equal((await values(BASE))["brew.presets"], "25,36.5;30,45;45,80");
  await page.screenshot({path: OUT + "settings-presets.png", fullPage: true});
  await tab(page, "Bezug");
  assert.equal(await view(page).locator(".chips button").first().textContent(), "Espresso36,5 g");
  await page.reload();
  await settle(page);
  await tab(page, "Einstellungen");
  assert.deepEqual(await row(page, "Espresso").locator("input").evaluateAll(xs => xs.map(x => x.value)), ["25,0 s", "36,5 g"]);
  await ctx.close();
});

test("Letzte Bezüge: leer", async ({browser}) => {
  const {page, ctx} = await open(browser, BASE, {hash: "#brew"});
  await view(page).locator("#shotList .empty", {hasText: "Noch keine Bezüge"}).waitFor();
  await ctx.close();
});

test("Beschäftigt (503): alles kommt trotzdem an, keine Fehlermeldung", async ({browser}) => {
  const {page, ctx, errors} = await open(browser, BUSY, {width: 320});
  let failed = false;
  page.on("console", () => {});
  const seen = [];
  for (const [label] of [["Maschine"], ["Einstellungen"], ["Wartung"], ["Bezug"]]) {
    await tab(page, label);
    await settle(page);
    seen.push(await view(page).locator(".row").count());
    if (await page.locator("#toast.show", {hasText: "Nicht gespeichert"}).count()) failed = true;
    await noOverflow(page, label + " @320");
  }
  await row(page, "Stoppen").locator("button", {hasText: "nach Zeit"}).click();
  await row(page, "Bezugszeit").waitFor({timeout: 15000});
  await settle(page);
  const v = await values(BUSY);
  assert.equal(v["brew.mode"], 1); assert.equal(v["brew.by_time.enabled"], 1);
  assert.ok(!failed, "no 'not saved' toast");
  assert.ok(seen.every(n => n > 0), "every tab has rows: " + seen);
  assert.deepEqual(errors, []);
  await ctx.close();
}, {base: BUSY});

// ---------- runner ----------
const browser = await chromium.launch({headless: !process.argv.includes("--headed")});
let failed = 0, ran = 0;
for (const t of tests) {
  if (ONLY && !t.name.includes(ONLY)) continue;
  ran++;
  await mock(t.base || BASE, "/__reset", "");
  const t0 = Date.now();
  try {
    await t.fn({browser});
    console.log(`ok    ${t.name} (${((Date.now() - t0) / 1000).toFixed(1)} s)`);
  } catch (e) {
    failed++;
    console.log(`FAIL  ${t.name}\n      ${String(e.message).split("\n").join("\n      ")}`);
  }
  while (contexts.length) await contexts.pop().close().catch(() => {});
}
await browser.close();
console.log(failed ? `${failed} von ${ran} Tests fehlgeschlagen` : `Alle ${ran} Tests bestanden`);
process.exit(failed ? 1 : 0);
