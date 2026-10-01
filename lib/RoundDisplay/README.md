# RoundDisplay – UI für das runde 1,28"-Display

Oberfläche für ein rundes GC9A01-TFT (240×240, SPI) an CleverCoffee, gezeichnet mit LovyanGFX. Die Bibliothek hängt nicht von Arduino ab und läuft deshalb unverändert in der Firmware und im Desktop-Simulator (`../../simulator`).

## Aufbau
| Datei | Inhalt |
|---|---|
| `src/RoundDisplayModel.h` | `rd::Model`: Momentaufnahme der Maschine (Temperatur, Soll, Heizleistung, Bezug, Waage, Rückspülen, WLAN). Die Firmware füllt sie in `src/display/roundDisplay.h`, der Simulator in `FakeMachine.h`. |
| `src/RoundDisplayUi.*` | `rd::RoundUi`: wählt den Bildschirm, entscheidet, ob neu gezeichnet werden muss, und zeichnet. |
| `src/RoundDisplayPaint.*` | `rd::Painter`: kantengeglättete Ringe, Kreise, Linien und Text in einem Bildstreifen. |
| `src/RoundDisplayTheme.h` | Farben und Maße, vertikales Raster (Grundlinien von Titel, großer Zahl und zwei Zeilen darunter). |
| `src/RoundDisplayStrings.h`, `src/RoundDisplayFormat.h` | Texte DE/EN, Zahlenformat (Dezimalkomma im Deutschen). |
| `src/RoundDisplayControl.*` | Logik zwischen Firmware und UI ohne Hardware: Zustandszuordnung, Bezugstimer, Meldungen aufteilen, Display aus/an. Firmware und Simulator nutzen dieselbe getestete Logik. |
| `src/RoundDisplayFont.*` | `rd::Font`: eigenes kompaktes Schriftformat (4 Bit Kantenglättung, 10-Byte-Glyphen), der Painter zeichnet die Glyphen selbst. Text sitzt pixelgenau dort, wo LovyanGFX ihn mit VLW-Schriften hingesetzt hat. |
| `src/RoundDisplayFonts.*`, `src/fonts/` | Die acht Schriften (Barlow Semi Condensed, SIL OFL 1.1): groß 80, mittel 34 und 32, Text 20, 19 und 18, Hinweis 16, Titel 19 px. |
| `tools/` | Font-Generator (`make_font.py`, `build_fonts.sh`). |

## Warum Streifen?
Ein ganzes Bild in 16 Bit sind 115 KB. Das passt nicht neben WLAN, Bluetooth und Webserver in den RAM des ESP32-WROOM (kein PSRAM). Deshalb wird jedes Bild in 6 Streifen à 240×40 gezeichnet. Zwei Streifenpuffer (je 19 KB) wechseln sich ab: Während DMA den einen zum Display schickt, zeichnet die CPU den nächsten. Gezeichnet wird nur, wenn sich etwas Sichtbares ändert (Werte so gerundet, wie sie angezeigt werden), höchstens alle 80 ms.

## Bildschirme
Aufheizen (Ring von 20 °C bis Soll) · Bereit (gezoomte Skala Soll ±5 K, Punkt = Ist, grün innerhalb `display.blinking.delta`) · Bezug (der Ring zeigt, was den Bezug beendet: mit Zielgewicht das Gewicht, sonst die Zeit) · Fertig (Haltezeit `display.post_brew_timer_duration`) · Spülen · Heißwasser · Dampf · Rückspülen · Wassertank leer · Standby · PID aus · Übertemperatur · Sensorfehler · Meldungen (Start, WLAN, IP, Waagen-Kalibrierung; lange Texte werden passend zur Kreisform umgebrochen).

