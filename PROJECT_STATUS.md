# PV-Ladecontroller – Projektstatus und nächste Schritte

## Ziel

Ein ESP32 soll Messwerte eines Danfoss TLX / IBC Servemaster und des
B+G E-TECH DS100-00B-R zusammenführen, diese über eine Weboberfläche
bereitstellen und einen go-e Charger lokal über dessen HTTP-API steuern.
Die RS485-Geräte werden abgefragt; CAN ist nicht Teil des Vorhabens.

Die Projekte sind Prototypen, keine fertigen Komponenten. Als späteres
Integrationsziel ist dieses Projekt vorgesehen:

- `250322-211201-esp32dev/`: enthält bereits eine Kopie des TLX/REST-Projekts.
- `DanfossTLX-REST-LolinD32/`: separater TLX/REST-Versuch, vom Benutzer
  zuvor erfolgreich getestet.
- `suessberg9_powermeter/`: separater DS100-Modbus-Versuch.

Die beiden TLX-Kopien unterscheiden sich. Bei der späteren Integration
Änderungen vergleichen und nicht unbesehen eine Kopie überschreiben.

## Stand vom 08.11.2026

### Aktueller Anlagenzustand

- Laut aktueller Rückmeldung ist alles angeschlossen. Bei hellem Wetter wird
  inzwischen ins Netz rückgespeist.
- Die Rückspeisung wurde live am DS100 gemessen und vom Benutzer mit dem
  Display abgeglichen: Register `0x0420` zeigt dabei negative Leistung.
- DS100 und TLX sind wieder gemeinsam am A/B-Bus angeschlossen. Die kombinierte
  ESP32-Firmware fragt beide Geräte nacheinander mit jeweils passender
  Baudrate ab; Livewerte beider Geräte sind über die Weboberfläche/JSON
  erreichbar.
- go-e-API-Lesen sowie manuelle Schreibtests direkt gegen die lokale API für
  `amp`, alle drei `frc`-Zustände und `psm` sind getestet. Der interne
  `psm`-Schreibpfad mit PV-Überschuss-Hysterese ist implementiert, geflasht
  und live geprüft; die Ladebetriebsarten und deren Bedienung sind noch nicht
  vollständig implementiert. Die erste konservative PV-Stromregelung ist
  freigegeben, gebaut und geflasht; automatische `amp`-/`frc`-Schreibbefehle
  sind im PV-Modus aktiv.
- Die kombinierte Firmware fragt ausgewählte go-e-API-Werte gefiltert mit
  einer Zielperiode von 15 s ab; ungefiltertes `/api/status` wird nicht
  verwendet. `nrg[11]` wird während des Ladens zusammen mit dem DS100-Saldo
  gebraucht, um den verfügbaren Überschuss zu berechnen; Chargerstatus und
  Sollwerte dienen Bestätigung und Anzeige. Die vorherige 5-s-Periode war für
  diese Zwecke unnötig häufig. Der Status gilt nach 45 s ohne erfolgreiche
  Aktualisierung als veraltet.
- Der go-e-Status wurde am 08.11.2026 für rund 70 s bei angeschlossenem
  Fahrzeug mitgelesen; die Firmware sendet dabei ausschließlich GET-
  Lesezugriffe.
- Nach dem manuellen Ladestopp bei weiterhin eingestecktem Fahrzeug meldete
  der Charger `car=4` (`Complete`), `alw=false`, `err=0`; `acu` war in der
  direkten API-Antwort `null`.
- Nach einem TLX-only-Test und erneutem Flashen/Neustart der kombinierten
  Firmware liefern TLX und DS100 wieder gültige Livewerte; der TLX meldet
  `On Grid` (Modus 60), der DS100 etwa `-330 W` Einspeisung. Der genaue Grund
  für den vorangegangenen Kommunikationsausfall ist noch nicht ermittelt.
- go-e-Statusauswertung erweitert und live geprüft: `acu=null` im Zustand
  `Complete` ist ein gültiger optionaler Wert, und der gültige Chargerstatus
  bleibt dabei erhalten. Der synchrone HTTP-Poll läuft nach fälligen TLX- und
  DS100-Abfragen; beobachtete erfolgreiche HTTP-Aufrufzeiten lagen zwischen
  89 und 490 ms.
- Vorbereitend für eine spätere Tasterbedienung wird `alwt` gefiltert gelesen
  und bei Änderungen zusammen mit `alw` protokolliert; Änderungen an `amp`
  werden ebenfalls erkannt. `alwt` ist laut API der Zeitpunkt der letzten
  Ladefreigabe-Änderung, nicht der Tastendruck selbst. Die englische aktuelle
  API-Key-Tabelle dokumentiert außerdem `lbp` als letzten Tastendruck in
  Millisekunden.
- LED-Doku geprüft: `amp` beeinflusst die LED-Ring-Anzeige, ist aber zugleich
  die R/W-Sollstrom-Konfiguration und daher kein sicherer LED-only-Regler.
  `led` ist als Legacy-Status (LED-Animation) dokumentiert, wurde vom Gerät
  mit Firmware 60.6 im gefilterten Statusabruf aber nicht zurückgegeben.
  Eine dokumentierte separate LED-Steuerung ist bislang nicht gefunden.
- Diagnose am 08.11.2026: `Preferences.begin()` und auch direkter
  `nvs_open()` blockieren den ESP vor dem WLAN-Start; dasselbe geschah nach
  gezieltem Löschen der 20-KiB-NVS-Partition. Auch `esp_partition_read()`
  blockierte bei einem separaten Diagnoseversuch. Die NVS-Partition wurde vor
  diesen Tests gesichert und anschließend vollständig wiederhergestellt; die
  Standard-Partitionstabelle ist aktiv. Die Ursache des Flash-API-Hängers ist
  ungeklärt; eine persistente Flash-Lösung wird daher nicht verwendet.
  Stattdessen startet die Firmware mit gesperrter PV- und Phasenregelung.
  Erst ein frischer go-e-Status mit Force-Off
  (`frc=1`, `alw=false`) gibt die Regelung wieder frei; andernfalls wird
  Force-Off angefordert und auf die Bestätigung gewartet. Diese Recovery
  wurde nach dem Flash live geprüft. Der go-e meldete `Complete`, Fehler 0;
  `/All` zeigte `pv_control_state=WAITING_FOR_START_RESERVE` und einen
  gültigen DS100-Wert von etwa `+1.414 W` Netzbezug.
- Begrenzter PV-Simulationstest live durchgeführt: synthetischer Netzsaldo
  `-2.500 W`, Force_1, 8-A-Obergrenze und 15-min-Testlimit. Der Benutzer
  bestätigte den ausgewählten 8-A-Wert in der App. Nach rund fünf
  Minuten Start-Haltezeit startete der Charger real mit `frc=0`, `alw=true`,
  Fahrzeugstatus `Charging`; die Sollstromstufen 6 A und 7 A wurden im
  Controllerstatus beobachtet. Während des Ladens zeigte der echte DS100
  ungefähr `+0,8` bis `+1,2 kW` Netzbezug; DS100 blieb gültig, Fehlerzähler 0,
  go-e-Fehler 0. Die Überwachung brach wegen einer zeitweise unvollständigen
  `/All`-Antwort ab; daraufhin wurde der Test manuell gestoppt. Direkte
  go-e-Abfrage bestätigte anschließend `frc=1`, `alw=false`, `err=0` und
  `nrg`-Leistungs-/Stromwerte 0; `amp=8` war der zuletzt gesetzte Sollwert.
  Der DS100 war gültig und der go-e meldete `Complete`. Das Testlimit von
  15 Minuten wurde nicht ausgeschöpft. Nach erfolgreichem Test wurden alle
  synthetischen Eingabe-/Start-/Stop-Routen, Teststatusfelder und UI-Elemente
  aus der Firmware entfernt; die echte PV-Regelung bleibt aktiv.

