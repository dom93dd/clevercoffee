// Walks through the whole web page on a real ESP32 (bench or machine) and uses every control once,
// then puts every setting back. Nothing destructive: the WiFi and factory reset dialogs are
// dismissed, no restart. At the end the settings are compared with a backup taken first.
//
//   node device_walkthrough.mjs [192.168.178.59] [--out DIR]
//
// Prints one line per check; exit code 1 on a failed check or a changed setting.

import assert from "node:assert/strict";
import {mkdirSync} from "node:fs";
import {chromium} from "playwright";

const host = process.argv[2] && !process.argv[2].startsWith("--") ? process.argv[2] : "192.168.178.59";
const i = process.argv.indexOf("--out"), OUT = i > 0 ? process.argv[i + 1] : new URL("./out/device/", import.meta.url).pathname;
mkdirSync(OUT, {recursive: true});
const B = `http://${host}`, sleep = ms => new Promise(r => setTimeout(r, ms));
const config = async () => (await fetch(B + "/download/config")).json();
const flat = (o, p = "") => Object.entries(o).flatMap(([k, v]) => v && typeof v === "object" ? flat(v, p + k + ".") : [[p + k, v]]);
let failed = 0;
const check = async (name, fn) => {
  try { await fn(); console.log("ok    " + name); } catch (e) { failed++; console.log("FAIL  " + name + "\n      " + String(e.message).split("\n")[0]); }
};

const before = await config();
const br = await chromium.launch();
const errors = [];
let inflight = 0, maxInflight = 0;

async function open(width) {
  const ctx = await br.newContext({viewport: {width, height: 900}, deviceScaleFactor: 2, locale: "de-DE"});
  const page = await ctx.newPage();
  page.on("pageerror", e => errors.push(`${width}px: ${e.message}`));
  page.on("console", m => { if (m.type() === "error" && !/503|Failed to load resource/.test(m.text())) errors.push(`${width}px: ${m.text()}`); });
  page.on("dialog", d => d.dismiss().catch(() => {}));
  const counted = r => !r.url().endsWith("/events") && !r.isNavigationRequest() && !r.url().startsWith("data:");
  page.on("request", r => { if (counted(r)) maxInflight = Math.max(maxInflight, ++inflight); });
  page.on("requestfinished", r => { if (counted(r)) inflight--; });
  page.on("requestfailed", r => { if (counted(r)) inflight--; });
  await page.goto(B + "/");
  await page.waitForFunction(() => !(document.querySelector("#tNow")?.textContent ?? "–").startsWith("–"), null, {timeout: 20000});
  return {ctx, page};
}
const view = page => page.locator("#main > div:not([hidden])");
const row = (page, label) => view(page).locator(".row", {has: page.locator(".lbl > div", {hasText: new RegExp("^" + label + "$")})});
async function tab(page, label) {
  await page.locator("nav button", {hasText: label}).click();
  await page.locator("nav button.on", {hasText: label}).waitFor();
  await sleep(150);
}
async function quiet(page, ms = 1200) { // the page's request chain ran dry
  let last = Date.now(); const on = () => { last = Date.now(); };
  page.on("request", on); page.on("requestfinished", on);
  while (Date.now() - last < ms) await sleep(100);
  page.off("request", on); page.off("requestfinished", on);
}
const noOverflow = async (page, what) => {
  const [sw, w] = await page.evaluate(() => [document.scrollingElement.scrollWidth, innerWidth]);
  assert.ok(sw <= w, `${what}: ${sw}px wide in ${w}px`);
};

// ---------- every tab at three widths ----------
for (const width of [320, 390, 1024]) {
  const {ctx, page} = await open(width);
  for (const [label, file] of [["Maschine", "status"], ["Bezug", "brew"], ["Einstellungen", "settings"], ["Wartung", "care"]]) {
    await check(`${width}px ${label}: aufgebaut, nicht breiter als der Bildschirm`, async () => {
      await tab(page, label);
      await quiet(page, 600);
      assert.ok(await view(page).locator(".card").count() > 0);
      await noOverflow(page, label);
      await page.screenshot({path: `${OUT}${width}-${file}.png`, fullPage: true});
    });
  }
  await ctx.close();
}

