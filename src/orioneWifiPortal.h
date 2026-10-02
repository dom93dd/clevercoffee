/**
 * @file orioneWifiPortal.h
 *
 * @brief Orione build (CC_ORIONE): the WiFi setup of WiFiManager made simple.
 *
 * - The display shows a QR code: the phone camera joins the setup WiFi without typing name or
 *   password (roundDisplayWifiSetup()).
 * - The captive portal opens straight on the network list, in German (English when the display
 *   language is English) and dark like the web interface: tap the network, type the password,
 *   "Verbinden". No menu with info, update or erase.
 * - A wrong password keeps the portal open for another try (stock: the portal closes and the
 *   machine goes offline until the next start).
 * - The first setup (no saved WiFi) waits 10 minutes instead of 60 seconds, and the time only runs
 *   while no phone is connected to the setup WiFi.
 *
 * Style and script live in flash and are added to every page WiFiManager builds; the script
 * sends the menu page on to /wifi and renames WiFiManager's fields and messages. RAM: the
 * device name for the script (bodyHeader).
 */

#pragma once

#include <WiFiManager.h>

namespace orione_portal {

    inline const char kHead[] PROGMEM = R"html(<style>
body.invert{background:#0b0b0c;color:#f2f2f4;font:16px/1.45 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;text-align:left;margin:0}
body.home{visibility:hidden}
.wrap{display:block;max-width:440px;min-width:0;margin:0 auto;padding:20px 16px 32px}
header b{font-size:13px;letter-spacing:.14em;color:#8e8e94}
header h1{font-size:28px;font-weight:650;margin:6px 0 4px;text-align:left;color:#f2f2f4}
header p{margin:0 0 18px;color:#8e8e94}
.nets{background:#161618;border-radius:18px;overflow:hidden;margin-bottom:18px}
.nets>div{display:flex;align-items:center;gap:8px;padding:0 14px;margin:0;border-top:1px solid #26262a}
.nets>div:first-child{border-top:0}
.nets>div.sel{background:rgba(255,146,38,.14)}
.nets a{flex:1;color:#f2f2f4!important;font-weight:600;padding:15px 0;text-decoration:none!important}
.nets>div.sel a{color:#ff9226!important}
.nets .q{float:none;margin:0;padding:0;min-width:0}
.nets .q.h{display:none}
label{display:block;font-size:13px;font-weight:600;color:#8e8e94;margin:14px 0 6px}
input{width:100%;margin:0;padding:14px;border:0;border-radius:12px;background:#161618;color:#f2f2f4;font-size:17px}
input:focus{outline:2px solid #ff9226}
.show{display:flex;align-items:center;gap:8px;margin-top:10px}
.show input{width:20px;height:20px;accent-color:#ff9226}
.show label{margin:0;font-size:15px;font-weight:500}
button{display:block;width:100%;margin:18px 0 0;padding:0;border:0;border-radius:12px;height:52px;line-height:52px;background:#ff9226!important;color:#1a1006;font-size:17px;font-weight:650}
button.sec{background:#26262a!important;color:#f2f2f4;margin-top:10px;font-weight:600}
button:disabled{opacity:.6}
.msg{border:0!important;border-radius:14px;background:#161618!important;margin:0 0 18px;padding:14px 16px}
.msg.D{background:rgba(239,68,68,.14)!important;color:#fca5a5}
.msg.S{background:rgba(52,211,153,.14)!important;color:#6ee7b7}
.card{background:#161618;border-radius:18px;padding:6px 16px}
.card p{margin:12px 0}
.url{font-size:20px;font-weight:650;color:#ff9226}
.dim{color:#8e8e94;font-size:14px}
</style><script>
(function(){
var P=location.pathname;
if(P=="/"){location.replace("/wifi");return}
document.addEventListener("DOMContentLoaded",function(){
var w=document.querySelector(".wrap"),i=document.getElementById("hn")||{dataset:{}},H=i.textContent||"orione",E=i.dataset.l=="en";
var t=E?["Connect WiFi","Pick your WiFi and enter its password.","Network","Password","Show password","Connect","Search again","Connecting ...","Almost done","The machine now connects to your WiFi; this setup WiFi goes away.","When the display shows <b>Connected</b>, open it in your home WiFi at","If the display shows the QR code again, it did not work (password?). Just join again.","Tap a network or type its name","No connection to ","Check the password and try again.","No networks found","Connected to "]
:["WLAN verbinden","Wähle dein WLAN und gib das Passwort ein.","Netzwerk","Passwort","Passwort anzeigen","Verbinden","Neu suchen","Verbinde …","Fast geschafft","Die Maschine verbindet sich jetzt mit deinem WLAN, dieses Einrichtungs-WLAN verschwindet dabei.","Zeigt das Display <b>Verbunden</b>, erreichst du sie im Heim-WLAN unter","Zeigt das Display wieder den QR-Code, hat es nicht geklappt (Passwort?). Dann einfach erneut verbinden.","Netzwerk antippen oder Namen eingeben","Keine Verbindung mit ","Passwort prüfen und erneut versuchen.","Keine Netzwerke gefunden","Verbunden mit "];
var hd=document.createElement("header");
if(P=="/wifisave"){hd.innerHTML="<b>ORIONE</b><h1>"+t[8]+"</h1>";w.innerHTML="";w.appendChild(hd);
var c=document.createElement("div");c.className="card";c.innerHTML="<p>"+t[9]+"</p><p>"+t[10]+"</p><p class=url>http://"+H+".local</p><p class=dim>"+t[11]+"</p>";w.appendChild(c);return}
var f=document.querySelector("form[action=wifisave]");if(!f)return;
hd.innerHTML="<b>ORIONE</b><h1>"+t[0]+"</h1><p>"+t[1]+"</p>";
var n=document.createElement("div");n.className="nets";
[].slice.call(w.children).forEach(function(d){if(d.querySelector&&d.querySelector("a[data-ssid]"))n.appendChild(d)});
w.insertBefore(n,w.firstChild);w.insertBefore(hd,n);
if(!n.children.length)n.outerHTML="<div class=msg>"+t[15]+"</div>";
n.addEventListener("click",function(e){var d=e.target.closest(".nets>div");if(!d)return;[].forEach.call(n.children,function(x){x.classList.toggle("sel",x==d)})});
[].forEach.call(f.querySelectorAll("br"),function(b){b.remove()});
var s=document.getElementById("s"),q=document.getElementById("p"),k=document.getElementById("showpass"),L=function(id,x){var l=f.querySelector("label[for="+id+"]");if(l)l.textContent=x;return l};
L("s",t[2]);L("p",t[3]);var sl=L("showpass",t[4]);
if(k&&sl){var sh=document.createElement("div");sh.className="show";k.parentNode.insertBefore(sh,k);sh.appendChild(k);sh.appendChild(sl)}
var sv=s.placeholder;s.placeholder=t[12];
var b=f.querySelector("button[type=submit]");b.textContent=t[5];
f.addEventListener("submit",function(){b.textContent=t[7];setTimeout(function(){b.disabled=true},0)});
var r=document.querySelector("button[name=refresh]");if(r){r.textContent=t[6];r.className="sec"}
[].slice.call(w.children).forEach(function(x){if(x.nodeName=="BR")x.remove()});
[].forEach.call(document.querySelectorAll(".msg"),function(m){var D=m.classList.contains("D"),S=m.classList.contains("S"),ip=(m.textContent.match(/[0-9.]{7,}/)||[""])[0];
if(D)m.innerHTML="<b>"+t[13]+sv+"</b><br>"+t[14];if(S)m.innerHTML="<b>"+t[16]+sv+"</b><br>IP "+ip;if(!D&&!S)m.remove();else w.insertBefore(m,n.parentNode?n:w.children[1])});
})})();
</script>)html";

    inline char bodyHeader[96]; // device name and language for the script

    /**
     * @brief Sets up the portal; call after the stock WiFiManager settings, before the portal starts
     * @param onAp shows the setup screen on the display when the setup WiFi is up
     */
    inline void configure(WiFiManager& wm, const char* hostname, const bool english, std::function<void(WiFiManager*)> onAp) {
        char name[48]; // only what a host name may contain, the script puts it into the page
        size_t n = 0;

        for (const char* c = hostname; *c != '\0' && n + 1 < sizeof(name); ++c) {
            if (isalnum(static_cast<unsigned char>(*c)) || *c == '-' || *c == '_' || *c == '.') {
                name[n++] = *c;
            }
        }

        name[n] = '\0';
        snprintf(bodyHeader, sizeof(bodyHeader), "<i id=hn hidden data-l=%s>%s</i>", english ? "en" : "de", name);

        static const char* menu[] = {"wifi"};
        wm.setMenu(menu, 1);
        wm.setTitle("Orione");
        wm.setClass("invert");
        wm.setCustomHeadElement(kHead);
        wm.setCustomBodyHeader(bodyHeader);
        wm.setShowInfoUpdate(false);
        wm.setShowInfoErase(false);
        wm.setScanDispPerc(false);
        wm.setRemoveDuplicateAPs(true);
        wm.setBreakAfterConfig(false); // a wrong password keeps the portal open for another try
        wm.setAPClientCheck(true);     // the timeout only runs while no phone is connected
        wm.setAPCallback(std::move(onAp));
    }

    /** First setup: 10 minutes for the phone; saved WiFi out of reach (router off): stock 60 s */
    inline unsigned long timeoutSeconds(const bool firstSetup) {
        return firstSetup ? 600 : 60;
    }

} // namespace orione_portal
