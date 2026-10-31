# Psion Chess files

Psion Chess itself is not part of this repository. The simplest way to
install it is

    ./psion -d

(`psion.exe -d` on Windows). This shows its copyright status, asks for
confirmation, then downloads `psion213.zip` from Classic DOS Games with
`curl`, checks it and installs its files here (or in `$PSION_DIR`).

To install it by hand, download it for example from Classic DOS Games:

https://www.classicdosgames.com/game/Psion_Chess.html

(the file `psion213.zip`) and unzip it into this directory, so that
`Chess.exe` is directly in `data/` (not in a subdirectory). The directory
should then contain:

    Chess.exe   Chess.hlp   Psion.grt   Verm2.13
    Chess1.ovl  Chess2.ovl  Chess3.ovl  Chess4.ovl
    Chess5.ovl  Chess6.ovl  Chess7.ovl

The case of the file names does not matter.

To avoid running unverified software, the adapter checks that each file
has the SHA-256 below (from `psion213.zip`) before running Psion, and
refuses to run it if one is missing or different. To check a download by
hand, run `sha256sum *` in this directory:

    df1691734fe9f1082bca3f5513380807104aa4cc9b68ae97ce2bbff74221660c  Chess.exe
    238a2f23bb9ad36c9b3bc620b91b127899fd7b8a5ef299db1786e482cac67e6a  Chess1.ovl
    37b735722a51b54ac2201a4e2d7f18addc1ca7591399fe82d7298b60e5e27950  Chess2.ovl
    63bf94f4fc0a5152551e491d6bb9ce6b67e12f5baebca23979a3bbee7dfc7d69  Chess3.ovl
    d8d79199492150c2b4564dc3289b970da168b8f647c7bc4d516bbde522834714  Chess4.ovl
    6cc735059dba6b3e1617cf9dd1f7f3b2a8e13001fe16e886e7d81a6dd6254ebd  Chess5.ovl
    8bf8c86dd0f93390dad3db42d7a809b1911181ff24421ec4558393df0c113857  Chess6.ovl
    a1f1a0c506ef0df74cf3bbcd68b93b32aa3631d1d9a8712ce6b170545e00991d  Chess7.ovl
    ede583b2768c8b9d1548c107421d9a469df7f86dce57e254b1e8e90cc7464353  Chess.hlp
    f48970a275d44dce41fe39a779d1e71e66880ec36f1ddcddccf1997dfe6e5b1c  Psion.grt
    e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  Verm2.13

(`Verm2.13` is empty.)

To install the files elsewhere, point the environment variable `PSION_DIR`
to their directory.

Why the files are not included: Richard Lang, who wrote the chess
engine, allows his part to be distributed freely, but nobody could give
permission for Psion's part (the graphics and input/output). See the
Licence section of the main README.
