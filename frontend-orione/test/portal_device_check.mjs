// Walks through the WiFi setup portal (src/orioneWifiPortal.h) on a bench ESP32 running the
// esp32_round_portal build, which serves the portal on port 8080 in the home WiFi.
//
//   node portal_device_check.mjs [192.168.178.59] [--out DIR]
//
// Nothing reaches the ESP32's WiFi settings: the browser turns the form post into a GET without
// data (the build does the same on its side), which WiFiManager answers without saving anything.
// The wrong-password message is checked on a copy of the page with WiFiManager's message added.

import assert from "node:assert/strict";
import {mkdirSync} from "node:fs";
import {chromium} from "playwright";

const host = process.argv[2] && !process.argv[2].startsWith("--") ? process.argv[2] : "192.168.178.59";
const i = process.argv.indexOf("--out"), OUT = i > 0 ? process.argv[i + 1] : new URL("./out/", import.meta.url).pathname;
mkdirSync(OUT, {recursive: true});
const B = `http://${host}:8080`;
const br = await chromium.launch();
const ctx = await br.newContext({viewport: {width: 390, height: 844}, deviceScaleFactor: 2, locale: "de-DE"});
const page = await ctx.newPage();
const errors = [];
page.on("pageerror", e => errors.push(e.message));
await page.route("**/wifisave", r => r.request().method() === "POST" ? r.continue({method: "GET", postData: "", url: B + "/wifisave"}) : r.continue());

const t0 = Date.now();
await page.goto(B + "/", {timeout: 30000});
await page.waitForURL("**/wifi", {timeout: 30000});
await page.waitForSelector("header h1");
console.log(`Menü -> Netzwerkliste in ${Date.now() - t0} ms`);
assert.equal(await page.locator("header h1").textContent(), "WLAN verbinden");
assert.deepEqual(await page.locator("label").allTextContents(), ["Netzwerk", "Passwort", "Passwort anzeigen"]);
assert.equal(await page.locator("button[type=submit]").textContent(), "Verbinden");
assert.equal(await page.locator("button.sec").textContent(), "Neu suchen");
assert.equal(await page.locator(".wrap > br").count(), 0);
assert.ok(await page.evaluate(() => document.scrollingElement.scrollWidth <= innerWidth), "no sideways scrolling at 390 px");
const nets = await page.locator(".nets a").allTextContents();
assert.ok(nets.length > 0, "networks listed");
console.log("Netzwerke:", nets.join(", "));
for (const m of await page.locator(".msg").allTextContents()) console.log("Meldung:", m);
await page.screenshot({path: OUT + "/portal-1-liste.png", fullPage: true});

await page.locator(".nets a").first().click();
assert.equal(await page.locator("#s").inputValue(), nets[0]);
assert.equal(await page.locator(".nets > div.sel").count(), 1);
await page.locator("#p").fill("nur-ein-test");
await page.locator(".show label").click();
assert.equal(await page.locator("#p").getAttribute("type"), "text");
await page.screenshot({path: OUT + "/portal-2-gewaehlt.png", fullPage: true});
await Promise.all([page.waitForURL("**/wifisave", {timeout: 20000}), page.locator("button[type=submit]").click()]);
await page.waitForSelector(".card .url");
assert.equal(await page.locator("header h1").textContent(), "Fast geschafft");
assert.equal(await page.locator(".url").textContent(), "http://orione.local");
await page.screenshot({path: OUT + "/portal-3-gespeichert.png", fullPage: true});

// wrong password: WiFiManager adds this message to the page after a failed attempt
await page.goto("about:blank"); // WiFiManager's web server serves one client at a time
let html = "";
for (let i = 0; i < 4 && !html; i++) html = await fetch(B + "/wifi").then(r => r.text()).catch(() => new Promise(ok => setTimeout(() => ok(""), 1500)));
assert.ok(html, "page copy fetched");
await page.route("**/wifi", r => r.fulfill({contentType: "text/html", body: html.replace("</div></body>", "<div class='msg D'><strong>Not connected</strong> to DD07<br/>Authentication failure</div></div></body>")}));
await page.goto(B + "/wifi");
await page.waitForSelector(".msg.D");
const msg = await page.locator(".msg.D").textContent();
assert.match(msg, /Keine Verbindung mit .*Passwort prüfen/);
console.log("Falsches Passwort:", msg);
await page.screenshot({path: OUT + "/portal-4-falsch.png", fullPage: true});

assert.deepEqual(errors, []);
await br.close();
const v = await fetch(`http://${host}/version`).then(r => r.status).catch(() => "keine Antwort");
assert.equal(v, 200, "the ESP32 is still in the home WiFi");
console.log("Portal geprüft, ESP32 weiter im WLAN. Screenshots:", OUT);