### DS100-00B-R

- Der Zähler ist mit dem ESP32 und einem bereits bewährten
  3,3-V-TTL/RS485-Wandler verbunden.
- Erfolgreicher Test über Modbus RTU mit 9.600 Baud, 8N1, Geräteadresse 1.
- Getesteter Abruf: Input Registers (Function Code 04), Register `0x0420`,
  zwei Register (32 Bit).
- Der Zählerdisplaywert von etwa `+0,240 kW Bezug` stimmte mit den
  seriellen Messungen von ungefähr `+0,239 bis +0,253 kW` überein.
  Anschluss, Abruf und Skalierung sind damit für diesen Bezugsfall plausibel
  bestätigt.
- Der Testcode wurde anschließend bereinigt, gebaut und auf das Board
  geflasht. Die serielle Ausgabe zeigt nun den Registerwert mit Vorzeichen
  in Watt und kW. Eine Live-Prüfung ergab etwa `+0,319 bis +0,323 kW Bezug`.
- Der verwendete ESP32 wurde beim Flashen als `ESP32-D0WD`, Revision 1.0,
  erkannt. Der Test verwendet GPIO16/17 für UART2 und GPIO4 für DE/RE.
- Beim DS100-Einzeltest lief der Prototyp aus
  `suessberg9_powermeter/src/main.cpp`; für die kombinierte Firmware wurden
  dessen validierte Register und Skalierungen in das Integrationsprojekt
  übernommen.
- Live-Prüfung am 08.11.2026 gegen 08:32: Bei der vom Benutzer beschriebenen
  Rückspeisung lieferte Register `0x0420` über sechs aufeinanderfolgende
  Abfragen Werte von `-166` bis `-193 W` (`-0,166` bis `-0,193 kW`).
  Damit ist ein negatives Vorzeichen im aktuellen Betriebszustand bestätigt.
- Der Benutzer bestätigt, dass das DS100-Display zeitgleich exakt dieselben
  Werte anzeigt. Vorzeichen, Skalierung und Abfrage von Register `0x0420` sind
  damit für Bezug und Einspeisung plausibel bestätigt.
