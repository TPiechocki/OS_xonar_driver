# OS_xonar_driver

Sterownik ustawia kartę w trybie tylko wyjścia, które jest 4-kanałowe (choć używam w podstawowej wersji tylko 2 kanałów przednich) o próbkowaniu 44100 kHz. Możliwe jest ustawienie głośności wyjść przednich, wyciszenie/odciszenie wszystkich wejść oraz przełączenie wyjścia na przedni panel (złącza w obudowie).

Oba elementy wystarczy zbudować poprzez komendę `make`.

## Sterownik
Sterownik znajduje się w ostatnim zagłębionym folderze w sound. Do skompilowania potrzebne są zainstalowane pliki nagłówkowe kernela. Do wczytania wystarczy potem komenda `sudo insmod xonar.ko`, jednak sterownik połączy się kartą oczywiście tylko, gdy odpowiednia karta (Asus Xonar DX), będzie znajdować się w komputerze.

## CLI do sterowania
Znajduje się w folderze CLI. Do kompilacja wymagany jest zestaw bibliotek `libsound2-dev`, który zawiera biblioteki z ALSA API.
