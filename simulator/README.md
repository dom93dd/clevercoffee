# Simulator für das runde Display

Zeigt die UI des runden 1,28"-Displays (GC9A01, 240×240) auf dem Mac, ohne ESP32.
Es läuft **derselbe Zeichencode** wie auf dem Controller (`../lib/RoundDisplay`): Die UI zeichnet in 240×40-Pixel-Streifen, die hier in ein Bild im Speicher statt über SPI an das Panel gehen. Was im Simulator gut aussieht, sieht auf dem Display genauso aus. Offen bleiben nur Farbwiedergabe und Blickwinkel des echten IPS-Panels.

Die Maschine dahinter ist ein einfaches Modell (`src/FakeMachine.h`): Thermoblock mit PI-Regler, Aufheizen in gut einer Minute, Temperatureinbruch beim Bezug, Brew-by-Time 25 s, optional Waage mit 36 g Zielgewicht.

## Voraussetzungen
- PlatformIO (`brew install platformio`)
- SDL2 (`brew install sdl2`)

## Starten
```sh
cd repos/clevercoffee/simulator
./run.sh              # baut und öffnet das Fenster (Maßstab 2)
./run.sh --scale 3    # größer
./run.sh --lang en    # englische Texte
./run.sh --brand "CAFFÈ DOMINIK"   # anderer Schriftzug im Intro (Standard: DOMS COFFEE)
```

Tasten (stehen auch rechts im Fenster):

| Taste | Wirkung |
|---|---|
| Leertaste | Bezugsschalter an/aus (Bezug stoppt nach 25 s von selbst) |
| G | Waage in den Einstellungen an/aus (aus: Symbol durchgestrichen) |
| V | Bluetooth-Verbindung der Waage trennen/herstellen (getrennt: Symbol grau) |
| S | Dampfschalter |
| F / H | Spülen / Heißwasser |
| B | Rückspülmodus (danach Leertaste = Start) |
| W | Wassertank leer |
| E | Sensorfehler (TSIC liefert −49,9 °C) |
| O | Übertemperatur 146 °C (Notabschaltung) |
| P / Y | PID aus / Standby (Blende zu, beim Verlassen Blende auf) |
| D | Display aus/an, wie die Firmware nach 10 min Standby (Blende zu, Panel schläft; Blende auf) |
| N | WLAN-Zustand: gut → schwach → weg → Offline-Modus |
| L | Sprache DE/EN |
| Pfeil hoch/runter | Soll ±0,5 °C |
| Pfeil links/rechts | Temperatur −/+1 K (Störung) |
| T | Zeitraffer 1× / 5× / 20× |
| R | Neustart mit Intro, Startmeldungen und kalter Maschine |
| C | Screenshot als PNG in den aktuellen Ordner |

## Ohne Fenster
Vorher einmal `pio run` (baut das Programm, `./run.sh` macht das automatisch).
```sh
.pio/build/sim/program --list                      # alle Szenarien
.pio/build/sim/program --shot brew brew.png        # ein Bildschirm als PNG
.pio/build/sim/program --shot intro-1 i.png --at 900   # Bild aus einer Animation (Szenarien intro-1, reveal, close)
.pio/build/sim/program --gallery galerie.png --scale 1
.pio/build/sim/program --review ../../../review/round-display   # alle Szenarien DE+EN als PNG + index.html zum Abnehmen
#   --compare ALTER_ORDNER  markiert, was sich gegenüber einer früheren Runde geändert hat (alte Bilder nach previous/, Knopf „Vorher zeigen“)
#   --notes NOTIZEN.json    {"szenario": "was geändert wurde"} erscheint am jeweiligen Bildschirm
.pio/build/sim/program --trace                     # Ablauf Kaltstart → Bezug → Dampf → Wassertank als Text
```
Die PNGs sind unkomprimiert (einige MB); `sips -s format png a.png --out b.png` macht sie klein.

## Tests
```sh
pio test -e test                        # alle Tests (etwa 10 s)
pio test -e test -f test_ui             # nur eine Suite
RD_UPDATE_GOLDEN=1 pio test -e test -f test_screens   # Referenzbilder neu schreiben (nach gewollten UI-Änderungen)
RD_LAYOUT_BOXES=1 pio test -e test -f test_layout -v   # Abstands-Check mit Position jedes Textes
./check_firmware.sh                     # Firmware-Builds prüfen (1,5 min), --quick ohne Vergleich mit upstream/master
```
| Suite | Prüft |
|---|---|
| `test_paint` | Kantenglättung, Winkel (0° = 12 Uhr, im Uhrzeigersinn), runde und gerade Enden, Clipping an Streifen- und Bildrändern, Maske, Text; Schriftformat: UTF-8, Glyphensuche in allen acht Schriften, Breite, Tintenhöhe, Mischfarben an den Kanten |
| `test_control` | Zuordnung der Firmware-Zustände, Bezugstimer mit Haltezeit, Meldungen (Aufteilung, Großschreibung mit Umlauten, UTF-8-sicheres Kürzen), Zahlenformat DE/EN, Display-aus-Ablauf |
| `test_ui` | Bildschirmwahl (Alarme vor allem anderen), Hysterese Aufheizen/Bereit, Neuzeichnen nur bei sichtbaren Änderungen, Animationen |
| `test_screens` | alle Szenarien in DE und EN: Streifen-Rendering pixelgleich zum Vollbild, nichts außerhalb des Glases, jeder Bildschirm zeigt etwas, Vergleich mit `test/golden/*.png`, jedes Zeichen in den Schriften vorhanden, jeder Text passt in den Kreis |
| `test_layout` | Abstände auf allen Szenarien in DE und EN, gemessen an den gezeichneten Pixeln: Inhalt ≥ 8 px zu Ring, Strichen, Markern, Statussymbolen und Glasrand; Ziffern der großen Zahl ≥ 12 px zur nächsten Zeile (Komma ≥ 7 px); Texte übereinander ≥ 7 px, Zeilen eines Absatzes ≥ 3 px. Rahmen und Inhalt trennt der Painter über Ebenen (`Layer::Frame`/`Layer::Content`). Dazu das Feedback vom 01.10.2026: Schriftgröße je Zeilenart (Soll/Ziel 19 px, zweite Zahl 32 px, zu breite Meldungszeile 18 px), gleiche Zeilenumbrüche der Kalibriermeldung, gleichmäßiger Abstand bis zu den Punkten „....“ |

Weicht ein Bild von der Referenz ab, legt der Test das neue Bild unter `test/golden/failed/` ab. `check_firmware.sh` baut `esp32_usb`, `esp32_round_usb` und `esp32_round_ota`, verlangt mindestens 16 KB freien Flash für die Rund-Builds und vergleicht die Größe der normalen Firmware mit einem Build von `upstream/master` (muss gleich sein).

## Hinweis zu Xcode
Passt ein installiertes Xcode (z. B. eine Beta) nicht zur macOS-Version, funktionieren `/usr/bin/clang++` und `xcrun` nicht. `sim_flags.py` erkennt das und nimmt automatisch die Command Line Tools sowie ein zur macOS-Version passendes SDK (z. B. 26.x statt 27.0). Dauerhaft beheben lässt es sich mit `sudo xcode-select -s /Library/Developer/CommandLineTools` oder mit einem zum System passenden Xcode.