// ---------- every control once, at phone width ----------
const {ctx, page} = await open(390);
const toastOk = () => page.locator("#toast.show", {hasText: /Gespeichert|✓/}).waitFor({timeout: 6000});

await check("Maschine: Soll-Temperatur + und − speichert und kommt zurück", async () => {
  const step = view(page).locator(".hero .step"), v0 = await step.locator("input").inputValue();
  await step.locator("button", {hasText: "+"}).click(); await toastOk(); await quiet(page);
  assert.notEqual(await step.locator("input").inputValue(), v0);
  await step.locator("button", {hasText: "−"}).click(); await toastOk(); await quiet(page);
  assert.equal(await step.locator("input").inputValue(), v0);
});
await check("Maschine: Temperaturregelung aus und wieder an", async () => {
  const sw = row(page, "Temperaturregelung").locator(".sw");
  await sw.click(); await toastOk(); await quiet(page);
  await sw.click(); await toastOk(); await quiet(page);
  assert.match(await sw.getAttribute("class"), /\bon\b/);
});
await check("Maschine: Verlauf gezeichnet", async () => {
  const px = await page.evaluate(() => { const c = document.querySelector("#chart"); const d = c.getContext("2d").getImageData(0, 0, c.width, c.height).data; let n = 0; for (let i = 3; i < d.length; i += 4) n += d[i] > 0; return n; });
  assert.ok(px > 1000, "chart pixels " + px);
});

await tab(page, "Bezug");
await check("Bezug: Stoppen von Hand / nach Zeit / zurück", async () => {
  const stop = () => row(page, "Stoppen");
  const was = await stop().locator("button.on").textContent();
  await stop().locator("button", {hasText: "nach Zeit"}).click(); await row(page, "Bezugszeit").waitFor(); await quiet(page);
  await stop().locator("button", {hasText: was}).click(); await quiet(page);
  assert.equal(await stop().locator("button.on").textContent(), was);
});
await check("Bezug: Schnellwahl Doppio setzt Ziel, danach alles wie vorher", async () => {
  const chips = view(page).locator(".chips button");
  assert.equal(await chips.count(), 3);
  await chips.nth(1).click(); await quiet(page, 1500);
  assert.equal(await view(page).locator(".chips button.on").count(), 1);
});
await check("Bezug: Letzte Bezüge, Kurve auf und zu", async () => {
  const rows = view(page).locator("#shotList .shot");
  if (await rows.count() === 0) { console.log("      (noch keine Bezüge)"); return; }
  await rows.first().click(); await view(page).locator("#shotList .curve:not([hidden])").waitFor();
  await page.screenshot({path: `${OUT}390-brew-curve.png`, fullPage: true});
  await rows.first().click(); await sleep(200);
  assert.equal(await view(page).locator("#shotList .curve:not([hidden])").count(), 0);
});

