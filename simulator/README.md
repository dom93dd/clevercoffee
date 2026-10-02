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

## Echtes Display am Mac (ESP32 + GC9A01 per USB)
Nur ESP32 DevKitC und Display, Strom über USB. Verdrahtung: VCC → 3V3, GND → GND, SCL → IO14, SDA → IO13, DC → IO4, CS → IO15, RST → IO5 (wie `src/display/roundDisplayDevice.h`).
```sh
cd esp32-bench
pio run -e demo -t upload -t monitor    # Farb-Testbild, dann alle Szenarien je 4 s; seriell die echte Zeichenzeit
pio run -e remote -t upload             # Display wird vom Simulator gesteuert:
cd .. && ./run.sh --display auto        # Tasten im Fenster wirken auf das echte Display
```
Im Modus `--display` schickt der Simulator nur den Maschinenzustand (Modell, Meldung, Display aus, Intro; `src/RemoteLink.h`, 921600 Baud, Rahmen mit Prüfsumme), der ESP32 zeichnet mit dem Code und Tempo der Firmware (ein Streifen pro Durchlauf, nur geänderte Streifen). Er meldet jede Sekunde Bilder pro Sekunde und die längste Blockade zurück, das Seitenfeld zeigt sie an.

## ESP32-Tempo: ruckelt es auf dem Chip?
```sh
./run.sh --spi 27                       # Fenster im Tempo des ESP32 (SPI 27/40/80 MHz), Taste X schaltet um
.pio/build/sim/program --tempo          # Bilder/s und Blockade von loop() je Phase, ohne Fenster
.pio/build/sim/program --bench          # Zeichenzeit und Prüfsumme je Szenario auf diesem Rechner
.pio/build/sim/program --inspect DIR    # 19 Bildbögen zur Sichtprüfung: alle Bildschirme DE/EN, Animationen, Abläufe, Grenzwerte
esp32-bench/run_qemu.sh                 # dieselben Szenarien als ESP32-Programm im Emulator (QEMU)
```
Im ESP32-Tempo wird jedes Bild hier gezeichnet, die Zeit pro Streifen gemessen und auf den ESP32 hochgerechnet (`kEsp32UsPerHostUs` in `src/Esp32Tempo.h`, aus `esp32-bench` im Emulator ermittelt). Dann läuft der Ablauf der Firmware in Echtzeit nach: ein Streifen pro `loop()`-Durchlauf, nur geänderte Streifen gehen per SPI raus, ein Streifen erscheint erst, wenn er übertragen ist. Das Seitenfeld zeigt Bilder pro Sekunde, Dauer eines Bildes und die längste Blockade von `loop()`.

