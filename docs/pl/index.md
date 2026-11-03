# Dokumentacja HARDWARIO Monitor

HARDWARIO Monitor to narzędzie do obsługi konsoli, logów i wgrywania firmware w urządzeniach HARDWARIO CHESTER:
aplikacja desktopowa (`hwio-monitor`) i narzędzie wiersza poleceń (`hwio`) dla systemów Windows, macOS i Linux.
Komunikuje się z urządzeniem przez sondę SEGGER J-Link, a z wiersza poleceń także przez Bluetooth LE.

Inne języki: [Čeština](../cs/index.md) · [Deutsch](../de/index.md) · [Slovenčina](../sk/index.md) ·
[English](../en/index.md)

## Podręcznik użytkownika

1. [Instalacja](install.md): pakiety, SEGGER J-Link Software, nRF Util, dostęp USB w systemie Linux
2. [Pierwsze kroki](getting-started.md): podłączanie CHESTER, karty, ustawienia, pomoc
3. [Konsola](console.md): powłoka i log, wyszukiwanie i filtrowanie, historia, pliki wsadowe, pliki konsoli
4. [Wgrywanie firmware](flashing.md): firmware aplikacji i LTE, identyfikatory z katalogu, UICR, kasowanie, recover
5. [Bluetooth LE](bluetooth.md): konsola przez Bluetooth, parowanie
6. [Rozwiązywanie problemów](troubleshooting.md): typowe problemy i wszystkie kody błędów
7. [Prywatność i przechowywane dane](privacy.md): sprawdzanie aktualizacji, raport diagnostyczny, miejsca przechowywania danych

## Dokumentacja referencyjna (tylko po angielsku)

- [Wiersz poleceń](../en/cli.md): wszystkie polecenia `hwio`, wyjście JSON, kody wyjścia
- [Migracja z py-hardwario](../en/cli-migration.md): `hardwario` → `hwio`
- [Serwer MCP](../en/mcp.md): `hwio mcp` dla asystentów AI

Ta dokumentacja opisuje HARDWARIO Monitor 2.0.