await tab(page, "Einstellungen");
await check("Einstellungen: Schnellwahl-Wert + und −", async () => {
  const st = row(page, "Espresso").locator(".step").first(), v0 = await st.locator("input").inputValue();
  await st.locator("button", {hasText: "+"}).click(); await toastOk(); await quiet(page);
  await st.locator("button", {hasText: "−"}).click(); await toastOk(); await quiet(page);
  assert.equal(await st.locator("input").inputValue(), v0);
});
await check("Einstellungen: Offset tippen (Komma), zurück", async () => {
  const inp = row(page, "Offset").locator("input"), v0 = await inp.inputValue();
  await inp.click(); await inp.fill("0,5"); await inp.press("Enter"); await toastOk(); await quiet(page);
  assert.equal(await inp.inputValue(), "0,5 °C");
  await inp.click(); await inp.fill(v0.replace(/[^0-9,.-]/g, "")); await inp.press("Enter"); await quiet(page);
  assert.equal(await inp.inputValue(), v0);
});
await check("Einstellungen: Standby an (Zeile erscheint) und aus", async () => {
  const sw = row(page, "Standby").locator(".sw"), on = /\bon\b/.test(await sw.getAttribute("class"));
  await sw.click(); await quiet(page);
  assert.equal(await row(page, "Nach").count(), on ? 0 : 1);
  await row(page, "Standby").locator(".sw").click(); await quiet(page);
});
await check("Einstellungen: Sprache Englisch und zurück", async () => {
  await row(page, "Sprache").locator("button", {hasText: "English"}).click();
  await page.locator("nav button", {hasText: "Settings"}).waitFor();
  await page.screenshot({path: `${OUT}390-settings-en.png`, fullPage: true});
  await row(page, "Language").locator("button", {hasText: "Deutsch"}).click();
  await page.locator("nav button", {hasText: "Einstellungen"}).waitFor(); await quiet(page);
});
await check("Einstellungen: Regler und System aufklappen", async () => {
  for (const s of ["Regler", "System"]) { // into the middle first: at the bottom edge the fixed tab bar would take the click
    const el = view(page).locator("summary", {hasText: s});
    await el.evaluate(e => e.scrollIntoView({block: "center"}));
    await el.click();
  }
  assert.equal(await view(page).locator("details[open]").count(), 2);
  assert.equal(await row(page, "Update-Passwort").locator("input").getAttribute("type"), "password");
});

await tab(page, "Wartung");
await check("Wartung: Backflush an und aus", async () => {
  const sw = () => row(page, "Backflush-Modus").locator(".sw");
  await sw().click(); await quiet(page, 1500);
  assert.match(await sw().getAttribute("class"), /\bon\b/);
  await sw().click(); await quiet(page, 1500);
  assert.doesNotMatch(await sw().getAttribute("class"), /\bon\b/);
});
await check("Wartung: Sicherung herunterladen, Version, Reset-Dialoge abgebrochen", async () => {
  const [dl] = await Promise.all([page.waitForEvent("download"), view(page).locator("a", {hasText: "herunterladen"}).click()]);
  assert.ok((await dl.path()) !== null);
  await view(page).locator(".ver", {hasText: "Version"}).waitFor();
  for (const b of ["WLAN zurücksetzen", "Werkseinstellungen"]) await view(page).locator("button", {hasText: b}).click();
  await sleep(1500);
  assert.ok(await fetch(B + "/version").then(r => r.ok), "still running");
});

await check("Höchstens eine Anfrage gleichzeitig, keine Skriptfehler", async () => {
  assert.equal(maxInflight, 1);
  assert.deepEqual(errors, []);
});
await ctx.close();
await br.close();

// ---------- put the brew settings back (stop choice and quick choice changed them) ----------
// by_time / by_weight are only accepted while brew.mode is automatic: those first, the mode last
const a0 = Object.fromEntries(flat(before));
const post = (k, v) => fetch(B + "/parameters", {method: "POST", headers: {"Content-Type": "application/x-www-form-urlencoded"}, body: `${encodeURIComponent(k)}=${encodeURIComponent(typeof v === "boolean" ? Number(v) : v)}`});
await post("brew.mode", 1);
for (const k of Object.keys(a0).filter(k => /^brew\.by_/.test(k))) await post(k, a0[k]);
await post("brew.mode", a0["brew.mode"]);

// ---------- settings as before ----------
const after = await config(), a = Object.fromEntries(flat(before)), b = Object.fromEntries(flat(after));
const changed = Object.keys({...a, ...b}).filter(k => JSON.stringify(a[k]) !== JSON.stringify(b[k]));
await check("Einstellungen wie vorher", async () => assert.deepEqual(changed.map(k => `${k}: ${a[k]} -> ${b[k]}`), []));
console.log(failed ? `${failed} Prüfungen fehlgeschlagen` : "Alle Prüfungen bestanden", "· Screenshots:", OUT);
process.exit(failed ? 1 : 0);
