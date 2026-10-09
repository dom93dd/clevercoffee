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
async function open(browser, base, {width = 390, hash = "", route, init} = {}) {
  const ctx = await browser.newContext({viewport: {width, height: 844}, locale: "de-DE"});
  contexts.push(ctx);
  if (init) await ctx.addInitScript(init);
  const page = await ctx.newPage();
  const errors = [], inflight = new Set();
  let maxInflight = 0;
  // the page itself is fully sent before its script runs; Playwright only reports it finished later
  // the browser fetches the manifest by itself, small and once
  const counted = r => !r.url().endsWith("/events") && !r.url().startsWith("data:") && !r.isNavigationRequest() && !r.url().endsWith("/manifest.json");
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

// Stand-in for api.anthropic.com in this page: answers like the Messages API with "stream": true,
// records what the page sent. status other than 200: an error answer (e.g. 401 for a bad key).
async function fakeClaude(page, reply, {status = 200, abort = false, message = "invalid x-api-key"} = {}) {
  const seen = [];
  await page.route("https://api.anthropic.com/**", async r => {
    const cors = {"access-control-allow-origin": "*", "access-control-allow-headers": "*", "access-control-allow-methods": "POST, OPTIONS"};
    if (r.request().method() === "OPTIONS") return r.fulfill({status: 204, headers: cors});
    seen.push({headers: r.request().headers(), body: JSON.parse(r.request().postData())});
    if (abort) return r.abort();
    if (status !== 200) return r.fulfill({status, headers: cors, contentType: "application/json", body: JSON.stringify({type: "error", error: {type: status === 401 ? "authentication_error" : "invalid_request_error", message}})});
    const answer = typeof reply === "function" ? reply(seen.at(-1).body) : reply, text = typeof answer === "string" ? answer : answer.text;
    const ev = o => `event: ${o.type}\ndata: ${JSON.stringify(o)}\n\n`;
    // a proposal ({text, tool: input}) follows the text as a tool_use block, its input in pieces as the API streams it
    const json = answer.tool ? JSON.stringify(answer.tool) : "";
    const toolUse = !answer.tool ? "" : ev({type: "content_block_start", index: 1, content_block: {type: "tool_use", id: "toolu_1", name: "einstellungen_vorschlagen", input: {}}}) +
      json.match(/.{1,9}/gs).map(c => ev({type: "content_block_delta", index: 1, delta: {type: "input_json_delta", partial_json: c}})).join("") + ev({type: "content_block_stop", index: 1});
    const body = ev({type: "message_start", message: {}}) + ev({type: "content_block_start", index: 0, content_block: {type: "text", text: ""}}) +
      text.match(/.{1,12}/gs).map(c => ev({type: "content_block_delta", index: 0, delta: {type: "text_delta", text: c}})).join("") + ev({type: "content_block_stop", index: 0}) + toolUse + ev({type: "message_stop"});
    return r.fulfill({status: 200, headers: {...cors, "content-type": "text/event-stream"}, body});
  });
  return seen;
}
const withKey = () => localStorage.setItem("orione.claude.key", "sk-ant-test");

// ---------- tests ----------
test("Start: Live-Werte, Soll, keine Fehler, eine Anfrage zur Zeit", async ({browser}) => {
  const p = await open(browser, BASE);
  await p.page.waitForFunction(() => !document.querySelector("#tNow").textContent.startsWith("–"));
  assert.match(await p.page.locator("#state").textContent(), /Heizt auf|Bereit/);
  assert.equal(await row(p.page, "Stoppen").count(), 0, "the next shot is set up on the brew tab only");
  assert.equal(await row(p.page, "Heizen").count(), 1);
  assert.equal(await view(p.page).locator(".step input").first().inputValue(), "95,0 °C");
  assert.equal(await p.page.locator("#tSet").isHidden(), true, "the setpoint shows in the stepper right below");
  assert.equal(await p.page.locator("text=Version").count(), 0, "version only in the care tab");
  assert.equal(p.maxInflight(), 1, "the ESP32 answers one request at a time");
  assert.deepEqual(p.errors, []);
  assert.deepEqual(await posts(BASE), [], "loading the page must not change anything");
  await p.page.screenshot({path: OUT + "status.png", fullPage: true});
  await p.ctx.close();
});

test("Soll-Temperatur: +/− in Schritten, Eingabe genau, einmal gespeichert und begrenzt", async ({browser}) => {
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
  assert.equal((await values(BASE))["brew.setpoint"], 93.7, "typed: kept with the shown decimal, not rounded to the 0.5 step");
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
  await view(page).locator("summary", {hasText: "Fortgeschritten"}).click(); // longer page
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
  assert.equal(await view(page).locator(".scalepair").count(), 1);
  await row(page, "Bluetooth-Waage").locator(".sw").click();
  await page.locator("#rebootBar:not([hidden])", {hasText: "Wirkt nach einem Neustart"}).waitFor();
  await settle(page);
  assert.equal((await values(BASE))["hardware.sensors.scale.enabled"], 0);
  assert.equal(await view(page).locator(".scalepair").count(), 0, "scale choice only with the scale on");
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

test("Neustart-Hinweis: verschwindet beim Zurückstellen, spricht die Seitensprache, verdeckt nichts", async ({browser}) => {
  const {page, ctx} = await open(browser, BASE, {hash: "#settings"});
  const bar = page.locator("#rebootBar");
  await row(page, "Füllstandssensor").locator(".sw").click(); // needs a restart
  await page.locator("#rebootBar:not([hidden])", {hasText: "Wirkt nach einem Neustart"}).waitFor();
  await row(page, "Sprache").locator("button", {hasText: "English"}).click();
  await page.locator("#rebootBar", {hasText: "Takes effect after a restart"}).waitFor();
  await row(page, "Language").locator("button", {hasText: "Deutsch"}).click();
  await page.locator("#rebootBar", {hasText: "Wirkt nach einem Neustart"}).waitFor();
  // the last row stays reachable above bar and tab bar
  const last = view(page).locator("summary", {hasText: "System"});
  await last.evaluate(e => e.scrollIntoView({block: "end"}));
  await page.evaluate(() => scrollTo(0, document.scrollingElement.scrollHeight));
  const [lb, bb] = [await last.boundingBox(), await bar.boundingBox()];
  assert.ok(lb.y + lb.height <= bb.y, `last row ${Math.round(lb.y + lb.height)} under the bar ${Math.round(bb.y)}`);
  await row(page, "Füllstandssensor").locator(".sw").click(); // back to the start value
  await settle(page);
  assert.equal(await bar.isHidden(), true, "nothing left to restart for");
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
  const rows = await list.evaluateAll(rs => rs.map(r => [...r.children].slice(0, 3).map(c => c.textContent)));
  // "heute", "gestern" or the date, as the page decides it (shortly after midnight 10 minutes ago is yesterday)
  const label = at => { const d = new Date(at * 1000), day = x => x.toDateString(); return day(d) === day(new Date()) ? "heute" : day(d) === day(new Date(Date.now() - 864e5)) ? "gestern" : d.toLocaleDateString("de-DE", {day: "2-digit", month: "2-digit"}); };
  assert.deepEqual(rows, [["25,3 s", "36,1 g", label(now - 600) + " " + hm(now - 600)], ["24,8 s", "–", label(now - 86460) + " " + hm(now - 86460)]]);
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
  assert.deepEqual(await chips.allTextContents(), ["Espresso1:2,0 · 36 g", "Doppio1:2,5 · 45 g", "Lungo1:4,5 · 81 g"], "a ratio, in grams at the dose");
  assert.equal(await view(page).locator(".chips button.on").count(), 0, "by hand: no preset active");
  await chips.nth(1).click();
  await row(page, "Zielgewicht").waitFor();
  await settle(page);
  const v = await values(BASE);
  assert.equal(v["brew.mode"], 1); assert.equal(v["brew.by_weight.enabled"], 1); assert.equal(v["brew.by_time.enabled"], 0);
  assert.equal(v["brew.by_weight.target_weight"], 45); assert.equal(v["brew.by_time.target_time"], 30);
  assert.equal(await view(page).locator(".chips button.on").textContent(), "Doppio1:2,5 · 45 g");
  assert.equal(await row(page, "Zielgewicht").locator("input").inputValue(), "45,0 g");
  await page.screenshot({path: OUT + "brew-presets.png", fullPage: true});
  await ctx.close();
});

test("Schnellwahl: ein im Reiter Bezug geändertes Ziel übernimmt der gewählte Chip", async ({browser}) => {
  const form = body => fetch(BASE + "/parameters", {method: "POST", headers: {"Content-Type": "application/x-www-form-urlencoded"}, body});
  await form("brew.mode=1"); await form("brew.by_weight.enabled=1&brew.by_time.enabled=0");
  const {page, ctx, errors} = await open(browser, BASE, {hash: "#brew"});
  await view(page).locator(".chips button.on", {hasText: "Espresso"}).waitFor(); // 36 g: Espresso
  await row(page, "Zielgewicht").locator("button", {hasText: "−"}).click();
  await view(page).locator(".chips button.on", {hasText: "Espresso1:2,0 · 35,5 g"}).waitFor(); // the chip keeps the ratio of the new target
  await settle(page);
  let v = await values(BASE);
  assert.equal(v["brew.by_weight.target_weight"], 35.5);
  assert.equal(v["brew.presets"], "25,1.97;30,2.5;45,4.5");
  await view(page).locator("summary", {hasText: "Feineinstellung"}).click();
  await row(page, "Ohne Waage nach").locator("button", {hasText: "+"}).click(); // the time goes along as well
  await settle(page);
  assert.equal((await values(BASE))["brew.presets"], "25.5,1.97;30,2.5;45,4.5");
  await view(page).locator(".chips button", {hasText: "Doppio"}).click(); // another chip: takes its values, changes none
  await view(page).locator(".chips button.on", {hasText: "Doppio1:2,5 · 45 g"}).waitFor();
  await settle(page);
  v = await values(BASE);
  assert.equal(v["brew.presets"], "25.5,1.97;30,2.5;45,4.5");
  assert.equal(v["brew.by_weight.target_weight"], 45); assert.equal(v["brew.by_time.target_time"], 30);
  await tab(page, "Einstellungen"); // the quick choice there shows it too
  assert.deepEqual(await row(page, "Espresso").locator("input").evaluateAll(xs => xs.map(x => x.value)), ["25,5 s", "1:2,0"]);
  assert.deepEqual(errors, []);
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
  assert.deepEqual(await espresso.locator("input").evaluateAll(xs => xs.map(x => x.value)), ["25,0 s", "1:2,0"]);
  await espresso.locator(".step").nth(1).locator("button", {hasText: "+"}).click();
  await settle(page);
  assert.equal((await values(BASE))["brew.presets"], "25,2.1;30,2.5;45,4.5");
  await page.screenshot({path: OUT + "settings-presets.png", fullPage: true});
  await tab(page, "Bezug");
  assert.equal(await view(page).locator(".chips button").first().textContent(), "Espresso1:2,1 · 38 g");
  await page.reload();
  await settle(page);
  await tab(page, "Einstellungen");
  assert.deepEqual(await row(page, "Espresso").locator("input").evaluateAll(xs => xs.map(x => x.value)), ["25,0 s", "1:2,1"]);
  await ctx.close();
});

test("Letzte Bezüge: Balken-Übersicht und Kurve je Bezug", async ({browser}) => {
  const now = Math.floor(Date.now() / 1000);
  await mock(BASE, "/__shot", {s: 31.0, g: null, at: now - 7200, nocurve: true});
  await mock(BASE, "/__shot", {s: 24.8, g: null, at: now - 3600});
  await mock(BASE, "/__shot", {s: 25.3, g: 36.1, at: now - 600});
  const {page, ctx, errors} = await open(browser, BASE, {hash: "#brew"});
  const painted = sel => page.evaluate(s => { // canvas has something drawn on it
    const c = document.querySelector(s); if (!c || !c.width) return 0;
    const d = c.getContext("2d").getImageData(0, 0, c.width, c.height).data; let n = 0;
    for (let i = 3; i < d.length; i += 4) n += d[i] > 0; return n;
  }, sel);
  await view(page).locator("#shotList canvas.bars").waitFor();
  assert.ok(await painted("#shotList canvas.bars") > 500, "bar chart drawn");
  assert.deepEqual(await view(page).locator("#shotList > .legend span").allTextContents(), ["Zeit", "Gewicht"]);

  const rows = view(page).locator("#shotList .shot"), curves = view(page).locator("#shotList .curve");
  await rows.nth(0).click();
  await curves.nth(0).locator("canvas").waitFor();
  assert.ok(await painted("#shotList .curve:not([hidden]) canvas") > 1000, "curve drawn");
  assert.deepEqual(await curves.nth(0).locator(".legend span").allTextContents(), ["Gewicht", "Durchfluss", "Temperatur", "Pumpe aus"]);
  assert.match(await rows.nth(0).getAttribute("class"), /open/);
  await page.screenshot({path: OUT + "brew-curve.png", fullPage: true});

  await rows.nth(1).click(); // the next one: the first closes, no scale means no weight line
  await curves.nth(1).locator("canvas").waitFor();
  assert.equal(await curves.nth(0).isHidden(), true);
  assert.deepEqual(await curves.nth(1).locator(".legend span").allTextContents(), ["Temperatur", "Pumpe aus"]);
  await rows.nth(1).click();
  assert.equal(await curves.nth(1).isHidden(), true);

  await rows.nth(2).click();
  await curves.nth(2).locator(".empty", {hasText: "keine Kurve"}).waitFor();
  const gets = (await page.evaluate(() => performance.getEntriesByType("resource").map(e => e.name))).filter(u => u.includes("/shot?"));
  assert.equal(gets.length, 3, "each curve fetched once: " + gets);
  await rows.nth(0).click(); // again: from the cache
  await curves.nth(0).locator("canvas").waitFor();
  assert.equal((await page.evaluate(() => performance.getEntriesByType("resource").map(e => e.name))).filter(u => u.includes("/shot?")).length, 3);
  assert.deepEqual(errors, []);
  await ctx.close();
});

test("Live-Bezug: Zeit, Gewicht, Fortschritt zum Ziel, danach Ergebnis", async ({browser}) => {
  const {page, ctx} = await open(browser, BASE, {hash: "#brew"});
  await view(page).locator(".chips button").nth(1).click(); // Doppio: by weight, 45 g
  await row(page, "Zielgewicht").waitFor();
  await settle(page);
  assert.equal(await page.locator("#liveShot").isHidden(), true, "no shot, no live card");
  await mock(BASE, "/__live", {state: 20, brewTime: 12.3, weight: 18, scale: 2});
  await page.locator("#liveShot:not([hidden]) #lsLab", {hasText: "Bezug läuft"}).waitFor();
  assert.equal(await page.locator("#lsTime").textContent(), "12,3 s");
  assert.equal(await page.locator("#lsWeight").textContent(), "18,0 g");
  assert.equal(await page.locator("#lsGoal").textContent(), "Ziel 45 g");
  await mock(BASE, "/__live", {state: 20, brewTime: 12.3, weight: 18, scale: 2, flow: 2.14});
  await page.locator("#lsGoal", {hasText: "Ziel 45 g · 2,1 g/s"}).waitFor();
  assert.match(await page.locator("#lsBar").getAttribute("style"), /scaleX\(0\.4\)/);
  assert.equal(await page.title(), "Bezug 12,3 s · Orione");
  await page.screenshot({path: OUT + "brew-live.png", fullPage: true});
  await mock(BASE, "/__live", {brewTime: 12.8}); // the firmware's first value after the shot: the final time
  await page.locator("#lsLab", {hasText: "Fertig"}).waitFor();
  assert.equal(await page.locator("#lsTime").textContent(), "12,8 s", "final time, and it stays");
  await ctx.close();
});

test("Waagen-Status im Reiter Bezug, mit Akkustand", async ({browser}) => {
  const {page, ctx} = await open(browser, BASE, {hash: "#brew"});
  await page.locator("#scaleSt.on", {hasText: "Waage verbunden · 0,0 g · Akku 76 %"}).waitFor();
  assert.equal(await page.locator("#scaleSt .low").count(), 0);
  await mock(BASE, "/__live", {battery: 18});
  await page.locator("#scaleSt .low", {hasText: /^Akku 18 % – bitte laden$/}).waitFor();
  assert.equal(await page.locator("#scaleSt").textContent(), "Waage verbunden · 0,0 g · Akku 18 % – bitte laden");
  await page.screenshot({path: OUT + "brew-battery-low.png", fullPage: true});
  await mock(BASE, "/__live", {weight: -0.02}); // noise just under zero after taring
  await page.locator("#scaleSt", {hasText: "Waage verbunden · 0,0 g · Akku 76 %"}).waitFor();
  await mock(BASE, "/__live", {weight: -0.3});
  await page.locator("#scaleSt", {hasText: "Waage verbunden · -0,3 g"}).waitFor(); // a real negative reading stays
  await mock(BASE, "/__live", {battery: null}); // a scale that does not report its battery
  await page.locator("#scaleSt.on", {hasText: /^Waage verbunden · 0,0 g$/}).waitFor();
  await mock(BASE, "/__live", {scale: 1, weight: null, battery: 50}); // not connected: no battery, whatever comes
  await page.locator("#scaleSt:not(.on)", {hasText: "nicht verbunden"}).waitFor();
  assert.equal(await page.locator("#scaleSt").textContent(), "Waage nicht verbunden – ist sie eingeschaltet?");
  await ctx.close();
});

test("Alarm: Banner auf der Startseite und im Titel", async ({browser}) => {
  const {page, ctx} = await open(browser, BASE);
  assert.equal(await page.locator("#alarm").isHidden(), true);
  await mock(BASE, "/__live", {state: 110});
  await page.locator("#alarm.err:not([hidden])", {hasText: "Sensorfehler"}).waitFor();
  assert.match(await page.locator("#alarm").textContent(), /Temperaturfühler und Kabel prüfen/);
  assert.equal(await page.title(), "Sensorfehler · Orione");
  await mock(BASE, "/__live", {state: 70});
  await page.locator("#alarm.info", {hasText: "Wassertank leer"}).waitFor();
  await page.screenshot({path: OUT + "status-alarm.png"});
  await mock(BASE, "/__live", {});
  await page.locator("#alarm[hidden]").waitFor({state: "attached"});
  await ctx.close();
});

test("Bedienbarkeit: Schalter mit Zustand, ganze Zeile tippbar, 44-px-Tasten, 16-px-Felder", async ({browser}) => {
  const {page, ctx} = await open(browser, BASE, {hash: "#settings"});
  const sw = row(page, "Standby").locator(".sw");
  assert.equal(await sw.getAttribute("aria-checked"), "false");
  assert.equal(await sw.getAttribute("aria-label"), "Standby");
  await row(page, "Standby").locator(".lbl p").click(); // the hint text, not the switch
  await settle(page);
  assert.equal((await values(BASE))["standby.enabled"], 1);
  assert.equal(await row(page, "Standby").locator(".sw").getAttribute("aria-checked"), "true");
  await view(page).locator("summary", {hasText: "Fortgeschritten"}).click(); // the offset is a fine setting now
  const b = await row(page, "Offset").locator(".step button").first().boundingBox();
  assert.ok(b.width >= 44 && b.height >= 44, `stepper button ${b.width}x${b.height}`);
  assert.equal(await row(page, "Offset").locator(".step button").first().getAttribute("aria-label"), "weniger");
  await view(page).locator("summary", {hasText: "System"}).click();
  assert.equal(await row(page, "Gerätename").locator("input").evaluate(e => getComputedStyle(e).fontSize), "16px");
  await ctx.close();
});

test("Rezept: Dosis, Mahlgrad und Verhältnis im nächsten Bezug", async ({browser}) => {
  const {page, ctx} = await open(browser, BASE, {hash: "#brew"});
  const rc = view(page).locator(".recipe");
  assert.deepEqual(await rc.locator("label").allTextContents(), ["Bohne", "Röstdatum", "Dosis", "Mahlgrad"]);
  assert.equal(await rc.locator(".roast").isVisible(), false, "a roast date only for a bean that has a name");
  assert.equal(await rc.locator(".step input").inputValue(), "18,0 g");
  assert.equal(await rc.locator(".txt").inputValue(), "12");
  await rc.locator(".txt").fill("14"); await rc.locator(".txt").press("Enter");
  await settle(page);
  assert.equal((await values(BASE))["brew.grind"], "14");
  await rc.locator(".txt").fill(""); await rc.locator(".txt").press("Enter"); // emptied again
  await settle(page);
  assert.equal((await values(BASE))["brew.grind"], "");
  await view(page).locator(".chips button").nth(1).click(); // Doppio, by weight: 45 g
  await page.locator("#ratio", {hasText: "Verhältnis 1:2,5"}).waitFor();
  await view(page).locator(".recipe .step input").fill("15"); await view(page).locator(".recipe .step input").press("Enter");
  await page.locator("#ratio", {hasText: "Verhältnis 1:2,5"}).waitFor(); // the chosen ratio stays, the target follows the dose
  await settle(page);
  assert.equal((await values(BASE))["brew.by_weight.target_weight"], 37.5);
  assert.equal(await row(page, "Zielgewicht").locator("input").inputValue(), "37,5 g");
  await page.screenshot({path: OUT + "brew-recipe.png", fullPage: true});
  await ctx.close();
});

test("Bohnen: neue anlegen, zurückwechseln holt ihre Werte, wählen und löschen in Einstellungen", async ({browser}) => {
  const form = body => fetch(BASE + "/parameters", {method: "POST", headers: {"Content-Type": "application/x-www-form-urlencoded"}, body});
  await form("brew.beans=" + encodeURIComponent("Ettli Don Pedro"));
  const {page, ctx, errors} = await open(browser, BASE, {hash: "#brew"});
  const pick = view(page).locator("#beanPick"), rc = view(page).locator(".recipe");
  await page.waitForFunction(() => document.querySelectorAll("#beanPick option").length === 2);
  assert.deepEqual(await pick.locator("option").allTextContents(), ["Ettli Don Pedro", "+ Neue Bohne …"]);
  assert.equal(await pick.inputValue(), "Ettli Don Pedro");
  // a new bean: named in a prompt, starts from the current values
  page.once("dialog", d => d.accept("Röstwerk Hell"));
  await pick.selectOption({index: 1});
  await toast(page, "Neue Bohne „Röstwerk Hell“ angelegt");
  await settle(page);
  let v = await values(BASE);
  assert.equal(v["brew.beans"], "Röstwerk Hell"); assert.equal(v["brew.dose"], 18);
  // its own dose and grind
  await rc.locator(".step input").fill("16,5"); await rc.locator(".step input").press("Enter");
  await settle(page);
  await rc.locator(".txt").fill("21"); await rc.locator(".txt").press("Enter");
  await settle(page);
  // back to the first one: its values come back
  await view(page).locator("#beanPick").selectOption("Ettli Don Pedro");
  await toast(page, "„Ettli Don Pedro“ geladen · 18 g → 36 g · Mahlgrad 12 · 95 °C · Nachlauf 1,00 s");
  await settle(page);
  v = await values(BASE);
  assert.equal(v["brew.beans"], "Ettli Don Pedro"); assert.equal(v["brew.dose"], 18); assert.equal(v["brew.grind"], "12");
  assert.equal(await view(page).locator(".recipe .step input").inputValue(), "18,0 g", "the field shows it");
  assert.equal(await view(page).locator(".recipe .txt").inputValue(), "12");
  await page.screenshot({path: OUT + "brew-beans.png", fullPage: true});
  // settings: both with their values, the current one marked
  await tab(page, "Einstellungen");
  const list = view(page).locator("#beanList");
  await list.locator(".beanrow").nth(1).waitFor();
  const rows = list.locator(".beanrow");
  assert.equal(await rows.nth(0).locator(".lbl > div").textContent(), "Ettli Don Pedro");
  assert.equal(await rows.nth(0).locator(".on").textContent(), "aktiv");
  assert.equal(await rows.nth(1).locator(".lbl p").textContent(), "16,5 g → 36 g · Mahlgrad 21 · 95 °C · Nachlauf 1,00 s");
  await noOverflow(page, "beans @390");
  await page.locator("#toast:not(.show)").waitFor({timeout: 6000});
  await view(page).locator('[data-card="sCoffee"]').evaluate(e => e.scrollIntoView({block: "start"}));
  await page.screenshot({path: OUT + "settings-beans.png"});
  await rows.nth(1).locator("button", {hasText: "Wählen"}).click();
  await toast(page, "„Röstwerk Hell“ geladen");
  await settle(page);
  v = await values(BASE);
  assert.equal(v["brew.beans"], "Röstwerk Hell"); assert.equal(v["brew.dose"], 16.5); assert.equal(v["brew.grind"], "21");
  assert.equal(await row(page, "Bohne").locator("input").inputValue(), "Röstwerk Hell", "the text field follows");
  // delete the other one, only after asking
  page.once("dialog", d => d.dismiss());
  await view(page).locator("#beanList .beanrow", {hasText: "Ettli"}).locator("button", {hasText: "Löschen"}).click();
  await settle(page);
  assert.equal((await mock(BASE, "/beans")).beans.length, 2, "not without confirming");
  page.once("dialog", d => d.accept());
  await view(page).locator("#beanList .beanrow", {hasText: "Ettli"}).locator("button", {hasText: "Löschen"}).click();
  await toast(page, "Gelöscht");
  await view(page).locator("#beanList .beanrow").nth(1).waitFor({state: "detached"});
  assert.deepEqual((await mock(BASE, "/beans")).beans.map(b => b.n), ["Röstwerk Hell"]);
  // typed in the coffee card: the same bean in other case is no new one
  await row(page, "Bohne").locator("input").fill("röstwerk hell"); await row(page, "Bohne").locator("input").press("Enter");
  await settle(page);
  assert.equal((await mock(BASE, "/beans")).beans.length, 1);
  assert.deepEqual(errors, []);
  await ctx.close();
});

test("Bohnen: Firmware ohne Profile zeigt keine Auswahl", async ({browser}) => {
  await mock(BASE, "/__live", {noBeans: true});
  const {page, ctx, errors} = await open(browser, BASE, {hash: "#brew"});
  await settle(page);
  assert.equal(await view(page).locator(".recipe .bean").isVisible(), false);
  await tab(page, "Einstellungen");
  assert.equal(await view(page).locator("#beanList .beanrow").count(), 0);
  assert.deepEqual(errors, []);
  await ctx.close();
});

test("Bezug aufklappen: Rezept, erster Tropfen, Bewertung, Vergleich, kalt gestartet", async ({browser}) => {
  const now = Math.floor(Date.now() / 1000);
  await mock(BASE, "/__shot", {s: 22.0, g: 33.0, at: now - 3600, d: 18.0, m: "13", t0: 89.5, fd: 4.8});
  await mock(BASE, "/__shot", {s: 25.3, g: 36.1, at: now - 600, d: 18.0, m: "12", t0: 94.6, fd: 6.2});
  const {page, ctx} = await open(browser, BASE, {hash: "#brew"});
  await view(page).locator("#shotList .shot").first().click();
  const cv = view(page).locator("#shotList .curve:not([hidden])");
  await cv.locator("canvas").waitFor();
  assert.equal(await cv.locator(".shotinfo").first().textContent(), "18,0 g Kaffee · Mahlgrad 12 · 1:2,0 · erster Tropfen nach 6,2 s · 10 g nach 11,3 s · Start bei 94,6 °C");
  assert.deepEqual(await cv.locator(".legend span").allTextContents(), ["Gewicht", "Durchfluss", "Temperatur", "Pumpe aus"]);
  assert.equal(await view(page).locator("#shotList .shot").nth(1).locator(".cold").textContent(), "kalt gestartet", "89,5 °C at 95 °C set");
  assert.equal(await view(page).locator("#shotList .shot").first().locator(".cold").count(), 0);
  await cv.locator(".taste button", {hasText: "sauer"}).click();
  await page.locator("#shotList .curve:not([hidden]) .taste button.on", {hasText: "sauer"}).waitFor();
  await settle(page);
  assert.equal((await mock(BASE, "/shots")).shots[0].r, 1);
  await view(page).locator("#shotList .curve:not([hidden]) .cmp button").first().click();
  await page.locator("#shotList .curve:not([hidden]) .cmp button.on").waitFor();
  const gets = (await page.evaluate(() => performance.getEntriesByType("resource").map(e => e.name))).filter(u => u.includes("/shot?"));
  assert.equal(gets.length, 2, "the other curve fetched for the comparison");
  await page.screenshot({path: OUT + "brew-compare.png", fullPage: true});
  await ctx.close();
});

test("Claude verbinden: ein Schritt, Schlüssel bleibt im Browser", async ({browser}) => {
  const {page, ctx} = await open(browser, BASE, {hash: "#settings"});
  const seen = await fakeClaude(page, "OK");
  const card = view(page).locator(".card.claude");
  assert.equal(await card.locator("a.linkbtn").getAttribute("href"), "https://console.anthropic.com/settings/keys");
  await card.locator("input").fill("abc"); await card.locator("button", {hasText: "Verbinden"}).click();
  await card.locator(".err", {hasText: "sk-ant-"}).waitFor();
  assert.equal(seen.length, 0, "nothing sent for an obviously wrong key");
  await card.locator("input").fill("sk-ant-test"); await card.locator("button", {hasText: "Verbinden"}).click();
  await card.locator(".lbl", {hasText: "Verbunden"}).waitFor();
  assert.equal(await page.evaluate(() => localStorage.getItem("orione.claude.key")), "sk-ant-test");
  const h = seen[0].headers;
  assert.equal(h["x-api-key"], "sk-ant-test"); assert.equal(h["anthropic-dangerous-direct-browser-access"], "true"); assert.ok(h["anthropic-version"]);
  assert.equal(seen[0].body.model, "claude-sonnet-5");
  assert.equal((await posts(BASE)).length, 0, "the machine never sees the key");
  await page.screenshot({path: OUT + "settings-claude.png", fullPage: true});
  await card.locator("button", {hasText: "Trennen"}).click();
  await card.locator("button", {hasText: "Verbinden"}).waitFor();
  assert.equal(await page.evaluate(() => localStorage.getItem("orione.claude.key")), null);
  await ctx.close();
});

test("Claude: kein API-Guthaben wird verständlich gemeldet, beim Verbinden und später", async ({browser}) => {
  const credit = "Your credit balance is too low to access the Anthropic API. Please go to Plans & Billing to upgrade or purchase credits.";
  let p = await open(browser, BASE, {hash: "#settings"});
  await fakeClaude(p.page, "x", {status: 400, message: credit});
  const card = view(p.page).locator(".card.claude");
  await card.locator("input").fill("sk-ant-test"); await card.locator("button", {hasText: "Verbinden"}).click();
  await card.locator(".err", {hasText: "API-Guthaben ist leer"}).waitFor();
  assert.equal(await p.page.evaluate(() => localStorage.getItem("orione.claude.key")), null, "not connected");
  await p.ctx.close();
  // connected earlier, credit used up since: no suggestion, the reason in the settings
  await mock(BASE, "/__shot", {s: 25.3, g: 36.1, at: Math.floor(Date.now() / 1000) - 600});
  p = await open(browser, BASE, {hash: "#brew", init: withKey, route: pg => fakeClaude(pg, "x", {status: 400, message: credit})});
  await settle(p.page);
  assert.equal(await p.page.locator(".ai").count(), 0);
  await tab(p.page, "Einstellungen");
  await view(p.page).locator(".card.claude .err", {hasText: "API-Guthaben ist leer"}).waitFor();
  assert.equal(await view(p.page).locator(".card.claude .lbl", {hasText: "Verbunden"}).count(), 1, "still connected");
  await p.ctx.close();
});

test("Claude: Vorschlag zum neuesten Bezug, einmal; Bewertung fragt neu; ausführlich", async ({browser}) => {
  const now = Math.floor(Date.now() / 1000);
  await mock(BASE, "/__shot", {s: 25.3, g: 36.1, at: now - 600, d: 18.0, m: "12"});
  let seen;
  const {page, ctx} = await open(browser, BASE, {hash: "#brew", init: withKey, route: async pg => { seen = await fakeClaude(pg, b => b.max_tokens > 1000 ? "Zeit und Verhältnis passen, die Temperatur fällt kaum ab." : "Etwas zu schnell – eine Stufe feiner."); }});
  const box = view(page).locator("#shotList > .ai");
  await box.locator("div", {hasText: "eine Stufe feiner"}).waitFor();
  assert.equal(await box.locator("b").textContent(), "Vorschlag zum letzten Bezug");
  const b0 = seen[0].body, user = b0.messages[0].content;
  assert.match(b0.system, /Barista/); assert.match(b0.system, /auf Deutsch/);
  for (const part of ["Quick Mill Orione", "25,3 s", "36,1 g in der Tasse", "18,0 g Kaffee", "Mahlgrad 12", "Temperatur in °C", "Durchfluss in g/s"]) assert.ok(user.includes(part), part);
  assert.equal(b0.stream, true);
  await page.screenshot({path: OUT + "brew-claude.png", fullPage: true});
  await page.reload(); await settle(page);
  await view(page).locator("#shotList > .ai div", {hasText: "eine Stufe feiner"}).waitFor();
  assert.equal(seen.length, 1, "kept in the browser, not asked again");
  await view(page).locator("#shotList .shot").first().click();
  await view(page).locator("#shotList .curve:not([hidden]) .taste button", {hasText: "sauer"}).click();
  await page.waitForFunction(() => true); await settle(page);
  assert.equal(seen.length, 2); assert.ok(seen[1].body.messages[0].content.includes("Geschmack sauer"));
  await view(page).locator("#shotList > .ai .linkbtn", {hasText: "Ausführlich"}).click();
  await view(page).locator("#shotList > .ai b", {hasText: "Auswertung"}).waitFor();
  const ai = view(page).locator("#shotList > details.ai");
  await ai.locator("div", {hasText: "Zeit und Verhältnis passen"}).waitFor({state: "visible"}); // just asked for: open
  await ai.locator("summary").click(); // folds away
  assert.equal(await ai.locator("div").isVisible(), false);
  assert.equal(await ai.locator(".teaser").textContent(), "Zeit und Verhältnis passen, die Temperatur fällt kaum ab.");
  assert.ok(await ai.locator(".teaser").isVisible(), "one line of it while folded");
  await page.screenshot({path: OUT + "brew-claude-folded.png", fullPage: true});
  await page.reload(); await settle(page);
  const again = view(page).locator("#shotList > details.ai");
  await again.locator("b", {hasText: "Auswertung"}).waitFor();
  assert.equal(await again.evaluate(d => d.open), false, "the long analysis starts folded");
  await again.locator("summary").click();
  await again.locator("div", {hasText: "Zeit und Verhältnis passen"}).waitFor({state: "visible"});
  assert.equal(seen[2].body.max_tokens, 4000);
  assert.equal(seen[2].body.thinking, undefined, "the long analysis may think");
  assert.deepEqual(seen[0].body.thinking, {type: "disabled"}, "the short one answers right away");
  await ctx.close();
});

test("Claude: schlägt Einstellungen vor, Übernehmen setzt sie, nur gültige Werte", async ({browser}) => {
  const now = Math.floor(Date.now() / 1000), form = body => fetch(BASE + "/parameters", {method: "POST", headers: {"Content-Type": "application/x-www-form-urlencoded"}, body});
  await form("brew.mode=1"); await form("brew.by_weight.enabled=1&brew.by_time.enabled=0");
  await mock(BASE, "/__shot", {s: 19.0, g: 36.2, at: now - 300, d: 18.0, m: "12", r: 1, tw: 36.0, sw: 34.4, ld: 1.8, lg: 1.0, fs: 1.8});
  let seen;
  const reply = {text: "Zu schnell und sauer – eine Stufe feiner und etwas mehr Kaffee.", tool: {mahlgrad: "11", dosis_g: 18.5, temperatur_c: 99.0, bezugszeit_s: 30, nachlaufzeit_s: 1.4}};
  const {page, ctx, errors} = await open(browser, BASE, {hash: "#brew", init: withKey, route: async pg => { seen = await fakeClaude(pg, reply); }});
  const box = view(page).locator("#shotList > .ai");
  await box.locator(".apply").waitFor();
  const b0 = seen[0].body, user = b0.messages[0].content;
  assert.equal(b0.tools?.[0]?.name, "einstellungen_vorschlagen"); assert.deepEqual(b0.tool_choice, {type: "auto"});
  assert.ok(user.includes("Schlag nicht vor, früher oder später zu stoppen"), "the stop learns by itself");
  assert.ok(user.includes("Eingestellt für den nächsten Bezug: Dosis 18,0 g, Mahlgrad 12, Zielgewicht 36,0 g, Brühtemperatur 95,0 °C."), user);
  // the time target does not apply while stopping by weight; 99 °C is more than a step from 95 °C: 98 °C
  assert.deepEqual((await box.locator(".apply span").allTextContents()).map(x => x.replace(/\u00a0/g, " ")),
    ["Mahlgrad 12 → 11", "Dosis 18,0 g → 18,5 g", "Brühtemperatur 95,0 °C → 98,0 °C", "Nachlaufzeit 1,00 s → 1,40 s"]);
  await page.screenshot({path: OUT + "brew-claude-apply.png", fullPage: true});
  await box.locator(".apply button", {hasText: "Übernehmen"}).click();
  await toast(page, "Vorschlag übernommen");
  await settle(page);
  const v = await values(BASE);
  assert.equal(v["brew.grind"], "11"); assert.equal(v["brew.dose"], 18.5); assert.equal(v["brew.setpoint"], 98); assert.equal(v["brew.by_weight.lag"], 1.4);
  assert.equal(v["brew.by_time.target_time"], 25, "not touched");
  await view(page).locator("#shotList > .ai .apply button:disabled", {hasText: "Übernommen"}).waitFor();
  assert.equal(await view(page).locator(".recipe .txt").inputValue(), "11", "the recipe shows it");
  await page.reload(); await settle(page);
  await view(page).locator("#shotList > .ai .apply button:disabled", {hasText: "Übernommen"}).waitFor();
  assert.equal(seen.length, 1, "kept in the browser");
  assert.deepEqual(errors, []);
  await ctx.close();
});

test("Pre-Infusion (Test): einschalten, Zeiten und Ventil in den Einstellungen", async ({browser}) => {
  const {page, ctx, errors} = await open(browser, BASE, {hash: "#settings"});
  const card = view(page).locator('[data-card="sPreinf"]');
  assert.equal(await card.locator("h2").textContent(), "Pre-Infusion (Test)");
  assert.equal(await card.locator(".row").count(), 1, "times and valve only once it is on");
  await row(page, "Pre-Infusion").locator(".sw").click();
  await row(page, "Pumpstoß").waitFor();
  await row(page, "Pumpstoß").locator("button", {hasText: "+"}).click();
  await settle(page);
  const v = await values(BASE);
  assert.equal(v["brew.pre_infusion.enabled"], 1); assert.equal(v["brew.pre_infusion.time"], 2.5);
  assert.equal(await row(page, "Pause").locator("input").inputValue(), "4,0 s");
  await card.screenshot({path: OUT + "settings-preinfusion.png"});
  await tab(page, "Bezug");
  assert.equal(await view(page).locator(".pinote").textContent(), "2,5 s Pumpstoß, 4,0 s Pause");
  const pr = view(page).locator(".row", {has: page.locator(".pinote")}); // switched per shot on the brew tab
  assert.equal(await pr.locator(".sw").getAttribute("aria-checked"), "true");
  await pr.locator(".sw").click();
  await settle(page);
  assert.equal((await values(BASE))["brew.pre_infusion.enabled"], 0);
  assert.deepEqual(errors, []);
  await ctx.close();
});

test("Pre-Infusion (Test): Phase im Live-Bezug, im Bezugsdetail, in der Kurve und für Claude", async ({browser}) => {
  const now = Math.floor(Date.now() / 1000), form = body => fetch(BASE + "/parameters", {method: "POST", headers: {"Content-Type": "application/x-www-form-urlencoded"}, body});
  await form("brew.pre_infusion.enabled=1&brew.pre_infusion.time=2&brew.pre_infusion.pause=4");
  await mock(BASE, "/__shot", {s: 31.0, g: 36.0, at: now - 300, d: 18.0, m: "12", fd: 7.5, pi: [2.0, 4.0, 0]});
  let seen;
  const {page, ctx, errors} = await open(browser, BASE, {hash: "#brew", init: withKey, route: async pg => { seen = await fakeClaude(pg, "Passt."); }});
  await view(page).locator("#shotList > .ai div", {hasText: "Passt."}).waitFor();
  const user = seen[0].body.messages[0].content;
  assert.ok(user.includes("Pre-Infusion ist eingeschaltet (ein Test): 2,0 s Pumpstoß mit offenem Ventil, dann 4,0 s Pause, in der Pause bleibt das Ventil offen und die Pumpe pulst langsam weiter"), user);
  assert.ok(user.includes("mit Pre-Infusion 2,0 s + 4,0 s Pause (Ventil zu)"));
  assert.ok(user.includes("31,0 s (davon 6,0 s Pre-Infusion, Bezug danach 25,0 s)"), "the time without it");
  assert.ok(user.includes("die Laufzeit-Faustregel gilt für die Zeit danach"));
  await view(page).locator("#shotList .shot").first().click();
  const info = view(page).locator("#shotList .curve:not([hidden]) .shotinfo");
  await info.waitFor();
  assert.match(await info.textContent(), /Pre-Infusion 2,0 s \+ 4,0 s Pause \(Ventil zu\) · Bezug danach 25,0 s · erster Tropfen nach 7,5 s/);
  await view(page).locator("#shotList .curve:not([hidden]) canvas").waitFor();
  await page.screenshot({path: OUT + "brew-preinfusion-curve.png", fullPage: true});
  for (const [pi, label] of [[1, "Pre-Infusion"], [2, "Pause"], [0, "Bezug läuft"]]) {
    await mock(BASE, "/__live", {state: 20, brewTime: 1.5 + pi, weight: 0, scale: 2, pi});
    await page.locator("#lsLab", {hasText: new RegExp("^" + label + "$")}).waitFor();
  }
  assert.deepEqual(errors, []);
  await ctx.close();
});

test("Claude: Mahlgrad-Vorschlag passt zur Skala der Mühle", async ({browser}) => {
  const now = Math.floor(Date.now() / 1000), form = body => fetch(BASE + "/parameters", {method: "POST", headers: {"Content-Type": "application/x-www-form-urlencoded"}, body});
  await form("brew.grind_scale=1&brew.grind=21");
  await mock(BASE, "/__shot", {s: 19.0, g: 36.0, at: now - 300, d: 18.0, m: "21", r: 1});
  // "finer" but a smaller number, on a grinder where higher is finer: not offered
  let seen;
  let {page, ctx} = await open(browser, BASE, {hash: "#brew", init: withKey, route: async pg => { seen = await fakeClaude(pg, {text: "Sauer: eine Stufe feiner.", tool: {mahlgrad: "20", mahlgrad_richtung: "feiner"}}); }});
  await view(page).locator("#shotList > .ai div", {hasText: "eine Stufe feiner"}).waitFor();
  await settle(page);
  assert.equal(await view(page).locator("#shotList > .ai .apply").count(), 0, "number and word disagree");
  assert.ok(seen[0].body.messages[0].content.includes("Skala der Mühle: eine höhere Zahl mahlt feiner, eine kleinere gröber."));
  await ctx.close();
  // the number alone: the direction comes from the scale
  ({page, ctx} = await open(browser, BASE, {hash: "#brew", init: withKey, route: async pg => { await fakeClaude(pg, {text: "Sauer: eine Stufe feiner.", tool: {mahlgrad: "22"}}); }}));
  await view(page).locator("#shotList > .ai .apply span").first().waitFor();
  assert.equal((await view(page).locator("#shotList > .ai .apply span").first().textContent()), "Mahlgrad 21 → 22 (feiner)");
  await tab(page, "Einstellungen");
  assert.deepEqual(await row(page, "Skala der Mühle").locator("button").allTextContents(), ["unbekannt", "höher = feiner", "höher = gröber"]);
  assert.equal(await row(page, "Skala der Mühle").locator("button.on").textContent(), "höher = feiner");
  await ctx.close();
});

test("Wartung: Protokoll über WLAN, Hinweis auf ein nicht gestartetes Update", async ({browser}) => {
  await mock(BASE, "/__live", {updateFailed: true, fw: "pending", bootReason: "software"});
  const {page, ctx, errors} = await open(browser, BASE, {hash: "#care"});
  const log = view(page).locator("a.btn", {hasText: "Protokoll anzeigen"});
  assert.equal(await log.getAttribute("href"), "/log");
  assert.equal(await log.getAttribute("target"), "_blank");
  await view(page).locator("#bootInfo", {hasText: "neue Firmware wird geprüft"}).waitFor();
  assert.equal(await view(page).locator("#bootInfo + p.err").textContent(), "Das letzte Update ist nicht gestartet, die vorige Firmware läuft.");
  const text = await (await fetch(BASE + "/log")).text();
  assert.match(text, /Round display ready/);
  assert.deepEqual(errors, []);
  await ctx.close();
});

test("Backflush fertig, Bezugsschalter noch an: Hinweis auf Maschine und Wartung, bis der Schalter aus ist", async ({browser}) => {
  const {page, ctx, errors} = await open(browser, BASE);
  await mock(BASE, "/__live", {state: 10, bfd: true, fp: false});
  const note = view(page).locator(".note.bfdone");
  await note.waitFor();
  assert.equal(await note.textContent(), "Backflush fertig: Bezugsschalter auf AUS stellen, Blindsieb raus und kurz spülen.");
  await tab(page, "Wartung");
  await view(page).locator('[data-card="sBf"] .note.bfdone').waitFor();
  await mock(BASE, "/__live", {state: 10, bfd: false});
  await view(page).locator('[data-card="sBf"] .note.bfdone').waitFor({state: "hidden"});
  assert.deepEqual(errors, []);
  await ctx.close();
});

test("Spülen nach dem Bezug: Hinweis, bis gespült ist; nicht während des Bezugs", async ({browser}) => {
  const {page, ctx, errors} = await open(browser, BASE);
  const note = view(page).locator(".note.rinse");
  assert.equal(await note.isVisible(), false, "no shot yet");
  await mock(BASE, "/__live", {state: 10, fp: true});
  await note.waitFor();
  assert.equal(await note.locator("span").textContent(), "Bitte spülen: Siebträger raus, Bezugsschalter 2–3 s an.");
  assert.equal(await note.locator(".btn").isVisible(), false, "the rinse button needs the water level sensor");
  await tab(page, "Bezug");
  await view(page).locator(".note.rinse").waitFor();
  await mock(BASE, "/__live", {state: 20, brewTime: 1.2, fp: true}); // the rinse itself runs: not now
  await view(page).locator(".note.rinse").waitFor({state: "hidden"});
  await mock(BASE, "/__live", {state: 10, fp: false});
  await page.waitForTimeout(1200);
  assert.equal(await view(page).locator(".note.rinse").isVisible(), false, "rinsed");
  assert.deepEqual(errors, []);
  await ctx.close();
});

test("Claude: Bohne je Bezug; neue Bohne oder Mühle fragt neu", async ({browser}) => {
  const now = Math.floor(Date.now() / 1000), form = body => fetch(BASE + "/parameters", {method: "POST", headers: {"Content-Type": "application/x-www-form-urlencoded"}, body});
  await mock(BASE, "/__shot", {s: 27.0, g: 36.0, at: now - 900, d: 18.0, m: "12", b: "Alte Bohne, hell"});
  await mock(BASE, "/__shot", {s: 24.1, g: 36.2, at: now - 300, d: 18.0, m: "11", b: "Röstwerk Nr. 5"});
  await form("brew.grinder=" + encodeURIComponent("Eureka Mignon")); await form("brew.beans=" + encodeURIComponent("Röstwerk Nr. 5"));
  let seen;
  const opts = {hash: "#brew", init: withKey, route: async pg => { seen = await fakeClaude(pg, "Passt so."); }};
  const {page, ctx} = await open(browser, BASE, opts);
  await view(page).locator("#shotList > .ai div", {hasText: "Passt so."}).waitFor();
  const user = seen[0].body.messages[0].content;
  for (const part of ["Mühle: Eureka Mignon", "Bohne jetzt in der Mühle: Röstwerk Nr. 5", "Mahlgrad 11, Bohne Röstwerk Nr. 5", "Mahlgrad 12, Bohne Alte Bohne, hell"]) assert.ok(user.includes(part), part);
  await view(page).locator("#shotList .shot").nth(1).click();
  assert.match(await view(page).locator("#shotList .curve:not([hidden]) .shotinfo").textContent(), /Mahlgrad 12 · Bohne Alte Bohne, hell/);
  // the same coffee: kept; other beans or grinder: asked anew (the answers live in this browser's storage)
  await page.reload(); await settle(page);
  await view(page).locator("#shotList > .ai div", {hasText: "Passt so."}).waitFor();
  assert.equal(seen.length, 1, "nothing new: from the browser");
  await form("brew.beans=" + encodeURIComponent("Neue Bohne"));
  await page.reload(); await settle(page);
  await view(page).locator("#shotList > .ai div", {hasText: "Passt so."}).waitFor();
  assert.equal(seen.length, 2, "other beans: asked again");
  assert.ok(seen[1].body.messages[0].content.includes("Bohne jetzt in der Mühle: Neue Bohne"));
  await form("brew.grinder=" + encodeURIComponent("Niche Zero"));
  await page.reload(); await settle(page);
  await view(page).locator("#shotList > .ai div", {hasText: "Passt so."}).waitFor();
  assert.equal(seen.length, 3, "other grinder: asked again");
  await ctx.close();
});

test("Claude: ohne Verbindung keine Vorschläge, abgelehnter Schlüssel wird getrennt", async ({browser}) => {
  await mock(BASE, "/__shot", {s: 25.3, g: 36.1, at: Math.floor(Date.now() / 1000) - 600});
  let seen;
  let p = await open(browser, BASE, {hash: "#brew", route: async pg => { seen = await fakeClaude(pg, "x"); }}); // no key
  await p.page.locator("#shotList .shot").first().waitFor(); await settle(p.page);
  assert.equal(await p.page.locator(".ai").count(), 0); assert.equal(seen.length, 0);
  await p.ctx.close();
  p = await open(browser, BASE, {hash: "#brew", init: withKey, route: async pg => { seen = await fakeClaude(pg, "x", {abort: true}); }}); // no internet
  await settle(p.page);
  assert.equal(seen.length, 1); assert.equal(await p.page.locator(".ai").count(), 0);
  assert.equal(await p.page.locator("#toast.show").count(), 0, "no error message either");
  assert.equal(await p.page.evaluate(() => localStorage.getItem("orione.claude.key")), "sk-ant-test", "kept for later");
  await p.ctx.close();
  p = await open(browser, BASE, {hash: "#brew", init: withKey, route: async pg => { seen = await fakeClaude(pg, "x", {status: 401}); }}); // key revoked
  await settle(p.page);
  assert.equal(await p.page.locator(".ai").count(), 0);
  assert.equal(await p.page.evaluate(() => localStorage.getItem("orione.claude.key")), null);
  await tab(p.page, "Einstellungen");
  await view(p.page).locator(".card.claude .err", {hasText: "abgelehnt"}).waitFor();
  await p.ctx.close();
});

test("Backflush-Erinnerung: Hinweis auf der Startseite, Zähler in Wartung", async ({browser}) => {
  await mock(BASE, "/__bf", {bf: 52});
  await mock(BASE, "/__shot", {s: 25.0, g: null, at: Math.floor(Date.now() / 1000)}); // loads the counter with the shots
  const {page, ctx} = await open(browser, BASE);
  await page.locator("#bfNote:not([hidden])", {hasText: "Backflush fällig: 52 Bezüge"}).waitFor();
  await tab(page, "Wartung");
  await page.locator("#bfSince", {hasText: "Letzter Backflush vor 52 Bezügen"}).waitFor();
  assert.equal(await row(page, "Erinnern nach").locator("input").inputValue(), "50");
  const inp = row(page, "Erinnern nach").locator("input"); // typed 2 stays 2 (+/- go in fives)
  await inp.fill("2"); await inp.press("Enter"); await settle(page);
  assert.equal((await values(BASE))["backflush.remind_after"], 2);
  await ctx.close();
});

test("Wartung: Spülen ohne Wasserstandssensor gesperrt, Backflush-Zähler bleibt bei Backflush", async ({browser}) => {
  await mock(BASE, "/__bf", {bf: 7});
  const {page, ctx} = await open(browser, BASE, {hash: "#care"});
  const cards = page.locator("section.card[data-card]");
  assert.deepEqual(await cards.evaluateAll(cs => cs.map(c => c.dataset.card)), ["sFlush", "sBf", "sDescale", "sDrip", "sCount"]);
  assert.equal(await page.locator("#flushBtn").isDisabled(), true);
  assert.match(await page.locator("#flushSt").textContent(), /^Nur mit Wasserstandssensor/);
  assert.equal(await row(page, "Nach dem Kaltstart automatisch").count(), 0, "no switch without the sensor");
  await page.locator('[data-card="sBf"] #bfSince', {hasText: "vor 7 Bezügen"}).waitFor(); // still in the backflush card
  assert.equal(await (await fetch(BASE + "/flush?start=1", {method: "POST"})).status, 409, "the firmware refuses too");
  await ctx.close();
});

test("Wartung: Spülen von Hand, Fortschritt, abbrechen; Hinweis auf der Startseite", async ({browser}) => {
  await fetch(BASE + "/parameters", {method: "POST", headers: {"Content-Type": "application/x-www-form-urlencoded"}, body: "hardware.sensors.watertank.enabled=1"});
  await mock(BASE, "/__live", {state: 10, warmup: 0});
  const {page, ctx, errors} = await open(browser, BASE, {hash: "#care"});
  const btn = page.locator("#flushBtn"), st = page.locator("#flushSt");
  await page.locator("#flushBtn:not([disabled])", {hasText: "Jetzt spülen"}).waitFor();
  assert.equal(await row(page, "Nach dem Kaltstart automatisch").count(), 1);
  await btn.click(); await settle(page);
  assert.deepEqual((await posts(BASE)).filter(p => p.path === "/flush").map(p => p.query), ["start=1"]);
  await mock(BASE, "/__live", {state: 25, warmup: 2, pulse: 1});
  await page.locator("#flushBtn", {hasText: "Spülen abbrechen"}).waitFor();
  assert.equal(await st.textContent(), "Spülstoß 1 von 3");
  await mock(BASE, "/__live", {state: 25, warmup: 3, pulse: 1});
  await page.locator("#flushSt", {hasText: "Pause, gleich Spülstoß 2 von 3"}).waitFor();
  await page.screenshot({path: OUT + "care-flush.png", fullPage: true});
  await btn.click(); await settle(page);
  assert.deepEqual((await posts(BASE)).filter(p => p.path === "/flush").map(p => p.query), ["start=1", "stop=1"]);
  await mock(BASE, "/__live", {state: 50, warmup: 4}); // backflush mode: no flush
  await page.locator("#flushBtn[disabled]", {hasText: "Jetzt spülen"}).waitFor();
  assert.match(await st.textContent(), /^Geht, sobald die Maschine bereit ist/);
  // start page: the note while the automatic one is pending, the pulse in the state while it runs
  await mock(BASE, "/__live", {state: 10, warmup: 1});
  await tab(page, "Maschine");
  await page.locator("#flushNote:not([hidden])", {hasText: "Spült gleich automatisch"}).waitFor();
  await mock(BASE, "/__live", {state: 25, warmup: 2, pulse: 2});
  await page.locator("#flushNote[hidden]").waitFor({state: "attached"});
  await page.locator("#state", {hasText: "Spülen 2/3"}).waitFor();
  assert.deepEqual(errors, []);
  await ctx.close();
});

test("iPhone-Home-Bildschirm: Kopfzeile unter der Statusleiste, nichts läuft unter der Uhr durch", async ({browser}) => {
  const {page, ctx} = await open(browser, BASE);
  const top = () => page.locator("header").evaluate(h => Math.round(h.querySelector(".brand").getBoundingClientRect().top));
  const plain = await top();
  // what iOS reports in a home screen app on an iPhone with a notch / Dynamic Island (59 pt top, 47 pt in landscape at the sides)
  await page.evaluate(() => { const s = document.documentElement.style; s.setProperty("--st", "59px"); s.setProperty("--sl", "47px"); s.setProperty("--sr", "47px"); });
  assert.equal(await top(), plain + 59, "the header moves down by the status bar");
  assert.equal(await page.evaluate(() => getComputedStyle(document.body, "::before").height), "59px", "a bar covers the status bar");
  assert.equal(await page.evaluate(() => getComputedStyle(document.body).paddingLeft), "63px", "clear of the notch in landscape");
  await page.evaluate(() => { const s = document.documentElement.style; s.setProperty("--sl", "0px"); s.setProperty("--sr", "0px"); });
  await page.screenshot({path: OUT + "iphone-safe-area.png"});
  await page.evaluate(() => scrollTo(0, 400));
  const covered = await page.evaluate(() => { const e = document.elementFromPoint(195, 20); return e === document.body || e === document.documentElement; });
  assert.ok(covered, "scrolled content stays under the bar, not on top of it");
  await ctx.close();
});

test("iPhone-Home-Bildschirm: Abstand unten nur einmal", async ({browser}) => {
  const {page, ctx} = await open(browser, BASE);
  const pad = (...a) => page.evaluate(a => bottomPad(...a), a);
  assert.equal(await pad(34, 59, 852, 852, 393, false), 34, "browser: the inset as iOS reports it");
  assert.equal(await pad(34, 59, 852, 852, 393, true), 34, "app over the whole screen: once");
  assert.equal(await pad(34, 59, 852, 818, 393, true), 0, "app stopping above the home indicator: not again");
  assert.equal(await pad(34, 0, 852, 798, 393, true), 34, "app below the status bar (shorter at the top): still needed");
  assert.equal(await pad(21, 0, 852, 393, 852, true), 21, "landscape: as reported");
  await page.evaluate(() => document.documentElement.style.setProperty("--sb", "0px"));
  const navH = await page.locator("nav").evaluate(n => Math.round(n.getBoundingClientRect().height));
  assert.ok(navH <= 63, `the bar itself stays slim (${navH} px)`);
  await ctx.close();
});

test("Reiterleiste klebt unten, auf kurzen und langen Seiten, auch beim Scrollen; am Rechner oben", async ({browser}) => {
  const {page, ctx} = await open(browser, BASE);
  const gap = () => page.evaluate(() => Math.round(innerHeight - document.querySelector("nav").getBoundingClientRect().bottom));
  const fullWidth = () => page.evaluate(() => { const r = document.querySelector("nav").getBoundingClientRect(); return r.left === 0 && Math.round(r.right) === innerWidth; });
  assert.equal(await gap(), 0, "short page: at the bottom");
  assert.ok(await fullWidth(), "from edge to edge");
  for (const t of ["Einstellungen", "Wartung"]) {
    await tab(page, t);
    assert.ok(await page.evaluate(() => document.documentElement.scrollHeight > innerHeight + 200), t + " is longer than the screen");
    assert.equal(await gap(), 0, t + ": at the bottom before scrolling");
    await page.evaluate(() => scrollTo(0, 99999));
    assert.equal(await gap(), 0, t + ": and at the end");
    const last = await page.evaluate(() => { const c = [...document.querySelectorAll("main > div:not([hidden]) section.card")].pop().getBoundingClientRect(); return Math.round(document.querySelector("nav").getBoundingClientRect().top - c.bottom); });
    assert.ok(last >= 8, t + `: the last card is not under the bar (${last} px)`);
    await page.evaluate(() => scrollTo(0, 0));
  }
  await page.setViewportSize({width: 1024, height: 800});
  const order = await page.evaluate(() => document.querySelector("nav").getBoundingClientRect().bottom <= document.querySelector("main").getBoundingClientRect().top);
  assert.ok(order, "wide screen: the tabs under the header, above the content");
  await ctx.close();
});

test("Waage wählen: suchen, verbinden, vergessen; Hinweis im Reiter Bezug", async ({browser}) => {
  await fetch(BASE + "/scale/forget", {method: "POST"});
  const {page, ctx, errors} = await open(browser, BASE, {hash: "#brew"});
  await page.locator("#scaleSt:not(.on)", {hasText: "Keine Waage gewählt – unter Einstellungen → Waage suchen"}).waitFor();
  await tab(page, "Einstellungen");
  const now = page.locator("#scaleNow");
  await page.locator("#scaleNow", {hasText: "Keine Waage gewählt"}).waitFor();
  assert.equal(await page.locator("#scaleForget").isHidden(), true, "nothing to forget");
  await page.locator("#scaleFind").click();
  await page.locator("#scaleFind[disabled]", {hasText: "Suche"}).waitFor();
  const rows = page.locator("#scaleFound .foundscale");
  await rows.nth(1).waitFor();
  assert.deepEqual(await rows.locator(".lbl > div").allTextContents(), ["BOOKOO_SC U 1234", "BOOKOO_SC 5678"]);
  assert.deepEqual(await rows.locator(".lbl p").allTextContents(), ["Signal gut", "Signal schwach"]);
  await page.locator("#scaleFind:not([disabled])", {hasText: "Waage suchen"}).waitFor({timeout: 20000});
  await page.screenshot({path: OUT + "settings-scale-found.png", fullPage: true});
  await rows.nth(0).locator("button", {hasText: "Verbinden"}).click();
  await page.locator("#scaleNow.on", {hasText: "Verbunden mit BOOKOO_SC U 1234 · Akku 76 %"}).waitFor();
  await page.locator("#scaleFind").waitFor({state: "hidden"}); // the machine does not search while connected
  assert.match(await page.locator("#scaleHint").textContent(), /erst „Vergessen“/);
  const sel = (await posts(BASE)).filter(p => p.path === "/scale/select").map(p => p.query);
  assert.deepEqual(sel, ["address=c8%3A2e%3A18%3Aaa%3A01%3A02&name=BOOKOO_SC%20U%201234"]);
  assert.equal(await rows.count(), 0, "the list goes once one is chosen");
  await tab(page, "Bezug");
  await page.locator("#scaleSt.on", {hasText: "Waage verbunden"}).waitFor();
  await tab(page, "Einstellungen");
  await page.locator("#scaleForget").click();
  await page.locator("#scaleNow:not(.on)", {hasText: "Keine Waage gewählt"}).waitFor();
  assert.ok((await posts(BASE)).some(p => p.path === "/scale/forget"));
  // nothing around
  await mock(BASE, "/__live", {noScales: true});
  await page.locator("#scaleFind").click();
  await page.locator("#scaleHint", {hasText: "Keine Waage gefunden"}).waitFor({timeout: 20000});
  assert.deepEqual(errors, []);
  await ctx.close();
});

test("Nach Gewicht: Nachlauf und Abweichung vom Ziel, Nachlaufzeit gelernt", async ({browser}) => {
  const now = Math.floor(Date.now() / 1000);
  await mock(BASE, "/__shot", {s: 27.0, g: 38.5, at: now - 900, tw: 36.0, sw: 35.1, ld: 1.5});
  await mock(BASE, "/__shot", {s: 25.6, g: 36.4, at: now - 300, tw: 36.0, sw: 34.6, ld: 1.8, lg: 1.0, fs: 1.8, d: 18.0});
  await fetch(BASE + "/parameters", {method: "POST", headers: {"Content-Type": "application/x-www-form-urlencoded"}, body: "brew.mode=1"});
  await fetch(BASE + "/parameters", {method: "POST", headers: {"Content-Type": "application/x-www-form-urlencoded"}, body: "brew.by_weight.enabled=1&brew.by_time.enabled=0"});
  let seen;
  const {page, ctx, errors} = await open(browser, BASE, {hash: "#brew", init: withKey, route: async pg => { seen = await fakeClaude(pg, () => "Passt."); }});
  const rows = view(page).locator("#shotList .shot");
  await rows.nth(1).waitFor();
  assert.equal(await rows.nth(0).locator(".dev").textContent(), "+0,4");
  assert.equal(await rows.nth(0).locator(".dev.off").count(), 0, "within a gram");
  assert.equal(await rows.nth(1).locator(".dev.off").textContent(), "+2,5", "over a gram: marked");
  await rows.nth(0).click();
  const info = view(page).locator("#shotList .curve:not([hidden]) .shotinfo");
  await info.waitFor();
  assert.match(await info.textContent(), /Ziel 36,0 g · Pumpe aus bei 34,6 g \(1,8 g = 1,00 s × 1,8 g\/s\) · Nachlauf 1,8 g · \+0,4 g über Ziel$/);
  await view(page).locator("#shotList .curve:not([hidden]) canvas").waitFor();
  await page.screenshot({path: OUT + "brew-weight-drops.png", fullPage: true});
  // the shot card: stop by weight shows the drop time (folded: the machine learns it), and what it means in grams at the last shot's flow
  assert.equal(await row(page, "Nachlaufzeit").isVisible(), false, "folded");
  await view(page).locator("summary", {hasText: "Feineinstellung"}).click();
  assert.equal(await row(page, "Nachlaufzeit").locator("input").inputValue(), "1,00 s", "stop by weight: the drop time");
  assert.equal(await page.locator("#lagNow").textContent(), "Letzter Bezug: 1,8 g/s, also 1,8 g vor dem Ziel");
  await row(page, "Nachlaufzeit").locator("button", {hasText: "+"}).click();
  await settle(page);
  assert.equal((await values(BASE))["brew.by_weight.lag"], 1.05, "in steps of 0.05 s");
  assert.equal(await page.locator("#lagNow").textContent(), "Letzter Bezug: 1,8 g/s, also 1,9 g vor dem Ziel");
  // live: stopped at 34.6 g, the drops bring it to 36.4 g
  await mock(BASE, "/__live", {state: 20, brewTime: 25.6, weight: 34.6, scale: 2, flow: 2.1});
  await page.locator("#lsLab", {hasText: "Bezug läuft"}).waitFor();
  // the machine logs the shot and learns from it (src/shotHistory.h): the page shows the new drop time on its own
  await mock(BASE, "/__shot", {s: 25.6, g: 36.6, at: now - 5, tw: 36.0, sw: 34.4, ld: 1.9, lg: 1.05, fs: 1.8});
  await mock(BASE, "/__param", {"brew.by_weight.lag": 0.88});
  await mock(BASE, "/__live", {state: 10, brewTime: 25.6, weight: 0.4, scale: 2, cup: 36.4});
  await page.locator("#lsGoal", {hasText: "Ziel 36 g · Pumpe aus bei 34,6 g · +0,4 g"}).waitFor();
  assert.equal(await page.locator("#lsWeight").textContent(), "36,4 g");
  await page.waitForFunction(() => document.querySelector('.step[data-id="brew.by_weight.lag"] input')?.value === "0,88 s", null, {timeout: 9000});
  assert.equal(await page.locator("#lagNow").textContent(), "Letzter Bezug: 1,8 g/s, also 1,6 g vor dem Ziel");
  const ctxText = seen.length ? seen[0].body.messages[0].content : "";
  assert.ok(ctxText.includes("Pumpe aus bei 34,6 g (1,8 g vor dem Ziel: gelernte Nachlaufzeit 1,00 s × Durchfluss beim Stopp 1,8 g/s), Nachlauf 1,8 g, +0,4 g zum Ziel"), "Claude gets the drops: " + ctxText);
  assert.deepEqual(errors, []);
  await ctx.close();
});

test("Fertig bleibt, solange der Bezugsschalter noch an ist; danach die Displayzeit; Hinweis nach 1 min", async ({browser}) => {
  const {page, ctx, errors} = await open(browser, BASE, {hash: "#brew"});
  await page.clock.install();
  await mock(BASE, "/__live", {state: 20, brewTime: 26.4, weight: 35.0, scale: 2});
  await page.locator("#lsLab", {hasText: "Bezug läuft"}).waitFor();
  await mock(BASE, "/__live", {state: 10, brewTime: 26.4, weight: 0.5, scale: 2, cup: 36.6, held: true});
  await page.locator("#lsLab", {hasText: "Fertig"}).waitFor();
  await page.clock.fastForward(45000);
  await page.waitForTimeout(800);
  assert.equal(await page.locator("#liveShot").isVisible(), true, "45 s later, switch still on: still there");
  assert.equal(await page.locator("#lsHint").isHidden(), true, "no reminder yet");
  await page.clock.fastForward(20000);
  await page.locator("#lsHint", {hasText: "Bezugsschalter wieder auf AUS stellen"}).waitFor();
  await mock(BASE, "/__live", {state: 10, brewTime: 26.4, weight: 0.5, scale: 2, held: false}); // switched off
  await page.waitForTimeout(800);
  await page.clock.fastForward(8000);
  await page.waitForTimeout(800);
  assert.equal(await page.locator("#liveShot").isVisible(), true, "8 s after switching off: still shown (10 s)");
  await page.clock.fastForward(4000);
  await page.locator("#liveShot").waitFor({state: "hidden"});
  assert.deepEqual(errors, []);
  await ctx.close();
});

test("Startseite nach Gewicht: Rad zeigt Ziel, danach die Abweichung, nach 1 min den Schalter-Hinweis", async ({browser}) => {
  await fetch(BASE + "/parameters", {method: "POST", headers: {"Content-Type": "application/x-www-form-urlencoded"}, body: "brew.mode=1"});
  await fetch(BASE + "/parameters", {method: "POST", headers: {"Content-Type": "application/x-www-form-urlencoded"}, body: "brew.by_weight.enabled=1&brew.by_time.enabled=0"});
  const {page, ctx, errors} = await open(browser, BASE);
  await page.clock.install();
  // pixels of a colour in the dial's bottom row (y 186-208 of 240), where the display has it
  const bottom = rgb => page.evaluate(([r, g, b]) => {
    const c = document.querySelector("#dial"), k = c.width / 240, d = c.getContext("2d").getImageData(0, Math.round(186 * k), c.width, Math.round(22 * k)).data;
    let n = 0; for (let i = 0; i < d.length; i += 4) n += Math.abs(d[i] - r) < 30 && Math.abs(d[i + 1] - g) < 30 && Math.abs(d[i + 2] - b) < 30; return n;
  }, rgb);
  const READY = [52, 211, 153], HEAT = [255, 146, 38], DIM = [142, 142, 148];
  await mock(BASE, "/__live", {state: 20, brewTime: 20.1, weight: 28.0, scale: 2});
  await page.locator("#dialCard:not([hidden])").waitFor();
  await page.waitForTimeout(500);
  assert.ok(await bottom(DIM) > 50, "while brewing: the target, dim");
  await mock(BASE, "/__live", {state: 10, brewTime: 26.4, weight: 0.5, scale: 2, cup: 36.6, held: true});
  await page.waitForTimeout(1500);
  assert.ok(await bottom(READY) > 50, "done 0,6 g over: green");
  await page.screenshot({path: OUT + "status-dial-weight-done.png"});
  await page.clock.fastForward(65000);
  await page.waitForTimeout(1500);
  assert.ok(await bottom(HEAT) > 50 && await bottom(READY) < 10, "a minute later, switch still on: the reminder");
  await page.screenshot({path: OUT + "status-dial-remind.png"});
  assert.deepEqual(errors, []);
  await ctx.close();
});

test("Bezüge: Bewerten trifft den richtigen, auch wenn inzwischen einer dazukam; zurück aus dem Hintergrund neu geladen", async ({browser}) => {
  const now = Math.floor(Date.now() / 1000);
  await mock(BASE, "/__shot", {s: 25.3, g: 36.1, at: now - 600});
  const {page, ctx, errors} = await open(browser, BASE, {hash: "#brew"});
  const rows = view(page).locator("#shotList .shot");
  await rows.first().waitFor();
  await rows.first().click();
  await view(page).locator("#shotList .curve:not([hidden]) .taste").waitFor();
  await mock(BASE, "/__shot", {s: 27.0, g: 38.0, at: now - 60}); // made while the page did not look
  await view(page).locator("#shotList .curve:not([hidden]) .taste button", {hasText: "sauer"}).click();
  await rows.nth(1).waitFor();
  await settle(page);
  const after = (await mock(BASE, "/shots")).shots;
  assert.deepEqual(after.map(x => [x.s, x.r || 0]), [[27.0, 0], [25.3, 1]], "rated the 25,3 s shot, now in place 1");
  // phone locked, a shot meanwhile, phone back
  const hidden = on => page.evaluate(h => { Object.defineProperty(document, "hidden", {value: h, configurable: true}); document.dispatchEvent(new Event("visibilitychange")); }, on);
  await hidden(true);
  await mock(BASE, "/__shot", {s: 24.0, g: 35.0, at: now - 10});
  await page.waitForTimeout(1500);
  assert.equal(await rows.count(), 2, "hidden: nothing loaded");
  await hidden(false);
  await rows.nth(2).waitFor({timeout: 5000});
  assert.equal(await rows.first().locator("b").textContent(), "24,0 s");
  assert.deepEqual(errors, []);
  await ctx.close();
});

test("Standby: der Regelungs-Schalter folgt der Maschine", async ({browser}) => {
  const {page, ctx, errors} = await open(browser, BASE);
  await page.locator("#pidSw.on").waitFor();
  await mock(BASE, "/__live", {state: 80});
  await page.locator("#pidSw:not(.on)").waitFor();
  assert.equal(await page.locator("#pidSw").getAttribute("aria-checked"), "false");
  await mock(BASE, "/__live", {state: 10}); // woken (brew switch)
  await page.locator("#pidSw.on").waitFor();
  await mock(BASE, "/__live", {state: 80});
  await page.locator("#pidSw:not(.on)").waitFor();
  await page.locator("#pidSw").click(); // one tap wakes it
  await settle(page);
  assert.equal((await values(BASE))["pid.enabled"], 1);
  assert.deepEqual(errors, []);
  await ctx.close();
});

test("Reiter, der nicht laden konnte: Hinweis, beim nächsten Öffnen neu geladen", async ({browser}) => {
  let refuse = true; // busy for longer than the page retries: the start tab and the other tabs in the background fail
  const {page, ctx} = await open(browser, BASE, {route: pg => pg.route("**/parameters?*", r => refuse ? r.fulfill({status: 503, body: "busy"}) : r.continue())});
  await page.waitForFunction(() => document.querySelector("#toast")?.textContent.includes("Nicht alles geladen"), null, {timeout: 20000});
  assert.equal(await page.locator("#pidSw").count(), 0, "built from what came");
  refuse = false;
  await tab(page, "Bezug");
  await row(page, "Stoppen").waitFor({timeout: 30000});
  await tab(page, "Maschine");
  await page.locator("#pidSw").waitFor({timeout: 15000});
  await ctx.close();
});

const setp = body => fetch(BASE + "/parameters", {method: "POST", headers: {"Content-Type": "application/x-www-form-urlencoded"}, body});

test("Nach Gewicht ohne Waage: Stopp nach Zeit, angezeigt und einstellbar; Obergrenze genannt", async ({browser}) => {
  await setp("brew.mode=1"); await setp("brew.by_weight.enabled=1&brew.by_time.enabled=0");
  const {page, ctx, errors} = await open(browser, BASE, {hash: "#brew"});
  assert.match(await row(page, "Stoppen").locator(".lbl p").last().textContent(), /^Jeder Bezug endet spätestens nach 60 s\.$/);
  await view(page).locator("summary", {hasText: "Feineinstellung"}).click();
  const fb = row(page, "Ohne Waage nach"); // the label says it all, no hint under it
  await fb.locator("button", {hasText: "+"}).click();
  await settle(page);
  assert.equal((await values(BASE))["brew.by_time.target_time"], 25.5);
  await mock(BASE, "/__live", {state: 20, brewTime: 8.2, scale: 1});
  await page.locator("#lsGoal", {hasText: "Waage nicht verbunden – Ziel 25,5 s"}).waitFor();
  await mock(BASE, "/__live", {state: 20, brewTime: 12.0, scale: 2, weight: 14.0}); // back during the shot: still by time
  await page.waitForTimeout(800);
  assert.match(await page.locator("#lsGoal").textContent(), /^Waage nicht verbunden – Ziel 25,5 s/);
  await mock(BASE, "/__live", {state: 20, brewTime: 1.0, scale: 2, weight: 0.3}); // the next shot (time went back): with the scale
  await mock(BASE, "/__live", {state: 10, brewTime: 1.0, scale: 2});
  await page.waitForTimeout(800);
  await mock(BASE, "/__live", {state: 20, brewTime: 2.0, scale: 2, weight: 0.5});
  await page.locator("#lsGoal", {hasText: /^Ziel 36 g/}).waitFor();
  assert.deepEqual(errors, []);
  await ctx.close();
});

test("Bezug löschen: nach Rückfrage, der richtige, auch wenn inzwischen einer dazukam", async ({browser}) => {
  const now = Math.floor(Date.now() / 1000);
  await mock(BASE, "/__shot", {s: 120.0, g: 0.0, at: now - 900}); // a test at the bench
  await mock(BASE, "/__shot", {s: 25.3, g: 36.1, at: now - 600});
  const {page, ctx, errors} = await open(browser, BASE, {hash: "#brew"});
  const rows = view(page).locator("#shotList .shot");
  await rows.nth(1).click();
  const del = view(page).locator("#shotList .curve:not([hidden]) button", {hasText: "Bezug löschen"});
  await del.waitFor();
  await mock(BASE, "/__shot", {s: 27.0, g: 38.0, at: now - 60}); // made meanwhile
  await del.click(); // Playwright says "no" to the question
  await page.waitForTimeout(600);
  assert.equal((await mock(BASE, "/shots")).shots.length, 3, "not confirmed: nothing deleted");
  let asked = "";
  page.once("dialog", d => { asked = d.message(); d.accept(); });
  await del.click();
  await toast(page, "Gelöscht");
  await settle(page);
  assert.equal(asked, "Diesen Bezug (120,0 s · 0,0 g) löschen?");
  assert.deepEqual((await mock(BASE, "/shots")).shots.map(x => x.s), [27.0, 25.3]);
  assert.deepEqual(await rows.locator("b").allTextContents(), ["27,0 s", "25,3 s"]);
  assert.equal(await view(page).locator("#shotList .curve:not([hidden])").count(), 0, "its curve closed");
  assert.equal((await fetch(BASE + `/shot/delete?i=0&at=${now - 600}&s=25.3`, {method: "POST"})).status, 409, "the machine checks which shot");
  assert.deepEqual(errors, []);
  await ctx.close();
});

test("Wartung: Spülen gesperrt, solange der Bezugsschalter an ist", async ({browser}) => {
  await setp("hardware.sensors.watertank.enabled=1");
  await mock(BASE, "/__live", {state: 10, warmup: 0});
  const {page, ctx, errors} = await open(browser, BASE, {hash: "#care"});
  await page.locator("#flushBtn:not([disabled])").waitFor();
  await mock(BASE, "/__live", {state: 10, warmup: 0, sw: true});
  await page.locator("#flushBtn[disabled]").waitFor();
  assert.equal(await page.locator("#flushSt").textContent(), "Erst den Bezugsschalter auf AUS stellen.");
  await mock(BASE, "/__live", {state: 10, warmup: 0, sw: false});
  await page.locator("#flushBtn:not([disabled])").waitFor();
  assert.equal(await page.locator("#flushSt").textContent(), "");
  assert.deepEqual(errors, []);
  await ctx.close();
});

test("Backflush: nach dem vollständigen Backflush ist der Modus aus, auch auf der Seite", async ({browser}) => {
  const {page, ctx, errors} = await open(browser, BASE, {hash: "#care"});
  const sw = () => row(page, "Backflush-Modus").locator(".sw");
  await sw().click();
  await settle(page);
  assert.match(await sw().getAttribute("class"), /\bon\b/);
  await mock(BASE, "/__live", {state: 50});
  await page.waitForTimeout(800);
  await mock(BASE, "/toggleBackflush", ""); // the firmware switches it off after the last cycle ...
  await mock(BASE, "/__live", {state: 10}); // ... and the machine is ready again
  for (let k = 0; k < 50 && /\bon\b/.test(await sw().getAttribute("class")); k++) await page.waitForTimeout(100);
  assert.doesNotMatch(await sw().getAttribute("class"), /\bon\b/, "the page follows the machine");
  await tab(page, "Maschine");
  assert.equal(await page.locator(".note", {hasText: "Backflush-Modus aktiv"}).count(), 0);
  await tab(page, "Wartung");
  await sw().click(); // on again: one toggle, not two
  await settle(page);
  assert.equal((await values(BASE))["BACKFLUSH_ON"], 1);
  assert.deepEqual(errors, []);
  await ctx.close();
});

test("Dampf vom Originalschalter: Status Dampf, danach Abkühlen mit Hinweis", async ({browser}) => {
  const {page, ctx, errors} = await open(browser, BASE);
  await mock(BASE, "/__live", {state: 10, currentTemp: 124.0, steam: 1});
  await page.locator("#state", {hasText: "Dampf"}).waitFor();
  assert.equal(await page.locator("#alarm").isHidden(), true, "steam itself is no alarm");
  await mock(BASE, "/__live", {state: 10, currentTemp: 101.0, steam: 2});
  await page.locator("#state", {hasText: "Abkühlen nach Dampf"}).waitFor();
  await page.locator("#alarm.info", {hasText: "Nach dem Dampf zu heiß für Espresso"}).waitFor();
  assert.match(await page.locator("#alarm").textContent(), /Spülbezug/);
  await mock(BASE, "/__live", {state: 10, currentTemp: 93.0, steam: 0});
  await page.locator("#alarm").waitFor({state: "hidden"});
  assert.doesNotMatch(await page.locator("#state").textContent(), /Dampf/);
  assert.deepEqual(errors, []);
  await ctx.close();
});

test("Wartung: Laufzeit und Grund des letzten Starts", async ({browser}) => {
  await mock(BASE, "/__live", {bootReason: "brownout"});
  const {page, ctx, errors} = await open(browser, BASE, {hash: "#care"});
  await page.locator("#bootInfo", {hasText: "Läuft seit 2 h 3 min · letzter Start: Unterspannung"}).waitFor();
  assert.deepEqual(errors, []);
  await ctx.close();
});

test("Regler: Heizen beim Bezug einstellbar", async ({browser}) => {
  const {page, ctx, errors} = await open(browser, BASE, {hash: "#settings"});
  await view(page).locator("summary", {hasText: "Fortgeschritten"}).click();
  const r = row(page, "Heizen beim Bezug");
  assert.equal(await r.locator("input").inputValue(), "60 %");
  assert.match(await r.locator(".lbl p").textContent(), /solange die Pumpe läuft/);
  await r.locator("button", {hasText: "+"}).click();
  await settle(page);
  assert.equal((await values(BASE))["brew.heat_boost"], 65);
  assert.deepEqual(errors, []);
  await ctx.close();
});

test("App-Symbol für den Home-Bildschirm", async ({browser}) => {
  const {page, ctx} = await open(browser, BASE);
  assert.equal(await page.locator("link[rel=manifest]").getAttribute("href"), "/manifest.json");
  const m = await (await fetch(BASE + "/manifest.json")).json();
  assert.equal(m.display, "standalone"); assert.equal(m.icons.length, 2);
  for (const f of ["/icon-192.png", "/icon-512.png", "/apple-touch-icon.png"]) {
    const r = await fetch(BASE + f); assert.equal(r.status, 200, f); assert.equal(r.headers.get("content-type"), "image/png");
  }
  await ctx.close();
});

test("Startseite zeigt beim Bezug, was das Display zeigt; danach FERTIG", async ({browser}) => {
  const {page, ctx} = await open(browser, BASE);
  await page.evaluate(() => document.fonts.ready);
  assert.equal(await page.evaluate(() => getComputedStyle(document.body).fontFamily.split(",")[0].replace(/"/g, "")), "Barlow SC");
  assert.ok(await page.evaluate(() => document.fonts.check("500 16px 'Barlow SC'") && document.fonts.check("600 16px 'Barlow SC'")), "both weights loaded");
  assert.equal(await page.locator("#dialCard").isHidden(), true);
  await mock(BASE, "/__live", {state: 20, brewTime: 12.3, weight: null, scale: 0});
  await page.locator("#dialCard:not([hidden])").waitFor();
  assert.equal(await page.locator("#tNow").isVisible(), false, "the temperature makes room");
  const painted = await page.evaluate(() => { const c = document.querySelector("#dial"); const d = c.getContext("2d").getImageData(0, 0, c.width, c.height).data; let n = 0; for (let i = 0; i < d.length; i += 4) n += d[i] > 200 && d[i + 1] > 140; return n; });
  assert.ok(painted > 500, "ring and digits drawn: " + painted);
  await page.screenshot({path: OUT + "status-dial.png"});
  await mock(BASE, "/__live", {brewTime: 12.8});
  await page.waitForTimeout(1500);
  await page.screenshot({path: OUT + "status-dial-done.png"});
  assert.equal(await page.locator("#dialCard").isHidden(), false, "the result stays a moment");
  await ctx.close();
});

test("Übersicht: letzter Bezug mit Bewertung und Zählern, Statuszeile; Tipp öffnet ihn im Reiter Bezug", async ({browser}) => {
  const now = Math.floor(Date.now() / 1000);
  await setp("standby.enabled=1&schedule.enabled=1&schedule.on=390");
  await mock(BASE, "/__shot", {s: 24.0, g: 35.0, at: now - 3600, d: 18.0, m: "13"});
  await mock(BASE, "/__shot", {s: 25.3, g: 36.4, at: now - 600, d: 18.0, m: "12", tw: 36.0, sw: 34.6});
  const {page, ctx, errors} = await open(browser, BASE);
  const card = view(page).locator("#lastCard");
  await card.locator(".lastnums").waitFor();
  assert.deepEqual(await card.locator(".lastnums > *").allTextContents(), ["25,3 s", "36,4 g", "1:2,0", "+0,4 g"]);
  assert.match(await card.locator(".lasthead small").textContent(), /^heute /);
  await card.locator(".shotinfo", {hasText: /^3 heute · 12 diese Woche$/}).waitFor();
  assert.equal(await view(page).locator("#careCard .shotinfo").textContent(), "Backflush in 50 Bezügen · Entkalken in ≈\u00a028\u00a0l · Tropfschale ≈\u00a0230\u00a0ml");
  const pills = await view(page).locator("#pills span").allTextContents();
  assert.ok(pills.includes("Waage · 76 %"), pills.join(" | "));
  assert.ok(pills.some(x => /^Ein (heute|morgen) 06:30$/.test(x)), pills.join(" | "));
  assert.equal(await view(page).locator("#pidState").textContent(), "an · Standby in 23 min");
  assert.ok(pills.includes("WLAN −71 dBm"), pills.join(" | "));
  await card.locator(".taste button", {hasText: "passt"}).click();
  await card.locator(".taste button.on", {hasText: "passt"}).waitFor();
  await settle(page);
  assert.equal((await mock(BASE, "/shots")).shots[0].r, 2, "rated the newest");
  await page.screenshot({path: OUT + "status-overview.png", fullPage: true});
  await card.locator(".lasthead").click();
  await page.locator("nav button.on", {hasText: "Bezug"}).waitFor();
  await view(page).locator("#shotList .shot.open").first().waitFor();
  assert.match(await view(page).locator("#shotList .shot.open b").textContent(), /^25,3 s$/);
  await view(page).locator("#shotList .curve:not([hidden]) canvas").waitFor();
  assert.deepEqual(errors, []);
  await ctx.close();
});

test("Zeitplan: einschalten, Uhrzeiten und Tage speichern, nächstes Einschalten auf der Übersicht", async ({browser}) => {
  const {page, ctx, errors} = await open(browser, BASE, {hash: "#settings"});
  assert.equal(await row(page, "Ein um").count(), 0, "times only while the schedule is on");
  await row(page, "Automatisch einschalten").locator(".sw").click();
  await row(page, "Ein um").waitFor();
  await settle(page);
  assert.equal((await values(BASE))["schedule.enabled"], 1);
  assert.equal(await row(page, "Ein um").locator("input").inputValue(), "06:30");
  assert.equal(await row(page, "Aus um").locator("input").inputValue(), "", "no off time");
  await row(page, "Ein um").locator("input").fill("06:45"); await settle(page);
  await row(page, "Aus um").locator("input").fill("22:00"); await settle(page);
  let v = await values(BASE);
  assert.equal(v["schedule.on"], 405); assert.equal(v["schedule.off"], 1320);
  await row(page, "Aus um").locator("input").fill(""); await settle(page);
  assert.equal((await values(BASE))["schedule.off"], 1440, "cleared: no off time");
  const days = view(page).locator(".days button");
  assert.deepEqual(await days.allTextContents(), ["Mo", "Di", "Mi", "Do", "Fr", "Sa", "So"]);
  await days.nth(5).click(); await settle(page);
  await days.nth(6).click(); await settle(page);
  assert.equal((await values(BASE))["schedule.days"], 31, "Monday to Friday");
  assert.equal(await days.nth(6).getAttribute("aria-pressed"), "false");
  const b = await days.first().boundingBox();
  assert.ok(b.height >= 44, `day button ${b.height} px high`);
  await page.screenshot({path: OUT + "settings-schedule.png", fullPage: true});
  await tab(page, "Maschine");
  assert.ok((await view(page).locator("#pills span").allTextContents()).some(x => /^Ein .+ 06:45$/.test(x)));
  assert.deepEqual(errors, []);
  await ctx.close();
});

test("Entkalken: Wasser seit dem Entkalken, Grenze, Entkalkt nach Rückfrage; fällig auf der Übersicht", async ({browser}) => {
  await mock(BASE, "/__care", {water: 41200});
  const {page, ctx, errors} = await open(browser, BASE);
  await page.locator("#descaleNote:not([hidden])", {hasText: "Entkalken fällig (≈ 41 l)"}).waitFor();
  await tab(page, "Wartung");
  assert.equal(await page.locator("#descaleSt").textContent(), "≈ 41 l bisher gezählt");
  assert.equal(await row(page, "Erinnern bei").locator("input").inputValue(), "40 l");
  assert.deepEqual(await view(page).locator("#stats small").allTextContents(), ["heute", "Woche", "gesamt", "Kaffee"]);
  assert.deepEqual(await view(page).locator("#stats b").allTextContents(), ["3", "12", "245", "4,4 kg"]);
  await row(page, "Erinnern bei").locator("button", {hasText: "+"}).click();
  await settle(page);
  assert.equal((await values(BASE))["descale.litres"], 45);
  await page.locator("#descaleNote").waitFor({state: "hidden"}); // 41 of 45 l: not due any more
  let asked = 0;
  page.once("dialog", d => { asked++; d.dismiss(); });
  await view(page).locator("button", {hasText: "Entkalkt"}).click();
  await settle(page);
  assert.equal(asked, 1);
  assert.equal((await posts(BASE)).filter(x => x.path === "/care/descaled").length, 0, "not without the confirmation");
  page.once("dialog", d => d.accept());
  await view(page).locator("button", {hasText: "Entkalkt"}).click();
  await page.locator("#descaleSt", {hasText: /^≈ 0,0 l seit dem Entkalken am \d{1,2}\.\d{1,2}\.\d{4}$/}).waitFor();
  assert.equal((await posts(BASE)).filter(x => x.path === "/care/descaled").length, 1);
  await page.screenshot({path: OUT + "care-descale.png", fullPage: true});
  assert.deepEqual(errors, []);
  await ctx.close();
});

test("Reinigung mit Reiniger: Backflush-Modus an, jeder Schritt als Hinweis, endet mit dem Modus", async ({browser}) => {
  const {page, ctx, errors} = await open(browser, BASE, {hash: "#care"});
  await view(page).locator("#cleanBtn").click();
  await row(page, "Backflush-Modus").locator(".sw.on").waitFor();
  const p = await posts(BASE);
  assert.ok(p.some(x => x.path === "/toggleBackflush" && x.query === "cleaner=1"), JSON.stringify(p));
  assert.equal((await values(BASE))["BACKFLUSH_ON"], 1);
  assert.equal(await view(page).locator("#cleanBtn").isVisible(), false, "not twice");
  await view(page).locator(".note.clean", {hasText: "Reinigung: Reiniger ins Blindsieb, dann Bezugsschalter an."}).waitFor();
  await tab(page, "Maschine");
  await view(page).locator(".note.clean", {hasText: "Reiniger ins Blindsieb"}).waitFor();
  assert.equal(await view(page).locator("#bfActive").isVisible(), false, "the step says more than \"backflush mode on\"");
  await mock(BASE, "/__live", {state: 50, clean: 2});
  await view(page).locator(".note.clean", {hasText: "Blindsieb ausspülen, Bezugsschalter aus und wieder an: Klarspülen."}).waitFor();
  await page.screenshot({path: OUT + "status-cleaning.png", fullPage: true});
  await mock(BASE, "/__live", {state: 50, clean: 3});
  await view(page).locator(".note.clean", {hasText: "Klarspülen mit Wasser."}).waitFor();
  await mock(BASE, "/__live", {state: 10, clean: 0});
  await view(page).locator(".note.clean").waitFor({state: "hidden"});
  assert.deepEqual(errors, []);
  await ctx.close();
});

test("Referenz-Bezug: speichern, liegt hinter jedem anderen Bezug, entfernen; Claude kennt ihn", async ({browser}) => {
  const now = Math.floor(Date.now() / 1000);
  await mock(BASE, "/__shot", {s: 27.0, g: 36.0, at: now - 3600, d: 18.0, m: "12", r: 2});
  await mock(BASE, "/__shot", {s: 22.5, g: 36.2, at: now - 600, d: 18.0, m: "13"});
  const {page, ctx, errors} = await open(browser, BASE, {hash: "#brew", init: withKey, route: pg => fakeClaude(pg, "Etwas feiner.")});
  await view(page).locator("#shotList .shot").nth(1).click();
  const cv = () => view(page).locator("#shotList .curve:not([hidden])");
  await cv().locator("canvas").waitFor();
  await cv().locator("button", {hasText: "Als Referenz"}).click();
  await toast(page, "Als Referenz gespeichert");
  assert.equal((await mock(BASE, "/reference")).shot.s, 27.0);
  await cv().locator("button", {hasText: "Referenz entfernen"}).waitFor();
  await view(page).locator("#shotList .shot").first().click(); // the newest: the reference is drawn behind it
  await cv().locator(".cmp button.on", {hasText: "Referenz · 27,0 s · 36,0 g"}).waitFor();
  await page.screenshot({path: OUT + "brew-reference.png", fullPage: true});
  await cv().locator(".cmp button", {hasText: "Referenz"}).click();
  await cv().locator(".cmp button.on").waitFor({state: "detached"});
  const user = await page.evaluate(() => coffeeContext()); // the suggestion was asked before the reference existed: what the next one gets
  assert.ok(user.includes("Referenzbezug, vom Nutzer als Vorbild gewählt"), user);
  assert.ok(user.includes("27,0 s, 36,0 g, 18,0 g Kaffee, Mahlgrad 12"), user);
  await view(page).locator("#shotList .shot").nth(1).click();
  await cv().locator("button", {hasText: "Referenz entfernen"}).click();
  await toast(page, "Referenz entfernt");
  assert.equal(await mock(BASE, "/reference"), null);
  assert.deepEqual(errors, []);
  await ctx.close();
});

test("Kanalbildung: Hinweis im Bezug und auf der Übersicht, Claude bekommt ihn", async ({browser}) => {
  const now = Math.floor(Date.now() / 1000);
  await mock(BASE, "/__shot", {s: 19.0, g: 36.0, at: now - 300, d: 18.0, m: "12", ch: true});
  let seen;
  const {page, ctx, errors} = await open(browser, BASE, {init: withKey, route: async pg => { seen = await fakeClaude(pg, "Gleichmäßiger verteilen."); }});
  await view(page).locator("#lastCard .shotinfo.ch", {hasText: "Durchfluss sprang – Kanalbildung?"}).waitFor();
  const ai = view(page).locator("#lastCard details.ai");
  await ai.waitFor();
  assert.equal(await ai.evaluate(d => d.open), false, "folded on the overview");
  await tab(page, "Bezug");
  await view(page).locator("#shotList .shot").first().click();
  await view(page).locator("#shotList .curve:not([hidden]) .shotinfo.ch").waitFor();
  assert.ok(seen[0].body.messages[0].content.includes("Durchfluss sprang während des Bezugs plötzlich an (Hinweis auf Kanalbildung)"));
  assert.deepEqual(errors, []);
  await ctx.close();
});

test("Temperaturverlauf im Bezug: in Temperatur einstellbar, Claude weiß davon", async ({browser}) => {
  await mock(BASE, "/__shot", {s: 25.0, g: 36.0, at: Math.floor(Date.now() / 1000) - 300});
  const {page, ctx, errors} = await open(browser, BASE, {hash: "#settings"});
  const r = row(page, "Verlauf im Bezug");
  assert.equal(await r.locator("input").inputValue(), "0,0 K");
  assert.match(await r.locator(".lbl p").textContent(), /0 = aus/);
  for (let k = 0; k < 4; k++) await r.locator("button", {hasText: "−"}).click();
  await settle(page);
  assert.equal((await values(BASE))["brew.temp_end"], -2);
  assert.equal(await r.locator("input").inputValue(), "−2,0 K".replace("−", "-"));
  const ctxText = await page.evaluate(() => coffeeContext());
  assert.ok(ctxText.includes("Temperaturverlauf: Das Soll ändert sich während des Bezugs gleichmäßig um -2,0 K bis zum Ende."), ctxText);
  assert.deepEqual(errors, []);
  await ctx.close();
});

test("Schnellwahl: alte Werte in Gramm werden als Verhältnis gelesen, Laden ändert nichts", async ({browser}) => {
  await setp("brew.presets=25,33;30,45;45,80&brew.dose=16.5&brew.mode=1"); await setp("brew.by_weight.enabled=1&brew.by_time.enabled=0&brew.by_weight.target_weight=33");
  const before = (await posts(BASE)).length; // the test's own settings
  const {page, ctx, errors} = await open(browser, BASE, {hash: "#brew"});
  assert.deepEqual(await view(page).locator(".chips button").allTextContents(), ["Espresso1:2,0 · 33 g", "Doppio1:2,7 · 45 g", "Lungo1:4,9 · 80 g"]);
  assert.equal(await view(page).locator(".chips button.on").textContent(), "Espresso1:2,0 · 33 g");
  assert.deepEqual((await posts(BASE)).slice(before), [], "loading the page must not rewrite the presets");
  await view(page).locator(".recipe .step input").fill("18"); await view(page).locator(".recipe .step input").press("Enter"); // more coffee: the cup follows
  await view(page).locator(".chips button.on", {hasText: "Espresso1:2,0 · 36 g"}).waitFor();
  await settle(page);
  assert.equal((await values(BASE))["brew.by_weight.target_weight"], 36);
  assert.deepEqual(errors, []);
  await ctx.close();
});

test("Spülen nach dem Bezug: Knopf spült einmal, nur mit Wasserstandssensor und bereit", async ({browser}) => {
  await setp("hardware.sensors.watertank.enabled=1");
  await mock(BASE, "/__live", {state: 10, fp: true, sw: true});
  const {page, ctx, errors} = await open(browser, BASE);
  const btn = view(page).locator(".note.rinse .btn");
  await btn.waitFor();
  assert.equal(await btn.isDisabled(), true, "the brew switch is still on");
  await mock(BASE, "/__live", {state: 10, fp: true, sw: false});
  await page.waitForFunction(() => !document.querySelector("#main > div:not([hidden]) .note.rinse .btn").disabled);
  await btn.click();
  await settle(page);
  assert.ok((await posts(BASE)).some(x => x.path === "/flush" && x.query === "start=1&pulses=1"), JSON.stringify(await posts(BASE)));
  await mock(BASE, "/__live", {state: 25, fp: true, warmup: 2, pulse: 1, pulses: 1});
  await btn.filter({hasText: "Spült …"}).waitFor();
  assert.equal(await page.locator("#state").textContent(), "Spülen", "one pulse: no 1/3");
  await mock(BASE, "/__live", {state: 10, fp: false});
  await view(page).locator(".note.rinse").waitFor({state: "hidden"});
  assert.deepEqual(errors, []);
  await ctx.close();
});

test("Wassertank leer: Tropfen statt nur Farbe; Kühlt ab neutral", async ({browser}) => {
  const {page, ctx, errors} = await open(browser, BASE);
  await mock(BASE, "/__live", {state: 70});
  await page.locator("#alarm.info svg.ico").waitFor();
  assert.match(await page.locator("#alarm").textContent(), /^Wassertank leerBitte nachfüllen/);
  assert.equal(await page.locator("#chip i").getAttribute("class"), "drop");
  await page.screenshot({path: OUT + "status-tank-empty.png"});
  await mock(BASE, "/__live", {state: 10, currentTemp: 99.0, targetTemp: 95.0});
  await page.locator("#state", {hasText: "Kühlt ab"}).waitFor();
  assert.equal(await page.locator("#chip i").evaluate(e => e.style.background), "var(--calm)");
  assert.equal(await page.locator("#chip i").getAttribute("class"), "");
  assert.deepEqual(errors, []);
  await ctx.close();
});

test("Einstellungen: Standby und Zeitplan in einer Karte, Waagen-Timer und Feinwerte nicht im Weg", async ({browser}) => {
  const {page, ctx, errors} = await open(browser, BASE, {hash: "#settings"});
  const cards = await page.locator("#main > div:not([hidden]) section.card[data-card]").evaluateAll(cs => cs.map(c => c.dataset.card));
  assert.deepEqual(cards, ["sPresets", "sCoffee", "sPreinf", "sTemp", "sPower", "sScale", "sClaude", "sTank", "sDisplay", "sPid", "sSystem"]);
  const power = view(page).locator('[data-card="sPower"]');
  assert.equal(await power.locator("h2").textContent(), "Ein & Aus");
  assert.deepEqual(await power.locator(".lbl > div").allTextContents(), ["Standby", "Automatisch einschalten"]);
  assert.equal(await row(page, "Timer der Waage mitlaufen lassen").count(), 0);
  assert.equal(await row(page, "Offset").isVisible(), false, "folded under Fortgeschritten");
  assert.equal(await row(page, "FERTIG danach noch").count(), 1);
  assert.match(await row(page, "PonM").locator(".lbl p").textContent(), /weniger Überschwingen/);
  assert.deepEqual(errors, []);
  await ctx.close();
});

test("Wartung: Bezüge exportieren (JSON und CSV), letzter Backflush mit Datum", async ({browser}) => {
  const now = Math.floor(Date.now() / 1000);
  await mock(BASE, "/__shot", {s: 25.3, g: 36.4, at: now - 600, d: 18.0, m: "12", b: "Röstwerk \"Hell\""});
  await mock(BASE, "/__care", {backflushAt: now - 86400 - 3600});
  const {page, ctx, errors} = await open(browser, BASE, {hash: "#care"});
  await page.locator("#bfSince", {hasText: /^Letzter Backflush vor 0 Bezügen · gestern \d\d:\d\d$/}).waitFor();
  const files = [];
  page.on("download", d => files.push(d));
  await view(page).locator("button", {hasText: "Bezüge exportieren"}).click();
  for (let i = 0; i < 50 && files.length < 2; i++) await sleep(100);
  assert.deepEqual(files.map(f => f.suggestedFilename().replace(/\d{4}-\d\d-\d\d/, "D")).sort(), ["orione-shots-D.csv", "orione-shots-D.json"]);
  const json = JSON.parse(await (await import("node:fs/promises")).readFile(await files.find(f => f.suggestedFilename().endsWith(".json")).path(), "utf8"));
  assert.equal(json.shots.length, 1); assert.ok(json.shots[0].curve?.t?.length > 10, "with its curve");
  const csv = await (await import("node:fs/promises")).readFile(await files.find(f => f.suggestedFilename().endsWith(".csv")).path(), "utf8");
  assert.match(csv, /^at;s;g;d;m;b;ratio;r;t0;fd;tw;sw;lg;pi;ch\n/);
  assert.match(csv, /;25,3;36,4;18,0;"12";"Röstwerk ""Hell""";2,02;/);
  assert.deepEqual(errors, []);
  await ctx.close();
});

test("Übersicht ohne Bezug: Zähler und was fällig ist", async ({browser}) => {
  const {page, ctx, errors} = await open(browser, BASE);
  const card = view(page).locator("#lastCard");
  await card.locator(".shotinfo", {hasText: /^3 heute · 12 diese Woche$/}).waitFor();
  assert.equal(await card.locator("h2").textContent(), "Heute");
  assert.equal(await view(page).locator("#careCard h2").textContent(), "Pflege");
  assert.deepEqual(errors, []);
  await ctx.close();
});

test("Erster Tropfen und 10 g gegen die Referenz: schneller heißt feiner", async ({browser}) => {
  const now = Math.floor(Date.now() / 1000);
  await mock(BASE, "/__shot", {s: 30.0, g: 36.0, at: now - 3600, d: 18.0, m: "12", fd: 8.0});
  await mock(BASE, "/__shot", {s: 22.0, g: 36.0, at: now - 600, d: 18.0, m: "13", fd: 5.0});
  const {page, ctx, errors} = await open(browser, BASE, {hash: "#brew"});
  await view(page).locator("#shotList .shot").nth(1).click();
  const cv = () => view(page).locator("#shotList .curve:not([hidden])");
  await cv().locator("canvas").waitFor();
  await cv().locator("button", {hasText: "Als Referenz"}).click();
  await toast(page, "Als Referenz gespeichert");
  await view(page).locator("#shotList .shot").first().click();
  await cv().locator(".shotinfo", {hasText: "erster Tropfen nach 5,0 s"}).waitFor(); // drawn anew for the newest
  assert.match(await cv().locator(".shotinfo").first().textContent(), /erster Tropfen nach 5,0 s \(Ref\. 8,0\) · 10 g nach 10,5 s \(Ref\. 12,6\)/);
  await cv().locator(".shotinfo.ch", {hasText: "2,1 s schneller als die Referenz: eher feiner mahlen"}).waitFor();
  const ctxText = await page.evaluate(() => coffeeContext());
  assert.ok(ctxText.includes("10 g in der Tasse nach 10,5 s"), ctxText);
  assert.ok(ctxText.includes("erster Tropfen nach 8,0 s, 10 g nach 12,6 s"), ctxText);
  assert.deepEqual(errors, []);
  await ctx.close();
});

test("Röstdatum je Bohne: im Reiter Bezug eintragen, Tag nach Röstung in Liste und für Claude; Niche-Schritte", async ({browser}) => {
  await setp("brew.beans=Ettli Don Pedro&brew.grinder=Niche Zero");
  const {page, ctx, errors} = await open(browser, BASE, {hash: "#brew"});
  const rp = view(page).locator("#roastPick");
  await rp.waitFor();
  const d = new Date(Date.now() - 12 * 864e5), iso = `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, "0")}-${String(d.getDate()).padStart(2, "0")}`;
  await rp.fill(iso);
  await view(page).locator("#roastAge", {hasText: "Tag 12 nach Röstung"}).waitFor();
  assert.ok((await posts(BASE)).some(x => x.path === "/beans/roast" && x.query === `n=Ettli%20Don%20Pedro&d=${iso}`), JSON.stringify(await posts(BASE)));
  const ctxText = await page.evaluate(() => coffeeContext());
  assert.ok(ctxText.includes("(heute Tag 12 nach Röstung)"), ctxText);
  assert.ok(ctxText.includes("Mahlgrad deshalb in Schritten von 0,25 bis 0,5 vorschlagen"), ctxText);
  await tab(page, "Einstellungen");
  await view(page).locator(".beanrow p", {hasText: /^Tag 12 nach Röstung · /}).waitFor();
  assert.deepEqual(errors, []);
  await ctx.close();
});

test("Backflush: Zyklus, Phase und Restzeit in der App", async ({browser}) => {
  const {page, ctx, errors} = await open(browser, BASE);
  await mock(BASE, "/__live", {state: 50, bfc: 3, bfn: 5, bfp: 1});
  await page.locator("#state", {hasText: "Backflush 3/5"}).waitFor();
  const note = view(page).locator(".note.bfprog");
  await note.waitFor();
  assert.match(await note.textContent(), /^Backflush: Zyklus 3 von 5 · Pumpen · noch ≈ (4[5-9]|50) s$/);
  await mock(BASE, "/__live", {state: 50, bfc: 5, bfn: 5, bfp: 3});
  await note.filter({hasText: /Zyklus 5 von 5 · Pause · noch ≈ (9|10) s$/}).waitFor();
  await tab(page, "Wartung");
  await view(page).locator('[data-card="sBf"] .note.bfprog').waitFor();
  await mock(BASE, "/__live", {state: 10, bfc: 0});
  await view(page).locator(".note.bfprog").waitFor({state: "hidden"});
  assert.deepEqual(errors, []);
  await ctx.close();
});

test("Tropfschale: Schätzung in Wartung, Hinweis bei 80 %, Geleert setzt zurück", async ({browser}) => {
  await mock(BASE, "/__care", {drip: 500});
  const {page, ctx, errors} = await open(browser, BASE);
  const note = view(page).locator(".note.drip");
  await note.locator("span", {hasText: "Tropfschale leeren (≈ 500 ml)"}).waitFor();
  await tab(page, "Wartung");
  assert.equal(await page.locator("#dripSt").textContent(), "≈ 500 ml von 600 ml (geschätzt)");
  assert.equal(await row(page, "Fasst").locator("input").inputValue(), "600 ml");
  await view(page).locator('section[data-card="sDrip"] button', {hasText: "Geleert"}).click();
  await page.locator("#dripSt", {hasText: "≈ 0 ml von 600 ml"}).waitFor();
  assert.equal((await posts(BASE)).filter(x => x.path === "/care/drip-emptied").length, 1);
  await tab(page, "Maschine");
  await view(page).locator(".note.drip").waitFor({state: "hidden"});
  assert.deepEqual(errors, []);
  await ctx.close();
});

test("Live-Kurve im Bezug: wächst mit, Referenz dahinter, bleibt nach dem Stopp stehen", async ({browser}) => {
  const now = Math.floor(Date.now() / 1000);
  await mock(BASE, "/__shot", {s: 28.0, g: 36.0, at: now - 3600, d: 18.0, m: "12", fd: 7.0});
  await setp("brew.mode=1"); await setp("brew.by_weight.enabled=1&brew.by_time.enabled=0");
  const {page, ctx, errors} = await open(browser, BASE);
  await page.evaluate(async () => { // the shot in the list becomes the reference: drawn dashed behind the live curve
    const r = await fetch(`/shot/reference?i=0&at=${shots[0].at}&s=${shots[0].s}`, {method: "POST"}); await loadReference(); return r.ok;
  });
  const ink = sel => page.evaluate(sel => { // drawn pixels on a canvas
    const c = document.querySelector(sel); if (!c || !c.width) return 0;
    const d = c.getContext("2d").getImageData(0, 0, c.width, c.height).data; let n = 0;
    for (let i = 3; i < d.length; i += 4) n += d[i] > 0; return n;
  }, sel);
  for (let k = 1; k <= 8; k++) {
    await mock(BASE, "/__live", {state: 20, brewTime: k, scale: 2, weight: Math.max(0, (k - 3) * 2.2), flow: k > 3 ? 2.2 : 0, currentTemp: 93 - k * 0.3});
    await sleep(700);
  }
  await page.waitForFunction(() => liveCurve?.pts.length >= 6);
  assert.ok(await ink("#dialCurve") > 500, "the overview draws it under the ring");
  const d = await page.evaluate(() => liveCurveData());
  assert.equal(d.dt, 1000); assert.equal(d.stop, -1, "still running");
  assert.ok(d.w.at(-1) >= 100, JSON.stringify(d.w));
  await mock(BASE, "/__live", {state: 10, brewTime: 8.4, scale: 2, weight: 11.0, cup: 11.6, held: true, currentTemp: 90.8});
  await page.waitForFunction(() => liveCurve?.stop != null);
  await sleep(1300);
  assert.equal(await page.locator("#dialCard").isVisible(), true, "the result stays while the switch is on");
  await tab(page, "Bezug");
  await view(page).locator("#lsCurve").waitFor();
  await page.evaluate(() => paintLive());
  assert.ok(await ink("#lsCurve") > 500, "and in the brew tab's live card");
  await page.screenshot({path: OUT + "brew-live-curve.png", fullPage: true});
  assert.deepEqual(errors, []);
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
    if (await page.locator("#toast.show", {hasText: /Nicht gespeichert|Nicht alles geladen/}).count()) failed = true;
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