Waagen-Symbol: steht immer unten rechts auf der Ringbahn, spiegelbildlich und auf gleicher Höhe wie das Heizsymbol links und genauso groß (grün = Waage verbunden und bereit, grau = Bluetooth-Waage eingeschaltet, aber nicht verbunden, blassgrau durchgestrichen = keine Waage, rot = Waagenfehler). Hinweistexte (Alarme, „Kein WLAN“, „Bitte nachfüllen“ …) stehen in einer kleineren Schrift (16 px) als der übrige Text. Fortschrittsringe beginnen bündig an der Nullmarke.

Dezente Zusatzelemente: Tendenzpfeil ▲/▼ neben der Temperatur (Steigung über 5 s, nicht während „BEREIT“), Bereit-Moment (zwei Wellen am grünen Punkt, einmal nach dem Aufheizen), Lichtreflex auf dem Ring während des Bezugs, Ø-Brühtemperatur nach dem Bezug, die letzten fünf Bezüge als Punkte auf dem Bereit-Bildschirm (grün = höchstens 1,5 s vom Ziel entfernt).

Gestaltungsregeln: große Ziffern für das, was man aus einem Meter Entfernung sehen muss. Farbe nur für den Zustand (bernstein = heizt, grün = bereit, crema = Bezug, blau = Wasser, rot = Alarm). Verbindungsprobleme erscheinen nur, wenn es wirklich eins gibt. Alarme haben Vorrang vor allem anderen.

## Animationen
| Animation | Wann | Dauer |
|---|---|---|
| Intro | beim Einschalten, vor der Versionsmeldung (blockiert `setup()` so lange) | 1,7 s |
| Blende auf | erster Bildschirm nach den Startmeldungen; Verlassen des Standby; Display wieder an | 0,9 s |
| Blende zu | Wechsel in den Standby; Display aus (10 min nach Standby) | 0,8 s |

Schriftzug: `ROUND_DISPLAY_BRAND` in `src/display/roundDisplay.h` (Standard „DOMS COFFEE“), im Simulator `--brand`. Beim Intro fährt der Skalenring auf Vollausschlag wie bei einem Instrumenten-Selbsttest, der Schriftzug blendet ein, ein Lichtreflex läuft über den Ring, dann zieht sich der Ring auf die Versionsmeldung zurück. Blende auf öffnet eine runde Blende mit Crema-Rand, die Temperatur zählt dabei hoch. Blende zu schließt sie, ein Punkt glimmt nach. Alarme zeigen sich immer sofort, ohne Animation. Einzelbilder aus den Animationen: `simulator/.pio/build/sim/program --shot intro-1 bild.png --at 900` (Szenarien `intro-1`, `reveal`, `close`).

Ausschalten mit dem Hauptschalter kann keine Animation zeigen: Das Netzteil hängt hinter S1, der ESP32 ist sofort stromlos.

## Schriften neu erzeugen
```sh
pip install freetype-py
./tools/build_fonts.sh
```
Größen und Zeichensätze stehen in `build_fonts.sh`. Ziffern werden gleich breit gemacht, damit Zahlen beim Zählen nicht springen.

## Speicher (Stand 01.10.2026, Env `esp32_round_usb`, mit Bluetooth)
Flash 1.666.841 von 1.703.936 B (97,8 %, ~37 KB frei); 4.0.3 ohne rundes Display: 1.545.069 B. Davon Schriften 44,5 KB (acht Schriften im kompakten Format; die fünf VLW-Schriften vorher brauchten 60,1 KB), LovyanGFX ~40 KB, UI ~13 KB. Zur Laufzeit kommen 2 × 19 KB Streifenpuffer dazu (Heap, nur auf echter Hardware messbar).

## Tests
89 Tests in `simulator/test` (Zeichnen, Schrift, Logik, UI-Verhalten, alle Bildschirme gegen Referenzbilder, Abstands-Check aller Bildschirme, Schriftgröße je Zeilenart), dazu `simulator/check_firmware.sh` für die Firmware-Builds. Start mit `cd simulator && pio test -e test`, Anleitung in `simulator/README.md`.
