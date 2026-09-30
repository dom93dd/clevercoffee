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
| G | Waage an/aus (Gewicht und Zielgewicht) |
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
```sh
.pio/build/sim/program --list                      # alle Szenarien
.pio/build/sim/program --shot brew brew.png        # ein Bildschirm als PNG
.pio/build/sim/program --shot intro-1 i.png --at 900   # Bild aus einer Animation (Szenarien intro-1, reveal, close)
.pio/build/sim/program --gallery galerie.png --scale 1
.pio/build/sim/program --trace                     # Ablauf Kaltstart → Bezug → Dampf → Wassertank als Text
```
Die PNGs sind unkomprimiert (einige MB); `sips -s format png a.png --out b.png` macht sie klein.

## Hinweis zu Xcode
Passt ein installiertes Xcode (z. B. eine Beta) nicht zur macOS-Version, funktionieren `/usr/bin/clang++` und `xcrun` nicht. `sim_flags.py` erkennt das und nimmt automatisch die Command Line Tools sowie ein zur macOS-Version passendes SDK (z. B. 26.x statt 27.0). Dauerhaft beheben lässt es sich mit `sudo xcode-select -s /Library/Developer/CommandLineTools` oder mit einem zum System passenden Xcode.
