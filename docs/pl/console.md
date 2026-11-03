# Konsola

[Dokumentacja](index.md) › Konsola

Strona **Console** (konsola, <kbd>Ctrl</kbd>+<kbd>1</kbd>) pokazuje, co MCU aplikacji urządzenia CHESTER wysyła
przez J-Link (SEGGER RTT), w dwóch panelach:

- **Shell**: powłoka poleceń urządzenia. Polecenia wpisuje się pod nią, a ich odpowiedzi pojawiają się tutaj.
  Wysłane polecenia są wyświetlane jako `> command`.
- **Log**: komunikaty logu urządzenia z kolumną poziomu: **D** debug, **I** info, **W** warning (ostrzeżenie),
  **E** error (błąd) oraz **H** dla wierszy zrzutu szesnastkowego (hexdump).

MCU LTE nie ma konsoli; przy celu **lte** strona wyświetla **This target has no console**.

Wiersze między wierszami urządzenia oznaczają, co stało się z połączeniem: **— connected —**,
**— disconnected —**, **— paused: … —** podczas wgrywania firmware, **— resumed —**.

## Układ

Przeciągnij uchwyt między panelami, aby zmienić ich rozmiar (lub ustaw na nim fokus i użyj klawiszy strzałek).
Przy szerokości okna poniżej 1000 pikseli panele są ułożone jeden pod drugim; **More** (więcej) na pasku narzędzi
zawiera **Stacked layout** i **Swap panes**, a także **Show timestamps** i **Wrap lines**.
<kbd>Ctrl</kbd>+<kbd>=</kbd>, <kbd>Ctrl</kbd>+<kbd>-</kbd> i <kbd>Ctrl</kbd>+<kbd>0</kbd> zmieniają rozmiar
czcionki.

Widok podąża za nowymi wierszami w miarę ich napływania. Po przewinięciu w górę pozostaje w miejscu, w którym
jesteś; przycisk **▼ N new lines** przenosi z powrotem, podobnie jak <kbd>End</kbd> lub **Follow**. **Pause**
(<kbd>Ctrl</kbd>+<kbd>P</kbd>) zamraża widok, podczas gdy wiersze nadal napływają; **Resume** je pokazuje. Każdy
panel przechowuje ostatnie 100 000 wierszy.

## Pasek narzędzi

