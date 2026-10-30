# Psion Chess files

Psion Chess itself is not part of this repository. Download it from
Classic DOS Games, which lists it as freeware:

https://www.classicdosgames.com/game/Psion_Chess.html

(the file `psion213.zip`) and unzip it into this directory, so that
`Chess.exe` is directly in `data/` (not in a subdirectory). The directory
should then contain:

    Chess.exe   Chess.hlp   Psion.grt   Verm2.13
    Chess1.ovl  Chess2.ovl  Chess3.ovl  Chess4.ovl
    Chess5.ovl  Chess6.ovl  Chess7.ovl

The case of the file names does not matter.

To keep the files elsewhere, set `PSION_DIR` to their directory.

If the adapter does not find Psion's files it will report where
it looked and exit.

Why the files are not included: Richard Lang, who wrote the chess
engine, allows his part to be distributed freely, but nobody could give
permission for Psion's part (the graphics and input/output). See the
Licence section of the main README.