`esp32-bench/` ist ein eigenes PlatformIO-Projekt mit denselben Einstellungen wie die Firmware (`-Os`). Ohne Hardware läuft es im Espressif-QEMU (`run_qemu.sh`; QEMU von https://github.com/espressif/qemu/releases nach `~/.espressif/tools/qemu-xtensa/<Version>/`, braucht `brew install libgcrypt`). Auf dem echten Dev Kit: `pio run -e bench -t upload -t monitor`, Auswertung mit `report.py --real`. Die Prüfsummen zeigen, ob der ESP32 dasselbe Bild zeichnet wie der Simulator (Pixel einer Szene vergleichen: `BENCH_FLAGS='-D BENCH_DUMP=\"ready\"' ./run_qemu.sh` und `RD_DUMP=ready .pio/build/sim/program --bench`).

## Tests
```sh
pio test -e test                        # alle Tests (etwa 15 s)
pio test -e sanitize                    # dieselben Tests mit AddressSanitizer und UBSan
pio test -e test -f test_ui             # nur eine Suite
RD_UPDATE_GOLDEN=1 pio test -e test -f test_screens   # Referenzbilder neu schreiben (nach gewollten UI-Änderungen)
RD_LAYOUT_BOXES=1 pio test -e test -f test_layout -v   # Abstands-Check mit Position jedes Textes
./check_firmware.sh                     # Firmware-Builds prüfen (1,5 min), --quick ohne Vergleich mit upstream/master
```
| Suite | Prüft |
|---|---|
| `test_ui` (Ergänzung) | ein Streifen pro Aufruf ergibt dasselbe Bild wie ein ganzer Durchlauf und zeigt einen einzigen Zeitpunkt; nur geänderte Streifen werden gesendet (0,1 °C ändern höchstens 3 von 6); Lichtreflex im Animationstakt während des Bezugs |
| `test_paint` | Kantenglättung, Winkel (0° = 12 Uhr, im Uhrzeigersinn), runde und gerade Enden, Clipping an Streifen- und Bildrändern, Maske, Text; Schriftformat: UTF-8, Glyphensuche in allen acht Schriften, Breite, Tintenhöhe, Mischfarben an den Kanten |
| `test_control` | Schutz gegen hängende `loop()` (Grenzen, Überlauf von `millis()`, OTA), Zuordnung der Firmware-Zustände, Bezugstimer mit Haltezeit, Meldungen (Aufteilung, Großschreibung mit Umlauten, UTF-8-sicheres Kürzen), Zahlenformat DE/EN, Display-aus-Ablauf |
| `test_ui` | Bildschirmwahl (Alarme vor allem anderen), Hysterese Aufheizen/Bereit, Neuzeichnen nur bei sichtbaren Änderungen, Animationen |
| `test_screens` | alle Szenarien in DE und EN: Streifen-Rendering pixelgleich zum Vollbild, nichts außerhalb des Glases, jeder Bildschirm zeigt etwas, Vergleich mit `test/golden/*.png`, jedes Zeichen in den Schriften vorhanden, jeder Text passt in den Kreis |
| `test_fuzz` | 3000 zufällige Zustände mit NaN, unendlich, riesigen und negativen Werten, kaputtem UTF-8 und Zeitsprüngen: kein Absturz, jedes Bild unter 250 ms (eine aus kaputten Werten abgeleitete Schleife würde den ESP32 hängen lassen), nichts außerhalb des Glases; Fehlerbild landet in `test/fuzz-failure.png` |
| `test_layout` | Abstände auf allen Szenarien in DE und EN, gemessen an den gezeichneten Pixeln: Inhalt ≥ 8 px zu Ring, Strichen, Markern, Statussymbolen und Glasrand; Ziffern der großen Zahl ≥ 12 px zur nächsten Zeile (Komma ≥ 7 px); Texte übereinander ≥ 7 px, Zeilen eines Absatzes ≥ 3 px. Rahmen und Inhalt trennt der Painter über Ebenen (`Layer::Frame`/`Layer::Content`). Dazu das Feedback vom 01.10.2026: Schriftgröße je Zeilenart (Soll/Ziel 19 px, zweite Zahl 32 px, zu breite Meldungszeile 18 px), gleiche Zeilenumbrüche der Kalibriermeldung, gleichmäßiger Abstand bis zu den Punkten „....“ |

Weicht ein Bild von der Referenz ab, legt der Test das neue Bild unter `test/golden/failed/` ab. `check_firmware.sh` baut `esp32_usb`, `esp32_round_usb` und `esp32_round_ota`, verlangt mindestens 16 KB freien Flash für die Rund-Builds, misst die Zeichenzeit im ESP32-Emulator (Budget: Streifen ≤ 12 ms, Bild ≤ 50 ms; übersprungen ohne QEMU) und vergleicht die Größe der normalen Firmware mit einem Build von `upstream/master` (muss gleich sein).

## Hinweis zu Xcode
Passt ein installiertes Xcode (z. B. eine Beta) nicht zur macOS-Version, funktionieren `/usr/bin/clang++` und `xcrun` nicht. `sim_flags.py` erkennt das und nimmt automatisch die Command Line Tools sowie ein zur macOS-Version passendes SDK (z. B. 26.x statt 27.0). Dauerhaft beheben lässt es sich mit `sudo xcode-select -s /Library/Developer/CommandLineTools` oder mit einem zum System passenden Xcode.