| Przycisk | Działanie |
|---|---|
| **Reset** | Resetuje urządzenie, a z **Reset and halt** resetuje je i utrzymuje zatrzymany CPU |
| **Reconnect** | Ponownie łączy się z konsolą (<kbd>F4</kbd>) |
| **Batch…** | Uruchamia [plik wsadowy](#pliki-wsadowe) (<kbd>Ctrl</kbd>+<kbd>B</kbd>); ⓘ obok otwiera dokumentację |
| **Clear** | Czyści **Shell**, **Log** lub **Both** (oba) (<kbd>Ctrl</kbd>+<kbd>K</kbd> czyści panel z fokusem) |
| **Save view…** | Zapisuje panel z fokusem do pliku tekstowego (<kbd>Ctrl</kbd>+<kbd>S</kbd>) |
| **Follow**, **Pause** | Zobacz wyżej |
| **More** | **Open log file**, **Open log folder** ([pliki konsoli](#pliki-konsoli)) i opcje układu |

## Wysyłanie poleceń

Kliknij pole wprowadzania pod powłoką (lub naciśnij <kbd>Ctrl</kbd>+<kbd>E</kbd>), wpisz polecenie i naciśnij
<kbd>Enter</kbd>. Gdy pole jest nieaktywne, jego tekst zastępczy podaje przyczynę (**Not connected**,
**Connecting…** lub trwająca operacja, na przykład wgrywanie firmware).

- <kbd>↑</kbd> i <kbd>↓</kbd> przechodzą przez historię poleceń.
- <kbd>Ctrl</kbd>+<kbd>R</kbd> przeszukuje historię: wpisz część polecenia, <kbd>Enter</kbd> wstawia je do pola
  wprowadzania.
- Podczas pisania lista podpowiada własne polecenia urządzenia (z ich tekstem pomocy) i pasujące wpisy historii.
  <kbd>↑</kbd>/<kbd>↓</kbd> wybierają, <kbd>Tab</kbd> lub <kbd>→</kbd> akceptuje, <kbd>Esc</kbd> zamyka listę.
- Szary tekst za kursorem uzupełnia wiersz; <kbd>→</kbd> lub <kbd>End</kbd> go akceptuje.
- <kbd>Tab</kbd> uzupełnia polecenie. Nigdy nie opuszcza pola wprowadzania; aby przejść dalej, użyj
  <kbd>Shift</kbd>+<kbd>Tab</kbd>.

Polecenia urządzenia są odczytywane za pomocą `help` przy połączeniu konsoli. Można to wyłączyć w
**Settings → Console → Read the command list from the device on connect (sends “help”)**, a podpowiedzi opcją
**Suggest commands while typing**.

### Historia poleceń

Historia przechowuje ostatnie 1000 poleceń dla każdego typu urządzenia i celu (na przykład CHESTER app). Jest
wspólna dla wszystkich kart i wiersza poleceń (`hwio chester app console`). Polecenia zawierające sekrety (takie jak
`lte config password`, `lrw config appkey` czy `ble config passkey`) nigdy nie są zapisywane, podobnie jak polecenie
rozpoczęte spacją. **Settings → Console** wyłącza zapisywanie (**Remember commands**) i usuwa historię
(**Clear history**).

## Wyszukiwanie i filtrowanie

Pasek pod logiem ma dwa tryby:

- **Search** (wyszukiwanie, <kbd>Ctrl</kbd>+<kbd>F</kbd>): podświetla dopasowania i pokazuje **3 / 17**.
  <kbd>Enter</kbd> lub <kbd>F3</kbd> przechodzi do następnego dopasowania, <kbd>Shift</kbd>+<kbd>Enter</kbd> lub
  <kbd>Shift</kbd>+<kbd>F3</kbd> do poprzedniego; w logu działają też <kbd>N</kbd> i <kbd>Shift</kbd>+<kbd>N</kbd>.
- **Filter** (filtr, <kbd>Ctrl</kbd>+<kbd>L</kbd>): pokazuje tylko pasujące wiersze (**Showing 40 of 2 000 lines**).

**Aa** uwzględnia wielkość liter, **.\*** traktuje tekst jako wyrażenie regularne. Przyciski poziomów **D**, **I**,
**W**, **E** i **H** pokazują lub ukrywają poziomy; wiersze zrzutu szesnastkowego (**H**) są ukryte, dopóki ich nie
włączysz. <kbd>Esc</kbd> zamyka wyszukiwanie, drugie <kbd>Esc</kbd> czyści filtr.

Powłoka ma własne wyszukiwanie: naciśnij <kbd>Ctrl</kbd>+<kbd>F</kbd>, gdy fokus jest na powłoce.

## Zaznaczanie i kopiowanie

Kliknij wiersz, aby go zaznaczyć, <kbd>Shift</kbd>+kliknięcie lub przeciągnięcie zaznacza więcej,
<kbd>Ctrl</kbd>+<kbd>A</kbd> zaznacza wszystko. <kbd>Ctrl</kbd>+<kbd>C</kbd> lub menu kontekstowe kopiuje wiersze
(„Copied to clipboard.”); jednorazowo kopiowanych jest najwyżej 100 000 wierszy.

## Zapisywanie widoku

**Save view…** (<kbd>Ctrl</kbd>+<kbd>S</kbd>) zapisuje panel z fokusem tak, jak jest wyświetlany, ze znacznikami
czasu, jeśli są włączone. Nazwa jest wstępnie wypełniona, na przykład
`chester-app-clime-v3.2.0-20261103-101500-log.txt`, a folderem jest ostatnio używany (za pierwszym razem Dokumenty).

## Pliki wsadowe

Plik wsadowy to plik tekstowy UTF-8 z jednym poleceniem powłoki w każdym wierszu. **Batch…**
(<kbd>Ctrl</kbd>+<kbd>B</kbd>) wysyła wiersze jeden po drugim, jeden co 200 ms. Puste wiersze i wiersze zaczynające
się od `#` są pomijane.

```text
# what the device is and how it is configured
info show
app config show
```

Cały plik jest sprawdzany przed wysłaniem pierwszego wiersza: najwyżej 10 000 poleceń, 1 MiB na plik i 4096 bajtów
na wiersz. Podczas działania pasek narzędzi pokazuje **Batch 3 / 4** z przyciskiem zatrzymania. Plik wsadowy
zatrzymuje się, gdy urządzenie się rozłączy lub rozpocznie się inna operacja (na przykład wgrywanie firmware).

## Pliki konsoli

Jeśli tego nie wyłączysz, każde połączenie jest zapisywane do pliku konsoli: powłoka, log, wysłane polecenia i
zdarzenia połączenia, ze znacznikami czasu. Pliki znajdują się w folderze **Log folder** pokazanym w
**Settings → Console**:

| System | Folder |
|---|---|
| Windows | `%LOCALAPPDATA%\HARDWARIO\Monitor\console` |
| macOS | `~/Library/Application Support/HARDWARIO/Monitor/console` |
| Linux | `~/.local/share/HARDWARIO/Monitor/console` |

Nazwa pliku zawiera urządzenie, firmware, czas (UTC) i sondę, na przykład
`chester-app-clime-v3.2.0-20261103-101500-<serial>.log`. Plik większy niż 10 MiB jest kontynuowany w
`….part2.log`. Folder przechowuje 50 najnowszych plików, łącznie najwyżej 200 MB. Sekrety są zapisywane jako
`<redacted>` (te same polecenia co w historii oraz claim token i passkey BLE z `info show`). W systemach Linux i
macOS pliki może odczytać tylko Twój użytkownik.

Pliki konsoli można wyłączyć w **Settings → Console → Save console to log files** albo dla danej sondy w **⋯**
(**Advanced**).

## Ponowne łączenie

Gdy łącze zostanie zerwane (urządzenie zostało zresetowane, straciło zasilanie lub wyciągnięto kabel), konsola
łączy się ponownie samoczynnie, a pasek urządzenia pokazuje **Reconnecting…** z przyczyną. Można to wyłączyć opcją
**Auto-reconnect** w **⋯** (**Advanced**) lub w **Settings → J-Link**; wtedy <kbd>F4</kbd> lub **Reconnect** łączy
ponownie.

Jeśli urządzenie wysyła dane szybciej, niż aplikacja może je wyświetlić, baner informuje, ile wierszy pominięto.

## Wiersz poleceń

Ta sama konsola w terminalu:

```sh
hwio chester app console                   # interactive: F4 reconnects, Ctrl+C on an empty line or Ctrl+D quits
hwio chester app command "info show"       # one command, print the answer
echo "app config show" | hwio chester app console
```

Zobacz [Wiersz poleceń](../en/cli.md#console).
