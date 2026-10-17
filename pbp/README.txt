SKATE IT PSP
============

A native PSP port of Skate It (Nintendo DS, USA, Electronic Arts 2008): the game engine is rewritten for the PSP, it is not an emulator. The game data is
converted from the user's own cartridge; nothing of the game is included in the source.

INSTALL
  Copy the folder SkateItPSP (EBOOT.PBP and data/) to ms0:/PSP/GAME/ on the Memory Stick. About 60 MB.
  Built for the PSP's full memory (MEMSIZE) and tested on a PSP; the older PSP-1000 has not been tried.
  Profiles, settings and the trick table are written into ms0:/PSP/GAME/SkateItPSP/data/ (profile0.sav ..., settings.sav, tricks.cfg).

CONTROLS (defaults; Options > Controls changes them, Options > Trick guide lists every trick)
  Analog nub        steer; a quick flick while popping = a flip trick; nub + flick in the air = the grab / grind variant
  CROSS             hold to crouch, release to ollie
  TRIANGLE          advanced pop (varial flips, hardflip, 360 flip ...); both pops together = body flips
  SQUARE            push (tap = quick, hold = strong); in the air, held with the nub left / right: spin
  CIRCLE            brake; flick left / right = revert; nub held hard over at speed = powerslide
  L / R             in the air: back hand / front hand grab; both = double grab
  Nub down / up, held   manual / nose manual (the nub balances it)
  Grinds and wallrides need no button: the board locks on by itself, the nub picks the grind.
  D-PAD             camera: left / right orbit, up / down distance
  START             pause menu
  SELECT + L        HUD detail lines on / off

MENU
  Start Career, My Skate (profiles, Log Book, Game Settings, Credits, Customize), Tutorial, Freeskate, Options. Multiplayer and My Spot are not built.

KNOWN LIMITS
  Customize and the PSP-1000 have not been seen on real hardware. Music is converted but switched off. Opponents in Jam / Best Trick / S.K.A.T.E. are not on screen.