- Registeradressen und Skalierungen wurden anhand der DS100-Zuordnung im
  [volkszaehler/mbmd-Treiber](https://github.com/volkszaehler/mbmd/blob/af19f17edbe0c8fa404813d70428d620db44a622/meters/rs485/ds100.go)
  geprüft: Phasenspannungen `0x0400/02/04`, Gesamtspannung `0x040C`,
  Phasenströme `0x0410/12/14`, Gesamtstrom `0x0418`, Phasenleistungen
  `0x041A/1C/1E` und Gesamtleistung `0x0420`. Spannung und Strom sind
  32-Bit-Werte mit Skalierung 1.000; Wirkleistung ist vorzeichenbehaftet in W.
- Live-Diagnose: `0x0400/02/04` ergaben ungefähr `225,7/225,7/226,1 V`;
  `0x040C` lag bei rund `225,8 V`. `0x0410/12/14` ergaben
  `-0,442/-0,594/-0,563 A`. Das passt zu den vom Benutzer am Display
  abgelesenen etwa `225 V` und `-0,4` bis `-0,6 A` je Phase.
- `0x041A/1C/1E` lieferten bei einer Messfolge `-7/-125/-113 W`; ihre Summe
  `-245 W` stimmte mit `0x0420` (`-245 W`) überein. Das bestätigt die
  Phasenleistungs-Zuordnung plausibel. Die Werte werden zeitlich nacheinander
  abgefragt, sind also kein exakt gleichzeitiger Phasen-Snapshot.
- Der Gesamtstrom `0x0418` wird vom Treiber ebenfalls mit Faktor 1.000 als
  vorzeichenbehafteter Strom interpretiert, seine Bedeutung wurde aber noch
  nicht gegen die Anzeige verifiziert. Für Regelung zunächst die bestätigte
  Gesamtwirkleistung `0x0420` nutzen.
- Phasenspannungen, Phasenströme und Phasenleistungen sollen einzeln erfasst
  und in der Oberfläche bzw. optionalen Historie sichtbar gemacht werden. Die
  spätere Ladeentscheidung verwendet dagegen die saldierte Gesamtwirkleistung
  `0x0420` am Netzanschlusspunkt: Import/Export über alle Phasen wird
  verrechnet; einzelne Phasenströme sind für die Überschussregelung nicht das
  maßgebliche Signal.

**Noch nicht verifiziert:** Gesamtstrom `0x0418` gegen Anzeige,
Energiezähler und weitere Messgrößen. Die einzelnen Phasenwerte dienen
zusätzlich der Anzeige/Historie; Regelgröße für den Netzsaldo bleibt die
Gesamtwirkleistung `0x0420`. Das Suffix `-R` steht laut Datenblatt für eine
gegenüber anderen Varianten umgekehrte Anschlussrichtung.

Den Zähler vorerst auf **9.600 Baud** belassen. Eine Änderung auf 19.200 Baud
ist ein späterer, separater Test und nicht Voraussetzung für den nächsten
Schritt.

### Danfoss TLX / IBC Servemaster

- Der separate TLX/REST-Prototyp wurde vom Benutzer über RS485 erfolgreich
  getestet.
- Er liest Messwerte nacheinander und stellt sie über eine Weboberfläche sowie
  JSON-Endpunkte wie `/All` bereit.
- Der vorhandene Treiber nutzt 19.200 Baud und beschreibt UART-Pins 16/17.
- Die vorhandenen JSON-Messwerte werden teilweise als formatierte Strings
  serialisiert. Für Regelung und gemeinsame Darstellung soll ein gemeinsames,
  typisiertes Datenmodell mit Zahlenwert, Einheit, Zeitstempel und
  Gültigkeitsstatus verwendet werden.
- Der TLX wurde in diesem Projekt nun separat am RS485 getestet. Der TLX-Treiber
  nutzt 19.200 Baud; der DS100 arbeitet mit 9.600 Baud und Modbus.
- Die Geräte teilen sich grundsätzlich einen A/B-Bus. Für den TLX-Einzeltest
  wurde der DS100 abgeklemmt und anschließend wieder angeschlossen.
- Die kombinierte Abfrage über den gemeinsamen Bus ist implementiert und
  live getestet: TLX bei 19.200 Baud, anschließend DS100 bei 9.600 Baud,
  danach Rückschaltung zum TLX. Der DS100 bleibt auf 9.600 Baud.
- Um die Busbelegung kurz zu halten, wird der DS100 nach jeweils sechs
  TLX-Messabfragen gelesen; die Modbuswerte kommen einzeln und mit Retry.
  Einzelne erste Versuche können mit Timeout ausfallen; ein Retry kann den
  Kanal im selben Zyklus erfolgreich aktualisieren.
- Einzeltest am 08.11.2026: Nach Abklemmen des DS100 wurde die TLX-Firmware
  gebaut und auf den ESP32 geflasht. Der TLX meldet sich im WLAN
  (`192.168.2.112`); HTTP-Webseite, `/All` und Einzelendpunkte wie `/GridP`,
  `/PvV1` und `/PvA1` antworten.
- Livewerte aus `/All`: Betriebsmodus `On Grid`; PV-Eingang 1 etwa
  `278–281 V` und `0,937 A`, Eingang 2 etwa `296 V` und `0,792 A`, Eingang 3
  `0 V`/`0 A`. Aus Spannung mal Strom ergibt sich überschlägig eine
  DC-Eingangsleistung von rund `0,50 kW`. Der TLX meldete `GridP` etwa
  `472–477 W`; die Phasenwerte `GridP1/2/3` waren `145/166/161 W`.
- Die `/All`-Antwort enthält Messwerte als formatierte Strings; einzelne
  Endpunkte liefern Zahlen samt Einheit. PV-DC-Leistung und Wechselrichter-
  Netzleistung sind unterschiedliche Größen. `GridP` ist nicht als
  saldierter Hausanschlusswert bestätigt; für Import/Export und spätere
  Laderegelung bleibt der DS100 am Netzanschlusspunkt maßgeblich.
- Diagnose am 08.11.2026: Bei der kombinierten Firmware gab es wiederholte
  `ERR RX Timeout`-Meldungen nach etwa 76–77 ms mit null empfangenen Bytes.
  Ein isolierter TLX-only-Test am selben ESP32/RS485-Anschluss lieferte
  dagegen in mehreren Messungen `On Grid` (Modus 60), PV-Eingang 1 etwa
  `277–282 V`/`0,86–0,93 A`, Eingang 2 etwa `293–299 V`/`0,70–0,78 A` und
  `GridP` etwa `424–429 W`.
- Nach Wiederherstellung und Neustart der kombinierten Firmware waren
  `/All`-Messungen erneut plausibel und wiederholt aktuell: `On Grid`,
  `GridP` etwa `440–468 W`, zwei aktive PV-Eingänge sowie DS100-Export von
  etwa `-309 bis -334 W`. Im seriellen Mitschnitt trat noch ein einzelner
  Timeout für `ProdTday` auf; ein dauerhafter TLX-Ausfall ließ sich nicht
  reproduzieren. Ursache des vorigen Ausfalls bleibt unklar.
- Firmwarefehler bei der Statusanzeige gefunden: Wenn die Abfrage des
  Betriebsmodus fehlschlägt, bleibt `TLX.Raw[OpMode]` beim initialen Wert 0.
  `GetParameters()` wertet diesen Wert trotzdem als `OFFGRIDOFF` aus und
  setzte `OpModeTxt` auf `Off Grid, OFF`. Korrigiert: Bei fehlender oder
  ungültiger Betriebsmodus-Antwort wird jetzt `TLX communication unavailable`
  ausgegeben statt ein falscher Anlagenzustand. Die Änderung wurde gebaut,
  geflasht und mit gültigem Modus 60 live geprüft.
- Der Benutzer bestätigt, dass die angezeigte TLX-Leistung mit dem
  Wechselrichterdisplay übereinstimmt.
- Der Wechselrichter hat zwei PV-Strings. Daher sind `PvV3` und `PvA3`
  erwartungsgemäß null; dies ist kein fehlender L3-Netzwert. Netzwerte für
  L1–L3 (`GridV1/2/3`, `GridA1/2/3`, `GridP1/2/3`) sind eigene Messkanäle.
- Liveprüfung der kombinierten Firmware am 08.11.2026: `/All` lieferte TLX-
  und DS100-Werte gleichzeitig; alle zehn DS100-Kanäle waren gültig.
  Beispiel: Phasenleistungen `-91/-175/-95 W` summierten sich zu `-361 W`,
  während Gesamtleistung `-362 W` betrug. Die Weboberfläche und HTTP-API
  blieben erreichbar.
- Antwortlängen des eModbus-Clients enthalten keine CRC-Bytes; eine zunächst
  zu strenge Längenprüfung wurde korrigiert. Bei Modbus-Fehlerantworten
  erscheinen Werte explizit als `null` und `valid:false`, nicht als veraltete
  Messzahl.

### go-e Charger

- Ziel ist die lokale HTTP-API v2 über WLAN; die API muss am Charger aktiviert
  sein.
- Modell: go-eCharger 11 kW, lokale IP `192.168.2.190`, Firmware `60.6`;
  HTTP-API v2 ist aktiviert.
- Die gefilterte Statusabfrage ist im kombinierten ESP32-Projekt integriert
  und live bestätigt. Sie liest nominal alle 15 s nur
  `fwv,typ,alw,alwt,amp,acu,frc,car,err,nrg,lccfc,lcctc,psm,fsp,lfspt,rbt,mptwt,mpwst,pnp,pwm`;
  ungefiltertes `/api/status` wird nicht verwendet. Der Poll ist synchron,
  wird aber erst nach fälligen TLX- und DS100-Abfragen ausgeführt. Der
  vorherige 5-s-Poll wurde reduziert, da häufiger Status für die langsamen
  Regelentscheidungen keinen Nutzen brachte.
- Live-Status am 08.11.2026: `alw=true`, angeforderter und erlaubter Strom
  jeweils `6 A`, Fehlercode `0`, Fahrzeugstatuscode `1` (`Idle`).
  Laut [go-e-API-v2-Dokumentation für Firmware 60.4](https://github.com/goecharger/go-eCharger-API-v2/blob/main/API_KEYS_FIRMWARE/apikeys_Firmware_60.4_sorted.md)
  sind alle definierten
  `car`-Codes: `0` Unknown/Error, `1` Idle, `2` Charging, `3` WaitCar,
  `4` Complete, `5` Error, `6` Initializing. Nicht definierte Zahlen bleiben
  mit Rohcode sichtbar und werden als `Unrecognized` bezeichnet.
- Das gefilterte `nrg`-Feld wird als 16 rohe Integerwerte mitgeloggt und in
  `/All` ausgegeben; die Indizes werden noch nicht gedeutet. Ohne Fahrzeug
  enthielt der Livecheck `[227,225,226,0,...]`.
- Fahrzeugtest am 08.11.2026, 09:21–09:22: In allen erfolgreichen
  Statusproben wurde `car=2` (`Charging`) gemeldet, `alw=true`, Soll- und
  erlaubter Strom jeweils `6 A`, Fehlercode `0`. Eine Beispielantwort für
  `nrg` war `[227,225,225,0,5,0,0,1200,0,0,0,1250,100,3,3,67]`; die
  Einzelwerte bleiben bis zum Abgleich mit der API-Dokumentation uninterpretiert.
- Während der etwa 70 s traten zwei go-e-HTTP-Timeouts auf; die folgenden
  Statusabfragen waren jeweils wieder erfolgreich. Die vorherige synchrone
  Abfrage lag vor den RS485-Abfragen und konnte fällige TLX-Lesevorgänge
  verzögern. Sie wurde hinter die TLX-/DS100-Abfragen verschoben.
- Nach manuellem Ladestopp bei eingestecktem Fahrzeug lieferte die direkte
  API-Abfrage `car=4`, `alw=false`, `err=0`, `acu=null` und `nrg` mit
  Nullwerten. Der Parser akzeptiert `acu=null` und erhält den Status nun als
  gültig; `/All` liefert `allowed_current_a:null`, `car_state:4` und
  `car_state_text:"Complete"`. Zusätzlich werden optionale `car:null`- und
  `lccfc`/`lcctc`-Werte unterstützt. Die beiden Zeitstempel werden in
  `/All`, Weboberfläche und serieller Diagnose angezeigt.
- Die deutsche API-Tabelle beschreibt `alwt` als Zeitstempel der letzten
  Änderung von `alw`; der Livewert am Charger lag bei `44642246` (API-Einheit
  wird hier unverändert übernommen). Dieser Wert wird nun abgefragt und bei
  Änderung geloggt. Änderungen von `amp` zwischen Samples werden ebenfalls
  geloggt, damit spätere Bedienversuche beobachtbar sind. Das kann Änderungen
  erfassen, beweist aber allein nicht, dass die physische Taste die Ursache
  war.
- Begrenzter Tastertest mit Firmware 60.6: Vor und nach einem einzelnen
  Tastendruck wurde der gefilterte Status direkt gelesen. `lbp` war in beiden
  Antworten `null`, obwohl die aktuelle englische API-Key-Tabelle `lbp` als
  letzten Tastendruck in Millisekunden dokumentiert. `amp=6`, `alw=false`,
  `frc=1` und `car=4` blieben gleich. `alwt` wechselte von `44642246` auf
  `0`, obwohl `alw` unverändert blieb; dieser einzelne Wertwechsel ist daher
  kein belastbarer Nachweis eines Taster-Events. Der spätere Test mit
  Firmware 57.1 bestätigte `lbp` live.
- Nach der vom Benutzer gestarteten Aktualisierung auf Firmware `57.1` war
  der Charger am 08.11.2026 wieder erreichbar. Erste gefilterte Probe:
  `car=2` (Charging), `alw=true`, `acu=6 A`, `amp=6 A`, `frc=0`, `err=0`,
  `lbp=null`. Da das Fahrzeug zu diesem Zeitpunkt lud, wird der nächste
  Tastervergleich erst nach manuellem Stopp durch den Benutzer in der App
  durchgeführt. Es wurde kein Schreibbefehl gesendet.
- Tastervergleich mit Firmware `57.1` nach App-Stopp: Baseline
  `alw=false`, `amp=6`, `frc=1`, `car=4` (Complete), `lbp=null`,
  `rbt=142680`. Nach einem einzelnen physischen Tastendruck:
  `alw=true`, `amp=6`, `frc=0`, `car=2` (Charging), `lbp=160073`,
  `rbt=175291`, `err=0`. Damit hat der Taster in dieser Konfiguration das
  Laden wieder freigegeben; das Fahrzeug begann erneut mit 6 A zu laden.
  `lbp` wird von Firmware 57.1 live geliefert und ändert sich bei der
  Betätigung; die API-Key-Tabelle beschreibt es als `lastButtonPress` in
  Millisekunden. Der Wert lag nahe dem `rbt`-Laufzeitwert. Firmware 60.6
  lieferte beim damaligen Tastendrucktest dagegen `lbp=null`. Kein
  API-Schreibbefehl wurde gesendet.
  Nach dem anschließenden Stopp in der App zeigten zwei direkte Proben
  `alw=false`, `acu=null`, `frc=1`, `lbp=160073`, `err=0`, aber weiterhin
  `car=2`. Die Freigabe wurde also zurückgenommen; der Fahrzeugstatus war in
  diesen Proben noch `Charging` und ist als möglicher verzögerter/nicht
  synchroner Status zu behandeln, nicht als bestätigtes `Complete`. Eine
  spätere Probe meldete schließlich `car=4` (Complete), bei weiterhin
  `alw=false`, `amp=6`, `frc=1`, `lbp=160073`, `err=0`.
- Die englische aktuelle API-Key-Tabelle beschreibt `bac` als R/W-Sperre für
  Stromänderungen per Taste: `0=AlwaysLock`, `1=LockWhenCarIsConnected`,
  `2=LockWhenCarIsCharging`, `3=NeverLock`. `sdp` ist die entsprechende
  R/W-Sperre für Force-/Startänderungen per Taste mit denselben Enum-Werten;
  `bar` betrifft separat die Sperre des WLAN-AP-Resets per Taste. In der App
  wurden vom Benutzer „Ladevorgang starten“ und „Strompegeländerung während
  Laden“ deaktiviert. Die Liveprobe unter Firmware 57.1 zeigte dazu passend
  `sdp=0`, `bac=0`, `bar=3`, `lbp=160073`, `lbl=null`. Vor dem erneuten
  Tastendruck waren `alw=false`, `amp=6`, `frc=1`, `car=4` (Complete).
  Danach änderte sich `lbp` auf `540504`, während `sdp=0`, `bac=0`,
  `alw=false`, `amp=6`, `frc=1`, `car=4` und `err=0` unverändert blieben.
  Damit ist unter diesen App-Einstellungen der Tastendruck per `lbp`
  erkennbar, ohne die beobachtete Ladefreigabe oder Stromvorgabe zu ändern.
  Keine API-Schreibbefehle oder Einstellungsänderungen durch den ESP32.
- Browser-Refresh: `/` und `/All` geben den bereits im ESP32 zwischengespeicherten
  Status aus; die Handler starten keine TLX-, DS100- oder go-e-Abfrage. Sie
  laufen jedoch synchron innerhalb von `server.handleClient()` in derselben
  Hauptschleife wie die Messabfragen. Ein langsamer HTTP-Client kann deshalb
  den nächsten Messzyklus verzögern und dessen Zeitabstand verschieben, erzeugt
  aber keine parallele Überschreibung oder Vermischung von Messwerten. Die
  wechselnde Antwortdauer ist somit eher eine mögliche Aktualitäts-/Timing-
  Abweichung als ein Daten-Glitch.
- LED-Ring: API-Key `amp` ist laut Dokumentation R/W und beeinflusst die
  Ringanzeige, steuert aber zugleich das gewünschte Stromlimit. `led` ist nur
  als Legacy-R-Status beschrieben und wurde beim Live-Filtertest mit
  Firmware 60.6 nicht geliefert. Deshalb wird `amp` nicht als bloßer
  LED-Steuerbefehl verwendet; eine separate LED-Steuerung ist ungeklärt.
- 60-s-Lauf nach der Änderung der Aufrufreihenfolge: 35 gültige go-e-Proben,
  zwei go-e-Abfragefehler und ein fehlgeschlagener DS100-Registerversuch;
  keine TLX-Timeouts oder ungültigen TLX-Werte. Erfolgreiche HTTP-Aufrufe
  dauerten 101–490 ms. Fehler werden jetzt ebenfalls samt Laufzeit geloggt.
- Eine Zielperiode von 5 s ist für die langsamen Fahrzeugzustände ausreichend;
  ein Poll unter 1 s ist unnötig und erhöht nur die synchrone Netzwerkaktivität.
- 65-s-Livecheck mit Zielperiode 5 s: 10 gültige go-e-Proben, keine go-e-
  Abfragefehler und keine TLX-Timeouts. Sechs erste DS100-Versuche schlugen
  fehl, wurden aber durch die vorhandenen Retries abgefangen; zeitgleiche
  `/All`-Proben zeigten gültige, frische DS100-Leistung von etwa `-357` bis
  `-381 W` und TLX-Leistung von `473` bis `483 W`.
- Nach dem letzten Flash weitere 40 s geprüft: sechs gültige go-e-Proben,
  effektive Abstände 6,26–6,51 s, keine go-e-Abfragefehler. In diesem kurzen
  Mitschnitt traten ein TLX-Timeout/ungültiger Rohwert sowie drei fehlgeschlagene
  DS100-Erstversuche auf; der zeitgleiche `/All`-Abruf zeigte danach trotzdem
  gültige Werte für TLX (`On Grid`, `GridP` 417 W), DS100 (Saldo `-218 W`) und
  go-e (`Complete`, `alw=false`, `err=0`). Das bestätigt die gemeinsame
  Livefunktion, ersetzt aber keinen längeren Stabilitätstest.
- Der synchrone HTTP-Aufruf blockiert weiterhin den Hauptloop für die
  jeweilige Dauer und kann damit Messungen verzögern; er verändert aber nicht
  UART-Baudrate oder RS485-Konfiguration. Vor der Änderung lag der HTTP-Poll
  vor den TLX-Abfragen und konnte fällige TLX-Lesevorgänge verschieben. Im
  anschließenden 60-s-Lauf nach dem Verschieben hinter TLX/DS100 gab es keine
  TLX-Timeouts. Das spricht dafür, dass die alte Reihenfolge zu Verzögerungen
  beigetragen haben kann, beweist aber nicht, dass sie den vorausgegangenen
  zeitweisen Ausfall verursacht hat.
- Das Statusobjekt erscheint mit Gültigkeit, Aktualisierungszeit und Fehler
  in `/All` und auf der Weboberfläche. Es werden ausschließlich GET-
  Lesezugriffe ausgeführt; Weboberfläche und JSON liefern Zustandscode und
  Text, Freigabe, Soll-/Iststrom (Iststrom kann gültig `null` sein),
  Force-/Fehlercode, Statusalter, rohe `nrg`-Werte und Ladeübergangszeiten.
  Charger-Steuerbefehle in der ESP32-Firmware sind nicht implementiert;
  Schreibtests wurden manuell mit der lokalen Charger-HTTP-API durchgeführt.
- Manueller API-Schreibtest am 08.11.2026 mit Firmware 57.1: Vorher wurde
  `fwv=57.1`, `amp=6`, `alw=false`, `frc=1`, `car=4` (Complete), `err=0`
  gelesen. Nach ausdrücklicher Freigabe wurde `GET /api/set?amp=6` gesendet;
  die API antwortete `{"amp":true}`. Nach 2 s war der Readback weiterhin
  `amp=6`, `alw=false`, `frc=1`, `car=4`, `err=0`.
- Danach wurde `amp=7` zunächst bei Force-Off gesetzt und zurück auf `6`
  gestellt; der Readback bestätigte beide Werte, ohne die Ladefreigabe zu
  ändern. Anschließend wurde ein kurzer aktiver Test bei 6 A gestartet:
  `GET /api/set?frc=2` lieferte `{"frc":true}`, Status `alw=true`, `acu=6`,
  `car=2`. Während des Ladens änderte `amp=7` den Status nach 3 s auf
  `amp=7`, `acu=7`; nach Rückstellung auf `amp=6` meldete der Charger wieder
  `acu=6`. `GET /api/set?frc=1` lieferte `{"frc":true}` und stoppte die
  Freigabe (`alw=false`, `acu=null`, `amp=6`, `frc=1`, `err=0`, `nrg` ohne
  Ladeleistung). Der Status `car=2` blieb in den folgenden kurzen Proben
  stehen, obwohl `alw=false`/`acu=null` und `nrg` ohne Ladeleistung waren;
  als verzögerten Fahrzeugstatus behandeln.
- `frc=0` (Neutral) wurde anschließend aus dem Force-Off-Zustand getestet.
  Die API antwortete `{"frc":true}`; nach 3 s meldete der Charger
  `frc=0`, `alw=true`, `acu=6`, `car=2` und begann wieder zu laden.
  `frc=1` stellte den sicheren Force-Off-Zustand wieder her. Nach 9 s waren
  `alw=false`, `acu=null`, `amp=6`, `frc=1`, `err=0` und alle `nrg`-Werte
  wieder null/0; `car=2` blieb als verzögerter Fahrzeugstatus stehen.
  `frc=0` bedeutet damit nicht „Laden stoppen“, sondern übergibt an die
  normale Charger-Logik, die in diesem Test die Ladung freigab. Alle drei
  `frc`-Werte und `amp`-Schreibzugriffe sind manuell live bestätigt; die
  ESP32-Firmware hat weiterhin keinen Schreibpfad.
- Phasenpräferenztest unter Firmware 57.1: Die gültigen `psm`-Werte sind
  `Auto=0`, `Force_1=1`, `Force_3=2`. Ein Schreibversuch auf `fsp=true`
  wurde mit HTTP 500 abgelehnt; die Geräteantwort erklärte `fsp` als
  read-only und verwies auf `psm`. `psm` wird in den öffentlichen Keytabellen
  nicht geführt, ist aber live lesbar (Typ `uint8`, vorher `0`). Ein
  JSON-Boolean `psm=true` wurde abgelehnt; `psm=1` wurde angenommen und
  änderte `fsp` auf `true`. `psm=0` wurde ebenfalls angenommen und setzte
  `fsp=false` zurück. Beide Änderungen wurden bei `frc=1`, `alw=false` und
  ohne Ladeleistung ausgeführt. Zum Schluss wurde `psm=1` als
  Einphasenpräferenz des aktuellen Fahrzeugs gesetzt; Endstatus:
  `psm=1`, `fsp=true`, `amp=6`, `frc=1`, `alw=false`, `acu=null`,
  `car=4`, `err=0`, `nrg` ohne Ladeleistung.
- Die live gelesenen Go-e-Phasenparameter sind `mptwt=600000 ms` (minimale
  Zeit zwischen Phasenumschaltungen: 10 min), `mpwst=120000 ms` (minimale
  Zeit, die eine Phasenpräferenz anliegen muss: 2 min), `psh=500 W` und
  `spl3=4200 W`. Die 10-minütige Mindestpause und eine längere ESP-seitige
  Stabilitätszeit sollen beim Phasenmanager berücksichtigt werden; für die
  Stromvorgabe bleibt der separate, schnellere Regelpfad vorgesehen.
- Die ESP32-Firmware erweitert den gefilterten Status um `psm`, `fsp`,
  `pnp`, `pwm`, `lfspt`, `rbt`, `mptwt` und `mpwst`; JSON und Weboberfläche
  zeigen Präferenz, Force-Single-Phase-Status und Phasenwunsch. Der interne
  Schreiber akzeptiert nur `psm=0/1/2` und validiert die Charger-Bestätigung.
  Die konservative PV-Phasenpolicy wechselt von `Force_1` zu `Force_3` erst ab
  5.200 W berechnetem Überschuss und von `Force_3` zurück zu `Force_1` unter
  5.000 W.
  Verwendet wird der saldierte DS100-Export zuzüglich der tatsächlich im go-e-
  `nrg`-Array gemeldeten aktiven Ladeleistung, damit der aktuelle Ladevorgang
  den verfügbaren Überschuss nicht künstlich versteckt. Zielzustände müssen
  mindestens 10 min anliegen; zusätzlich werden `mptwt` und `mpwst`
  respektiert. Die Policy ist im Quellcode momentan auf PV-Adaptiv eingestellt;
  die Zuordnung anderer Lademodi auf `Auto=0` ist vorgesehen, aber noch nicht
  über eine Moduswahl aktiviert.
  Es gibt keinen Web-Schreibendpunkt. Der Moduswahl-/Ladestartpfad ist noch
  nicht implementiert. Die aktualisierte Phasenpolicy wurde erfolgreich
  gebaut und geflasht. Im Live-`/All`-Read danach: TLX `On Grid`; DS100 gültig
  mit `power_total=-299 W` (Einspeisung); berechneter Überschuss `299 W`;
  Charger `psm=1` (`Force_1`), `fsp=true`, Ziel `Force_1`,
  `phase_target_pending=false`, `frc=1`, `alw=false`, `car=4`, Ladeleistung
  null. Damit blieb die Policy erwartungsgemäß im sicheren Einphasenmodus und
  es war keine Phasenumschaltung nötig. Direkt nach dem Flash waren DS100-
  Werte kurz ungültig; beim Folgeread nach etwa 12 s waren DS100 und TLX
  wieder gültig.

## Nächster Schritt

Die Messquellen und ihr Verhalten bei Bezug/Einspeisung sind ausreichend
verifiziert, um die nächste Story anzugehen. Die konservative Phasenpolicy
(`Force_1`/`Force_3`) ist gebaut, geflasht und live geprüft; sie blieb bei
rund 300 W Einspeisung erwartungsgemäß auf `Force_1`. Die manuell begrenzten
go-e-Schreibtests für `amp`, `frc` und `psm` sind erfolgt.

**Als Nächstes: Story 3.1 – kontrolliert live beobachten.** Die konservative
Policy für Start, Nachregeln und Stop ist mit den vorläufigen Schutzwerten
geflasht und aktiv. Im bisherigen Live-Test lag der Überschuss noch unter der
Startschwelle; echtes Starten, Stromnachregeln und Stop bei ausreichender
PV-Leistung wurden daher noch nicht gemeinsam beobachtet. Bei natürlichem
Überschusslauf diese Zustände kontrollieren, ohne absichtlich einen großen
Lastsprung zu erzeugen. Danach folgt als nächste Entwicklungsstory die
Modussteuerung und ihre Webbedienung.
Maßgeblich ist der rohe DS100-Netzsaldo (`0x0420`): negativer Wert bedeutet
Einspeisung. Der für die Phasenwahl rekonstruierte Überschuss ist nicht die
Regelgröße für die Stromnachführung. Unbekannte, ungültige oder veraltete
Messwerte müssen die PV-Ladefreigabe verhindern bzw. das Laden sicher stoppen.
Die go-e-Mindestvorgabe von 6 A bedeutet, dass unterhalb der Mindestladeleistung
nicht stufenlos weitergeregelt werden kann. Daher erst nach stabil ausreichendem
Überschuss starten, Sollstrom vorsichtig und begrenzt anheben, bei Bezug
zügiger reduzieren und nötigenfalls stoppen. Phase bleibt separat und deutlich
langsamer geregelt. Der erste begrenzte Livebetrieb ist aktiv; die
Schutzparameter werden anhand realer Messwerte weiter beobachtet.

Regelungsvorgaben des Benutzers: Der mögliche Überschuss wird während des
Ladens aus dem DS100-Netzexport plus der go-e-Ladeleistung berechnet; so wird
auch bei sinkender PV-Leistung zunächst auf 6 A reduziert und ein begrenzter
Netzbezug auf der aktiven Phase in Kauf genommen. Ein 60-s-Gleitmittelwert
bildet die Regelgröße. Stromänderungen erfolgen höchstens einmal pro Minute,
in 1-A-Schritten, mit Sicherheitsabstand zugunsten der Einspeisung. Start erst
nach ausreichender Reserve über 5 min (10 min wäre unnötig träge, da
Phasenumschaltung bereits separat auf 10 min begrenzt ist). Stromabregelung
und nötiger Stop
reagieren auf denselben Netzsaldo; plötzliche Haushaltslasten wie Kochen werden
also wie ein Rückgang des PV-Überschusses behandelt. Zwischen Start-/Stop-
Aktionen gilt ein Mindestabstand von 5 min.

Ungültige DS100-Samples werden aus dem Mittelwert ausgelassen, aber als
Kommunikationsfehler gezählt; einzelne Ausfälle lösen keine Notabschaltung
aus. Steigende Fehlerzahl begrenzt zunächst die Stromvorgabe auf 6 A;
bei anhaltenden/zu häufigen Ausfällen und/oder veralteter Messung folgt der
Stop. Fehlerzähler und Gültigkeit sind in JSON/Web sichtbar. Der
Phasenwechsel bleibt bei 10 min Mindestintervall und nutzt weiter den
ermittelten verfügbaren Überschuss; die zunächst einphasige Fahrzeugreaktion
verhindert nicht den Test des go-e-Phasenbefehls.

Erster Implementierungsstand: 60-s-Mittel der gültigen
DS100-Netzleistungsproben; verfügbare Ladeleistung = negativer Mittelwert plus
go-e-Gesamtwirkleistung `nrg[11]`; pro Snapshot Gesamt- und Fehlerfolge-Zähler.
Aktive Startparameter sind vorläufig 500 W Sicherheitsreserve, 230 V je Phase,
Start-Haltezeit 5 min, Strombefehl mindestens 60 s auseinander, Start-/Stop-
Mindestabstand und Unter-6-A-Haltezeit jeweils 5 min. Drei aufeinanderfolgende
Messfehler setzen den Zielstrom auf 6 A (Abregelung weiterhin in 1-A-Schritten
pro Minute), sechs führen zum Stop; einzelne Fehler werden ausgelassen und
halten neue Regelbefehle kurz an. Bei 15 s alter Messung wird geregelt
pausiert, bei 30 s alter Messung sicher gestoppt. Diese Grenzwerte sind
Werte der ersten Aktivierung; sie müssen im Betrieb weiter beobachtet werden.
Die Web-JSON zeigt Mittelwert, verfügbare Ladeleistung, Startschwelle,
Fehlerzähler, Zielstrom und Aktion. Live geprüft nach Aktivierungs-Flash: TLX
`On Grid`, DS100 gültig mit `-455 W` Momentanwert und `-425 W` 60-s-Mittel
(Einspeisung), kein Messfehler, Zielstrom 6 A, Startschwelle 1.880 W, Status
`WAITING_FOR_START_RESERVE`; Charger bleibt `frc=1`, `alw=false` und bei
0 W. Beim späteren 60-s-Read lagen Netzleistung bei `-426 W` und Mittelwert
bei `-440 W`; das Verhalten blieb unverändert und die Regelung startete nicht.

Der Taster bleibt eine spätere Bedienungsstory: `lbp` wurde unter Firmware
57.1 mit deaktivierten App-Tastenfunktionen (`sdp=0`, `bac=0`) erkannt, aber
wiederholte Ereignisse müssen noch bestätigt werden. Es gibt keinen
allgemeinen Web-Schreibendpunkt.


## Backlog

### Epic 1 – Messwerte verlässlich erfassen

**Story 1.1 – DS100-Richtung und Messwerte verifizieren**

- Vorzeichen für Bezug und Einspeisung klären: für `0x0420` durch
  Displayvergleich bestätigt.
- Gesamtwirkleistung `0x0420` als saldiertes Regelungssignal verwenden;
  Phasenwerte einzeln für Anzeige und optionale Historie erfassen.
- Phasenspannungen, Phasenströme und Phasenwirkleistungen sind plausibilisiert;
  Gesamtstrom `0x0418` und Energiezähler bleiben offen.
- Akzeptanz für Netzleistung erfüllt: Bezug und Rückspeisung sind
  unterscheidbar, Vorzeichen und Messwert plausibel.

**Story 1.2 – TLX-Datenquelle verifizieren**

- TLX ist separat ausgelesen; `/All` und typisierte Einzelendpunkte liefern
  wiederholt PV- und Netzwerte samt Einheiten.
- PV-Eingangsleistung aus Spannung × Strom und TLX-Netzleistung gegen
  Wechselrichterdisplay bzw. Dokumentation bestätigen.
- Akzeptanz: benötigte Werte sind wiederholbar abrufbar und ihre Einheit
  sowie Bedeutung/Vorzeichen sind bekannt.

**Story 1.3 – RS485-Geräte koordiniert abfragen**

- Gemeinsamer A/B-Bus ist bestätigt; DS100 (Modbus, 9.600 Baud) und TLX
  (19.200 Baud) nacheinander mit jeweils passender Konfiguration abfragen.
- Die erste kombinierte Abfrage läuft; bei einem Live-Snapshot waren beide
  Quellen gültig und die DS100-Phasensumme passte zur saldierten Leistung.
- Einzelne Modbus-Anfragen können mit Timeout ausfallen; Retry und
  `valid:false`/`null` verhindern, dass ungültige Messwerte als gültig
  erscheinen. Längere Stabilitätsbeobachtung bleibt offen.
- Akzeptanz: über längere Laufzeit aktuelle Werte beider Quellen sowie
  sichtbare Kommunikations- und Timeoutfehler.

**Story 1.4 – Gemeinsames Messdatenmodell bereitstellen**

- DS100-Netzleistung und TLX-PV-Leistung in einem gemeinsamen Modell führen.
- Zahlenwerte statt formatierter Strings für Regelung verwenden.
- Akzeptanz: Wert, Einheit, Zeitstempel und Gültigkeit sind für jeden
  Messkanal verfügbar.

### Epic 2 – go-e lokal anbinden

**Story 2.1 – API-Status lesen**

- WLAN-/HTTP-Verbindung und gefilterte Statusabfrage implementiert und live
  bestätigt.
- API-Fehler, Charger-Nichterreichbarkeit und ungültige Antworten sind über
  Gültigkeitsstatus und Fehlertext sichtbar.
- Fahrzeugstatuscodes und optionale Nullwerte verarbeitet; `Complete` mit
  `acu=null` live bestätigt. Weitere Statuscodes sind dokumentiert, aber
  nicht alle am Fahrzeug beobachtet.
- Poll-Zielintervall aktuell 15 s. Zuvor waren es 5 s; die Statuswerte werden
  für Rückmeldung/Anzeige und `nrg[11]` als Ladeleistungsanteil der
  Überschussrechnung benötigt, nicht aber in 5-s-Taktung.

**Story 2.2 – Unterstützte Steuerfunktionen prüfen**

- Modell und Firmware festgestellt: go-eCharger, Firmware 57.1.
- In der offiziellen API-Doku bestätigt: `amp` (R/W) ist der
  angeforderte Strom, `frc` (R/W) ist Neutral/Off/On; `acu` und `alw` sind
  Statuswerte. Für Einphasenbetrieb sind außerdem `fsp`, `psh`, `pnp` und
  weitere Phasenlogik-Status/Parameter dokumentiert. Die Wirkung von `amp`,
  `frc` und `psm` wurde am Charger mit Firmware 57.1 kontrolliert geprüft.
- Tasterkonzept vorbereiten: `lbp` wird von Firmware 57.1 geliefert und ist
  laut englischer aktueller API-Key-Tabelle der letzte Tastendruck in
  Millisekunden. Mit den vom Benutzer in der App deaktivierten
  Start-/Force- und Stromänderungsfunktionen (`sdp=0`, `bac=0`) wurde ein
  Tastendruck erkannt, ohne dass `alw`, `frc`, `car` oder `amp` wechselten.
  App-Sperren beibehalten und die Erkennung bei mehreren Tastendrücken
  verifizieren; dann ereignisgesteuert auf `lbp` reagieren, keine häufigere
  Poll-Schleife.
- HTTP-Schreibsyntax und begrenzte Schaltwirkungen wurden gegen Firmware 57.1
  verifiziert: `amp=7`/`amp=6`, `frc=2` (Force On), `frc=1` (Force Off) und
  `frc=0` (Neutral). `amp`-Änderungen während
  des Ladens änderten `acu` innerhalb ca. 3 s; `frc=0` gab die Ladung frei,
  `frc=1` stoppte sie. Aktueller Endzustand ist Force-Off bei 6 A und ohne
  Ladeleistung; `car` kann dabei zeitverzögert `Charging` anzeigen. Als
  Phasenmodus ist `Auto=0`, `Force_1=1`, `Force_3=2`. Der Charger steht auf
  `Force_1`, passend zum derzeit einphasigen Fahrzeug. Die ESP32-Policy ist
  auf PV-Überschuss ausgelegt: bei verfügbarer Leistung ab 5.200 W für 10 min
  `Force_3`, unter 5.000 W für 10 min zurück auf `Force_1`; dazwischen bleibt
  die aktuelle Phase. Für die Schaltentscheidung wird DS100-Export plus
  go-e-Gesamtwirkleistung aus `nrg[11]` verwendet. Andere Lademodi sollen auf
  `Auto` zielen, sind aber noch nicht über die Moduswahl aktiviert.
  Die erste PV-Strom-/Freigaberegelung (Story 3.1) ist jetzt freigegeben und
  aktiv; als Nächstes längere Livebeobachtung, danach Moduswahl und die übrigen
  Modi. Phase bleibt mit 10-minütiger Mindestpause deutlich träger als die
  Stromregelung. Keine frei parametrierbare HTTP-Schreibroute bereitstellen.
- Akzeptanz: verwendete API-Parameter sind für das Gerät dokumentiert und
  kontrolliert getestet.

### Epic 3 – Lademodi sicher umsetzen

**Story 3.1 – Rückspeiseadaptives Laden**

- DS100-Leistung am Netzanschlusspunkt als maßgebliches Signal verwenden.
- Regelgröße ist der 60-s-Gleitmittelwert des DS100-Saldos, nicht der
  phasenweise rekonstruierte Phasenüberschuss. Beim Laden wird für die
  Stromvorgabe der go-e-Ladeleistung entsprechend zum Netzexport addiert;
  Bezug und plötzliche Haushaltslasten werden unmittelbar im gleichen Signal
  sichtbar.
- Start nur nach ausreichendem Export, der mindestens 6 A auf der aktiven
  Phase plus Sicherheitsabstand deckt; aktive erste Haltezeit 5 min.
  Start-/Stop-Befehle mindestens 5 min auseinanderhalten, damit Schütze nicht
  takten. Aktive Startreserve 500 W; Stop bei längerem Unterschreiten der
  6-A-Mindestleistung für 5 min.
- Sollstrom höchstens einmal pro Minute in 1-A-Schritten verändern; Erhöhungen
  zurückhaltend, Reduktion bei Bezug schneller. Unterhalb des Überschusses für
  die Mindestvorgabe zunächst auf 6 A abregeln; anhaltender Mangel führt nach
  der Haltezeit zum Stop. Ein begrenzter Bezug bei fallender Einspeisung bis
  zum Abregeln/Stop ist akzeptiert.
- Ungültige Samples nicht in den Mittelwert aufnehmen, aber Fehler zählen.
  Einzelne Ausfälle halten die Regelung nicht sofort an; steigende Fehlerzahl
  begrenzt auf 6 A, anhaltende Fehler oder zu alte Messwerte stoppen sicher.
  Fehlerzähler und Messgültigkeit sind sichtbar; aktive Schwellen stehen im
  Abschnitt „Nächster Schritt“.
- Phasenpolicy unabhängig bei 10 min Mindestintervall lassen und den
  Phasenbefehl auch bei einphasigem Fahrzeug testen.
- Akzeptanz: bei fehlendem/ungültigem Messwert kein Überschussladen; Netzbezug
  wird durch konservative Schwellen möglichst vermieden. Simulation bzw.
  kontrollierte Testsequenz zeigt stabile Sollstromänderungen und Stop ohne
  unsteten Befehlstakt.

**Story 3.2 – Betriebsarten zentral verwalten**

- Einen zentralen, typisierten Betriebsmodus mit `PV-Adaptiv` (Standard),
  `Aus`, `Vollgas` und `Einstellbar` einführen. Alle Steuerwege rufen dieselbe
  Moduswechsel-/Übergangsfunktion auf; die PV-Regelung darf nicht außerhalb
  des PV-Modus schreiben.
- Beim ESP-Neustart sicher in den festgelegten Standardmodus `PV-Adaptiv`
  wechseln; manuelle Vollgas-/Einstellmodi dürfen nicht unbemerkt fortgesetzt
  werden.
- `Aus`: Laden über `frc=1` sperren. `PV-Adaptiv`: konservative PV-Regelung
  aktivieren. `Vollgas`: `Auto`-Phasenwahl und freigegebenen Maximalstrom
  verwenden. `Einstellbar`: nur validierte Strom-/Phaseneinstellungen
  anwenden. Fehler und fehlgeschlagene Übergänge sichtbar melden.
- Akzeptanz: Modusstatus und Chargerstatus bleiben konsistent; parallele
  Automatikpfade können keine Modusvorgabe überschreiben.

**Story 3.3 – Weboberfläche für Betriebsarten**

- Aktuellen Modus, Chargerzustand, Regelziel, Messgültigkeit und Fehler klar
  anzeigen; Bedienung für `Aus`, `PV-Adaptiv`, `Vollgas` und
  `Einstellbar` bereitstellen.
- Nur begrenzte Modus-/Einstellungsbefehle akzeptieren, keinen generischen
  Charger-API-Proxy oder frei parametrierbaren Schreibendpunkt hinzufügen.
- Akzeptanz: UI zeigt bestätigten Zustand und meldet abgelehnte/
  fehlgeschlagene Übergänge, statt Erfolg vorzutäuschen.

**Story 3.4 – Vollgasmodus**

- Manuelles Laden unabhängig vom PV-Überschuss bis zur vom Charger/Fahrzeug
  erlaubten Grenze ermöglichen; Phasenmodus `Auto`, Maximalstrom innerhalb
  bestätigter go-e-Grenzen.
- Über Web auf `Aus` oder `PV-Adaptiv` beendbar.
- Akzeptanz: Modus ist sichtbar und manuell beendbar.

**Story 3.5 – Einstellbarer Lademodus**

- Manuelles Stromlimit und – falls unterstützt – Phasenwahl ermöglichen.
- Vorgesehener Bereich laut Anforderung: einphasig 6 A bis dreiphasig 16 A;
  tatsächliche Grenzen mit Charger und Fahrzeug abgleichen. Nur feste,
  validierte Wertebereiche annehmen.

**Story 3.6 – Rückkehr zum PV-Modus bei neuem PV-Tag**

- Ohne Uhr eine dunkle Phase und anschließenden PV-Neubeginn anhand
  stabiler PV-Leistung erkennen.
- Nach manuellem Vollgas-/Einstellmodus bei erkannter Nacht (PV stabil offline
  und danach stabil wieder online) automatisch auf `PV-Adaptiv` zurückschalten.
- Zusätzlich sofort auf `PV-Adaptiv` zurückschalten, wenn das Fahrzeug als
  ausgesteckt erkannt wird; dies gilt auch ohne vorherige Nachterkennung.
  `PV-Adaptiv` bleibt damit immer der sichere Standardmodus nach einem
  abgeschlossenen manuellen Ladevorgang.
- Akzeptanz: kurze Wolken oder Leistungsschwankungen lösen keinen falschen
  Tageswechsel aus; Ausstecken wird zuverlässig erkannt; Verhalten nach
  ESP32-Neustart ist definiert.

### Epic 4 – Tasterbedienung und optionale Historie

- **Story 4.1 – go-e-Taster startet Vollgasmodus:** erkannte `lbp`-Änderung
  ruft denselben zentralen Moduswechsel wie die Weboberfläche auf und setzt
  `Vollgas`. App-Sperren `sdp=0` und `bac=0` bleiben aktiv. Mehrere
  Tastendrücke und die Entprellung/Neustartbehandlung vor der Integration
  bestätigen. Web bleibt der Weg, `Aus` bzw. `PV-Adaptiv` wieder auszuwählen.
- Fehlerzustände und veraltete Messwerte auch in der Webansicht anzeigen; bei
  ungültigen Werten sicher reagieren.
- Datenhistorie für Wohnungsverbrauch, PV-Leistung und weitere Kanäle erst
  nach stabiler Erfassung als optionalen zweiten Schritt bewerten.

## Reihenfolge

1. NVS-Blockade separat untersuchen, bevor persistente Flash-Zugriffe
   wieder eingeführt werden. Bis dahin bleibt das Startup-Recovery-Gate aktiv:
   PV-/Phasenregelung sind nach Neustart gesperrt, bis ein frischer go-e-Status
   Force-Off bestätigt.
2. Story 3.2 – zentrale Modusverwaltung und sichere Übergänge.
3. Story 3.3 – Webstatus und begrenzte Bedienung aller vier Modi.
4. Story 3.4/3.5 – Vollgas und einstellbaren Modus Schritt für Schritt
   validieren.
5. Story 3.6 – Rückkehr zum PV-Modus bei Nacht-/PV-Neustart oder erkanntem
   Ausstecken.
6. Story 4.1 – Taster setzt Vollgas über denselben Moduswechselpfad.
7. Optionale Datenspeicherung zuletzt bewerten.

## Arbeitsregeln für die nächsten Tests

- Einen bekannten funktionierenden Stand behalten; Baudrate und
  Registerbelegung nicht gleichzeitig ändern.
- Messwerte einzeln gegen Display oder eine andere verlässliche Referenz
  prüfen.
- Vor jedem Upload beachten, dass die Firmware auf dem ESP32 ersetzt wird.
- Keine automatische Laderegelung aktivieren, bevor die Policy mit gültigen,
  ungültigen und veralteten Messwerten geprüft wurde und die Begrenzungen für
  Start, Stromänderung und Stop feststehen.
