# Another World — installation

Requires Retro-Go SD **v2.0.0-rc1 or later** on the Game & Watch.

The game data is not included: you need the files of the PC DOS version
(`MEMLIST.BIN` and `BANK01` to `BANK0D`).

1. Build the data pack (Python 3, nothing else to install):

       python3 aw_pack.py AnotherWorld.aw /path/to/ANOTHER

2. Copy to the SD card, side by side, with the same name:

       /homebrews/AnotherWorld.bin
       /homebrews/AnotherWorld.aw

3. Start "Another World" from the Homebrew tab. The first start copies the
   pack to the external flash.

Controls: D-pad = move (up = jump, down = crouch), A = action (run, kick,
shoot, hold to charge the gun), B = jump, GAME = pause, PAUSE/SET =
Retro-Go menu (savestates, display). A skips the intro.

---

# Another World — installation (français)

Nécessite Retro-Go SD **v2.0.0-rc1 ou plus récent** sur la Game & Watch.

Les données du jeu ne sont pas fournies : il faut les fichiers de la version
PC DOS (`MEMLIST.BIN` et `BANK01` à `BANK0D`).

1. Créer le pack de données (Python 3, rien d'autre à installer) :

       python3 aw_pack.py AnotherWorld.aw /chemin/vers/ANOTHER

2. Copier sur la carte SD, côte à côte, avec le même nom :

       /homebrews/AnotherWorld.bin
       /homebrews/AnotherWorld.aw

3. Lancer « Another World » depuis l'onglet Homebrew. Au premier lancement,
   le pack est copié dans la flash externe.

Commandes : croix = déplacements (haut = saut, bas = s'accroupir), A = action
(courir, donner un coup de pied, tirer, maintenir pour charger le pistolet),
B = saut, GAME = pause, PAUSE/SET = menu Retro-Go (sauvegardes, affichage).
A passe l'intro.
