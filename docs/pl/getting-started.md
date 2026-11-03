# Pierwsze kroki

[Dokumentacja](index.md) › Pierwsze kroki

HARDWARIO Monitor wyświetla konsolę i log urządzeń HARDWARIO CHESTER i wgrywa ich firmware przez sondę SEGGER
J-Link. Bluetooth LE i serwer MCP dla asystentów AI są dostępne w wierszu poleceń (`hwio`, zobacz
[Bluetooth LE](bluetooth.md) i [Serwer MCP](../en/mcp.md)).

Przed rozpoczęciem zainstaluj aplikację i SEGGER J-Link Software ([Instalacja](install.md)).

## Urządzenie

CHESTER ma dwa MCU, nazywane **celami** (targets):

| Cel | MCU | Co można zrobić |
|---|---|---|
| `app` | MCU aplikacji (nRF52840) | konsola i log (RTT), wgrywanie firmware, kasowanie, reset, recover |
| `lte` | MCU LTE (nRF9160 lub nRF9151) | wgrywanie firmware LTE i firmware modemu, kasowanie, reset, recover; nie ma konsoli |

Podłącz J-Link do komputera przez USB i do złącza programowania CHESTER, a CHESTER zasil z baterii lub przez jego
wejście zasilania USB-C.

## Połączenie

1. Uruchom **HARDWARIO Monitor**. Pierwsza karta, **New device** (nowe urządzenie), wyświetla **Choose a device to
   connect** (wybierz urządzenie do połączenia) z sondami J-Link podłączonymi do tego komputera. Lista odświeża się
   co dwie sekundy.
2. Wybierz sondę. Sonda otwarta już w innej karcie jest oznaczona **In use (tab N)**; jej wybranie przełącza na tę
   kartę. Sonda zajęta przez inny program jest oznaczona **In use by another program** i nie można jej wybrać.
3. W sekcji **Device** pozostaw **CHESTER** i wybierz cel: **app** lub **lte**.
4. Naciśnij **Connect** (połącz).

Pasek urządzenia u góry karty pokazuje sondę, cel i stan: **Connecting…**, **Connected · 3.30 V** (z napięciem
celu), **Reconnecting…** lub **Disconnected — reason** (z przyczyną). **Connect** i **Disconnect**
(<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>D</kbd>) znajdują się po prawej stronie, a <kbd>F4</kbd> łączy ponownie.

Jeśli nie znaleziono żadnej sondy, strona wyświetla **No J-Link probe found** i informację, czy znaleziono J-Link
Software. Sprawdź kabel USB, a w systemie Linux regułę udev ([Instalacja](install.md#dostęp-usb-do-j-link)).
`hwio doctor` sprawdza to wszystko.

### Opcje połączenia

**⋯** (**Advanced**, zaawansowane) na pasku urządzenia zawiera opcje tej sondy. HARDWARIO Monitor zapamiętuje je
dla każdego numeru seryjnego sondy. Obowiązują od następnego połączenia.

| Opcja | Domyślnie | Znaczenie |
|---|---|---|
| **Automatic speed** | włączona | J-Link sam wybiera prędkość. Wyłączona: **J-Link speed (kHz)**, od 5 do 50000 |
| **RTT latency (ms)** | 0 (wartość urządzenia, 50 ms dla CHESTER) | Jak często odczytywana jest konsola |
| **Auto-reconnect** | włączona | Samoczynne ponowne połączenie po zerwaniu łącza |
| **Reset on connect** | wyłączona | Reset urządzenia przy łączeniu |
| **Save console to log file** | włączona | Zapis [pliku konsoli](console.md#pliki-konsoli) |

Sondy bez własnych opcji używają ustawień domyślnych z **Settings → J-Link**.

## Karty

Każde urządzenie to jedna karta; można otworzyć do 8 kart. <kbd>Ctrl</kbd>+<kbd>T</kbd> lub **+** otwiera nową
kartę, <kbd>Ctrl</kbd>+<kbd>W</kbd> ją zamyka (podobnie kliknięcie karty środkowym przyciskiem myszy),
<kbd>Ctrl</kbd>+<kbd>Tab</kbd> i <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Tab</kbd> przełączają między kartami.
Jeśli w karcie trwa jakaś operacja, przed zamknięciem pojawi się pytanie.

Pasek boczny po lewej stronie przełącza strony karty: **Console** (konsola, <kbd>Ctrl</kbd>+<kbd>1</kbd>) i
**Flash** (wgrywanie, <kbd>Ctrl</kbd>+<kbd>2</kbd>). **Settings** (ustawienia, <kbd>Ctrl</kbd>+<kbd>,</kbd>)
otwiera się jako osobna karta.

Przy następnym uruchomieniu karty zostają przywrócone, rozłączone (**Settings → General → Reopen tabs at start**).

Jednocześnie działa tylko jedno okno aplikacji. Ponowne uruchomienie aplikacji przenosi otwarte okno na wierzch.

## Strony

- [Konsola](console.md): powłoka i log urządzenia, wyszukiwanie, filtrowanie, historia, pliki wsadowe, pliki konsoli.
- [Wgrywanie firmware](flashing.md): firmware aplikacji i LTE, kasowanie, recover, reset.

## Ustawienia

| Sekcja | Zawartość |
|---|---|
| **General** | **Language** (System, English, Čeština), **Theme** (System, Light, Dark), **Reopen tabs at start** |
| **Console** | rozmiar czcionki, znaczniki czasu, zawijanie wierszy, podpowiedzi poleceń, historia poleceń, pliki konsoli i ich folder |
| **J-Link** | biblioteka J-Link (**Automatic** lub **Custom path**) i domyślne opcje połączenia |
| **nRF Util** | program nRF Util (**Automatic** lub **Custom path**), potrzebny tylko do firmware modemu LTE |
| **About** | wersja (z przyciskiem **Copy** dla wsparcia technicznego), [sprawdzanie aktualizacji](privacy.md#sprawdzanie-aktualizacji), polecenie `hwio` tej instalacji, licencje, odnośniki |

Zmiany obowiązują natychmiast.

## Pomoc

**Help** (pomoc) na dole paska bocznego:

- **Keyboard shortcuts (F1)**: wszystkie skróty klawiaturowe aplikacji (także <kbd>Ctrl</kbd>+<kbd>/</kbd>).
- **Create diagnostic report…**: plik zip dla wsparcia technicznego HARDWARIO
  ([Prywatność](privacy.md#raport-diagnostyczny)).
- **Documentation ↗**: strona HARDWARIO Monitor w serwisie docs.hardwario.com.

Gdy coś się nie powiedzie, aplikacja pokazuje, co się stało, prawdopodobną przyczynę (**Probably:**) i co należy
zrobić (**What to do:**). **Details** pokazuje kod błędu; [Rozwiązywanie problemów](troubleshooting.md) zawiera
wszystkie kody.

## Wiersz poleceń

Wszystko, co aplikacja robi z sondą J-Link, jest dostępne także jako polecenie:

```sh
hwio jlink list                       # the connected probes
hwio chester app console              # the console of the application MCU
hwio chester app flash firmware.hex
hwio doctor                           # check the installation
```

Zobacz [Wiersz poleceń](../en/cli.md).
