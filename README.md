# ESP32 vehicle charge controller

Ein ESP32 verbindet einen Danfoss-TLX-Wechselrichter und einen DS100-Zähler
mit einem go-eCharger. Im Modus **PV-Adaptiv** wird das Laden anhand des
Netzbezugs bzw. der Einspeisung geregelt. Das Projekt ist eine eigenständige
Weiterentwicklung und kein Git-Fork der unten genannten Projekte.

Die Weboberfläche bietet `Aus`, `PV-Adaptiv` und `Start` mit Stromregler
(6–16 A) sowie Phasenwahl (Auto/1/3). `Start` setzt den Strom vorab und
startet; eine nötige Phasenänderung folgt erst bei bestätigter Ladefreigabe
und nach den vom go-e gemeldeten Mindestwartezeiten.
Die Steueroberfläche hat keine Anmeldung und ist nur für ein vertrauenswürdiges
lokales Netzwerk gedacht. Nicht ohne zusätzliche Absicherung ins Internet
freigeben.

## Regelverhalten

- PV-Überschuss: 60-s-Mittelwert und 500 W Sicherheitsreserve.
- Start: ausreichende Reserve muss 5 min anliegen; Start-/Stopp-Befehle liegen
  im PV-Regelbetrieb mindestens 5 min auseinander. Ein manueller Start aus
  Force-Off wartet nicht auf den Startup-Stop; nach einem bestätigten
  manuellen Stopp gilt vor einem erneuten Start eine 5-minütige Sperre.
  `Aus` bleibt ein sofortiger Stopp.
- PV-Regelung: mindestens 6 A, Anpassung in 1-A-Schritten frühestens jede
  Minute, damit schwankende Messwerte keine hektischen Änderungen auslösen.
  Manueller Start/Taster setzt den gewählten Sollstrom direkt (Taster: 16 A);
  die 5-minütige Sperre nach einem bestätigten manuellen Stopp gilt weiterhin.
- Phasen: Wechsel erst nach 10 min stabiler Anforderung und frühestens 10 min
  nach dem vorherigen Phasenbefehl. Umschaltung auf drei Phasen ab 5.2 kW,
  zurück auf eine Phase unter 5.0 kW.
- go-e-Statusabfrage: alle 15 s; ein Status älter als 45 s gilt als veraltet.
- Ein Tastendruck am go-e startet mit 16 A und Phasenwahl `Auto`. Nach einem
  manuellen Start kehrt der Controller bei erkanntem Ausstecken des Fahrzeugs
  zu `PV-Adaptiv` zurück. Ohne Ausstecken erfolgt die Rückkehr nach mindestens
  30 min mit unter 50 W PV-Eingangsleistung und anschließend 5 min mit mehr
  als 100 W.

Die Werte dokumentieren den aktuellen Firmwarestand; bitte vor dem Betrieb
mit der eigenen Anlage und den Charger-Einstellungen abgleichen.

## Bauen

PlatformIO-Projekt für `esp32dev`. Lege `src/Secrets.h` anhand von
[`src/Secrets.example.h`](src/Secrets.example.h) mit den eigenen WLAN-Daten an,
danach baue mit `pio run -e esp32dev`. `src/Secrets.h` wird nicht versioniert.

## Referenzen und Dank

Die technische Machbarkeit der Einzelkomponenten wurde vor der Integration
geprüft. Danke an die Maintainer und Mitwirkenden dieser Projekte:

- [Danfoss-TLX von AMajland](https://github.com/AMajland/Danfoss-TLX/)
- [Danfoss-TLX / TLX-ino von TorbenNor](https://github.com/TorbenNor/Danfoss-TLX) –
  Referenz der bestehenden RS485-Komponente
- [eModbus](https://github.com/eModbus/eModbus/)
- [go-eCharger API v2](https://github.com/goecharger/go-eCharger-API-v2)

Das Projekt entstand gemeinschaftlich durch Jan Dressler und mit Copilot-
Unterstützung: Die Komponenten wurden zunächst einzeln auf ihre technische
Machbarkeit geprüft und anschließend in dieser eigenständigen Anwendung
zusammengeführt.

[`PROJECT_STATUS.md`](PROJECT_STATUS.md) bleibt das ausführlichere
Entwicklungs- und Testprotokoll.
