# Prywatność i przechowywane dane

[Dokumentacja](index.md) › Prywatność

HARDWARIO Monitor nie ma telemetrii ani konta. Łączy się z internetem tylko w sprawach wymienionych poniżej, a
wszystko, co przechowuje, pozostaje na Twoim komputerze.

## Połączenia sieciowe

| Co | Kiedy | Dokąd |
|---|---|---|
| [Sprawdzanie aktualizacji](#sprawdzanie-aktualizacji) | aplikacja desktopowa, najwyżej raz dziennie (można wyłączyć); `hwio update check` | `api.github.com` |
| Firmware z katalogu | gdy wgrywasz firmware według identyfikatora lub uruchamiasz `hwio chester app catalog-info` | `firmware.hardwario.com` |
| Firmware LTE od HARDWARIO | gdy wgrywasz firmware LTE bez własnego pliku | `docs.hardwario.com` |
| Odnośniki do dokumentacji | gdy klikniesz któryś z nich | Twoja przeglądarka |

Serwer MCP (`hwio mcp --http`) nasłuchuje tylko na tym komputerze (127.0.0.1) i nie jest dostępny z sieci.
SEGGER J-Link Software i nRF Util to osobne programy, które działają według własnych zasad.

## Sprawdzanie aktualizacji

Aplikacja desktopowa pobiera z GitHub listę wydań HARDWARIO Monitor 10 sekund po uruchomieniu, najwyżej raz na
24 godziny. Żądanie nie zawiera żadnych danych o Tobie ani o Twoich urządzeniach, jedynie wersję aplikacji w
nagłówku `User-Agent`. Gdy dostępna jest nowsza wersja, informuje o tym baner (**HARDWARIO Monitor 2.1.0 is
available (you have 2.0.0).**) z przyciskami **Download**, który otwiera stronę wydania, i **Skip this version**.
Aplikacja nigdy sama niczego nie pobiera ani nie instaluje.

**Settings → About → Updates**:

- **Check for updates automatically** (włączone): wyłącza codzienne sprawdzanie.
- **Include pre-releases (beta, release candidates)** (wyłączone).
- **Check now**.

Wiersz poleceń sprawdza aktualizacje tylko na żądanie: `hwio update check` (`--prerelease` uwzględnia wersje
przedpremierowe).

## Raport diagnostyczny

Gdy potrzebujesz pomocy wsparcia technicznego HARDWARIO, utwórz raport diagnostyczny: **Help → Create diagnostic
report…** w aplikacji lub `hwio diag report` (`-o FILE`). Jest to plik zip zawierający:

- podsumowanie: wersję aplikacji, system operacyjny, znalezione J-Link Software i nRF Util, podłączone sondy
  J-Link, otwarte karty i wyniki testów `hwio doctor`;
- ostatnie logi aplikacji (aplikacji desktopowej i `hwio`);
- plik ustawień;
- najnowsze raporty o awariach, jeśli istnieją.

Przed zapisaniem nazwa użytkownika, folder domowy i nazwa komputera są zastępowane (`<user>`, `~`, `<host>`), a
wartości wyglądające na hasła, tokeny lub klucze są zastępowane przez `<redacted>`. Numery seryjne sond pozostają w
raporcie. Logi mogą zawierać dane wyjściowe Twojego urządzenia, więc przejrzyj raport przed wysłaniem. Pliki konsoli
i historia poleceń nie są częścią raportu.

Raport jest zapisywany na Twoim komputerze (domyślnie `hwio-diagnostics-YYYYMMDD-HHMMSS.zip` w folderze Dokumenty
lub w bieżącym folderze w przypadku `hwio diag report`); nigdy nie jest przesyłany. Dołącz go samodzielnie do
zgłoszenia do wsparcia technicznego.

## Raporty o awariach

Jeśli aplikacja lub `hwio` ulegnie awarii, raport o awarii jest zapisywany w folderze `crashes` danych aplikacji
(przechowywanych jest najwyżej 5). Nic nie jest wysyłane. Przy następnym uruchomieniu aplikacja wyświetla
**HARDWARIO Monitor quit unexpectedly last time.** i proponuje **Create diagnostic report**, który zawiera raport o
awarii. `HWIO_NO_CRASH_HANDLER=1` wyłącza raporty o awariach.

## Co jest przechowywane i gdzie

| Dane | Windows | macOS | Linux |
|---|---|---|---|
| Ustawienia (`Monitor.ini`) | `%APPDATA%\HARDWARIO\Monitor.ini` | `~/.config/HARDWARIO/Monitor.ini` | `~/.config/HARDWARIO/Monitor.ini` |
| Dane aplikacji | `%LOCALAPPDATA%\HARDWARIO\Monitor` | `~/Library/Application Support/HARDWARIO/Monitor` | `~/.local/share/HARDWARIO/Monitor` |

Folder danych aplikacji zawiera:

| Folder | Zawartość | Limit |
|---|---|---|
| `console` | [pliki konsoli](console.md#pliki-konsoli) i historia poleceń (`history-<device>-<target>.txt`) | 50 plików i 200 MB; 1000 poleceń na historię |
| `logs` | logi aplikacji, jeden plik na uruchomienie | 10 plików aplikacji desktopowej, 20 dla `hwio`, 50 MiB każdego rodzaju |
| `firmware-cache` | pobrane firmware z katalogu i firmware LTE | 100 plików, 500 MiB |
| `crashes` | raporty o awariach | 5 plików |

Plik ustawień, logi i pliki konsoli są wspólne dla aplikacji desktopowej i `hwio`. Pliki konsoli i historię może
odczytać tylko Twój użytkownik (Linux, macOS). Polecenia zawierające sekrety nie są zapisywane w historii, a w
plikach konsoli są zapisywane jako `<redacted>` ([Konsola](console.md#historia-poleceń)).

Odinstalowanie HARDWARIO Monitor nie usuwa tych folderów; jeśli chcesz, usuń je samodzielnie. Aby usunąć tylko
historię poleceń: **Settings → Console → Clear history** lub `hwio console history clear`.
