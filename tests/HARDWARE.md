# Hardware checklist (M1)

For the user, on a real PSP. Copy `dist/SkateItPSP/` to `ms0:/PSP/GAME/` (EBOOT.PBP + data/), play through the list, and tell me the item numbers that failed
(or what looked / felt wrong). Everything below is host-tested but **not yet confirmed on hardware** unless it says so.
Build under test: EBOOT.PBP of 2026-10-15 (built from the current src/, host tests: tests/run_tests.sh).

Which PSP model? (1000 / 2000 / 3000 / Go) - the extra-RAM flag (MEMSIZE 1) is only honoured by the 2000 and later.

## A. Start-up and menus
1. EA logo and the intro movie play (any of Start / Cross / Circle skips). After a run, send `data/vx_log.txt` (it logs the decode timing).
2. Title -> PRESS START -> main menu. The greyed entries (my skate, multiplayer, my spot) cannot be selected; tutorial can.
3. Start career / Freeskate -> skater list: the 3D skater shows, turns, and changes after you rest on a name (~0.1 s). L / R jump 5 names.
4. Location list: the picture and the area name change; L / R jump by area; "N challenges" is shown.
5. Text is crisp (no speckles) and the arrow never clips.
6. Menu sounds (move / ok / back) play.

## B. Options (main menu and the pause menu)
7. Controls: every preset applies; Custom lets you choose a button for each action (a taken button swaps). Leave, quit the game, start again: the mapping is kept (data/settings.sav).
8. Trick guide: categories and tricks scroll; button names follow your mapping.
9. Gravity: 50 .. 150 % for gravity / ollie / push really changes the riding (try 50 % gravity: long floaty jumps).
10. Information: each line toggles (FPS, speed, position ...).

## C. Riding feel (round 10 changes) - Library (Amphitheatre) is the best place
11. Push: a tap = small quick push, holding = a stronger, slower one.
12. Basic pop (X): hold = crouch, release = ollie; flick the nub while held: kickflip (up-left), heelflip (up-right), shuvits (left / right), 360 shuvits (down-left / right).
13. Advanced pop (Triangle): the advanced flips (varial, hardflip, inward heel, 360 flip, laser); both pops held: the body flips.
14. Nollie stance: crouch, hold the nub forward until NOLLIE shows on the HUD, flick: the nollie flip.
15. Steering while crouching: hold the stick sideways with a pop held - you still turn (about 0.1 s after the flick).
16. Grinds with no button: ollie at a rail or ledge: you lock on by yourself. The nub direction at that moment picks the grind. A flick while grinding switches grind.
17. Hop off a rail: a TAP of pop hops and re-grabs, HOLDING it and letting go jumps off the side.
18. No balance bar on rails; a manual still has the bar (pull the nub back and hold).
19. Grabs on L / R in the air (nub picks the grab), spins (push button in the air + nub left / right), wallride on a tall wall at speed.
20. Camera on the D-pad: left / right orbit, up / down distance.

## D. Challenges (career mode: Start career -> any skater -> Elementary School or Library)
21. Gold / green beams stand on the floor. Standing in one shows "START: <name>"; Start opens the pause menu with START CHALLENGE.
22. The 3-2-1-GO countdown (digits + sounds), the objectives top right tick green, "Complete!" and the result sound; Cross returns to skating. Beam turns gold; it stays gold after a restart (data/career.sav).
23. Try one challenge of each kind: Photo (Library "Book It!"), Film (Library "Hit The Books", Elementary School "Lights, Camera..."), Own The Spot (Library "Where's Jerry?").
24. Pause menu: RESPAWN, RESTART CHALLENGE, LEAVE CHALLENGE, OPTIONS (opens over the frozen game), QUIT TO MENU (back to the location list, no restart loop).

## D2. Tutorial (new in this build, main menu entry "tutorial")
24a. The list shows 10 tutorials; the picture-free left panel explains the chosen one; X starts it at the Community Center (a short loading screen first).
24b. The tip for the next objective sits top left in the buttons of YOUR mapping (change the mapping in Options and start again: the names change); it moves on as objectives turn green.
24c. Try: Flickit! (ollie, kickflip, heelflip, pop shuvit, FS pop shuvit - any order), Pushing & Timing (4 pushes, a ledge), Grinds, Nollies, Manuals (flip trick -> land in a manual -> flip out of it), Lines (a line of 2+ tricks worth 400).
24d. "Complete!" then X: back to the list with the next unfinished tutorial chosen and a green tick on the finished ones (kept after a restart). Start -> RESTART CHALLENGE restarts it, QUIT TO MENU leaves.
24e. Which tutorials feel impossible or too easy on the PSP controls? (Manuals and Advanced Grinds are the ones I am least sure about.)

## D3. Career map (new in this build: "start career" no longer goes to a plain location list)
24f. First start: a skater list with only three skaters (Aaron, Rob Dyrdek, Mike); X -> the career map. Areas Urban / Parks / Indoors / Small Spots / Real World; an orange box marks one with an open event (only Parks at first: the tutorials).
24g. Parks -> Community Center -> the event list: the five tutorials are open (orange), the rest shows ??? . The left panel gives the goal, the story text and the state; X on an open event = Go To Event: loading, then the 3-2-1 countdown starts by itself at the event's start spot.
24h. Win it: "Complete!", then X: a menu BACK TO MAP / RETRY / KEEP SKATING (and lines like "1 new event" or "New skater: ..." under Complete!). BACK TO MAP shows the list again with a green box on the finished event; after the 5th tutorial pool A opens (Elementary School, Lake Sherwin, Community Center events).
24i. Pause menu inside an event: RESTART CHALLENGE works, QUIT TO MAP returns to the event list (not the title).
24j. In KEEP SKATING / free roam in a career level only the open events have beams. Walk into one: START begins it as before.
24k. Events that are not built yet (S.K.A.T.E., Jam, races ...) say "Not in the game yet"; SQUARE on them skips them (TEMPORARY, so you can see how far the career goes). Please do skip a few and tell me whether the unlocks (new skaters, chained events like Filming 101 after Lights, Camera) look right.
24l. SQUARE on the area list: choose another unlocked skater; the career remembers it after a restart. Quit the game and start again: "start career" goes straight to the map and the progress is still there (data/profile0.sav).
24m. Freeskate is unchanged: all 19 skaters, every place.

## D4. Races (new: Checkpoint and Deathrace events, 17 of them)
The first one in the career is Sherwin 500 (Urban > Lake Sherwin, Checkpoint vs Shingo; it opens after the five tutorials). Later: Speed School (Parks > Elementary School), Tube Test (Urban > Spillway, a Death Race), the five San Francisco ones (Real World).
24n. Open Sherwin 500: the countdown shows Shingo on the grid next to you (a real skater model with a board, standing on the ground).
24o. At GO he rolls off at once and you have 20 s for the first gate (each gate adds 20 s). Two green beams mark the gate to pass (the posts), a dimmer pair the one after. Passing a gate adds its seconds to the clock (top centre); the right side shows "Gate 2/6" and your place ("1st of 2").
24p. A bail takes 3 s off the clock. Missing a gate = you cannot pass the later ones (they go in order).
24q. The finish: "Complete!" if you were first, otherwise "Failed", with "Final positions" and the times underneath. Then X: BACK TO MAP / RETRY / KEEP SKATING. The clock running out = Failed.
24r. Death races (Spillway Tube Test, Old Town, the five San Francisco ones: the course follows the level's race line down a hill; no gates except the finish): do the opponents stay on the course and reach the finish about when the clock says? Are they beatable? The opponents' cruising speed is capped at 9.5 m/s (the San Francisco recordings need 14 m/s, which the port cannot ride): too easy / too hard? (RACE_AI_MAX_SPEED in src/race.h)
24s. Opponents skate through walls / buildings in places? (they follow a line between the gates, not the physics) - tell me where.

## D5. Score contests (Jam, Best Trick, S.K.A.T.E.: 24 career events). The first ones: EduSKATE (Parks > Elementary School, pool 1), Paris Connection (Real World > Paris, the first Jam, pool 3)
24t. Jam (Paris Connection, 120 s, one opponent): the right side shows "You  <score>" and the opponent's score growing as the clock runs. At the end the higher score wins ("1st place" / "2nd place" under Complete / Failed). Compare the opponent's pace with yours: is a normal good run enough to win? (the opponents' lines are their real DS results; our points are not the DS's, see PLANS M10)
24u. Best Trick (Community Pride, Blind Spot ...): rounds of 90 s ("Round 2 of 3"); only your BEST LINE of a round counts, your total is the sum. Between rounds a 3 s pause shows the round's result. Does the best-line rule feel right, or should a round be one single trick?
24v. S.K.A.T.E. (EduSKATE): the opponent sets a trick (it is named at the top: "Sammy sets: Kickflip"): land the same one within 10 s inside the marked area (the area is not drawn yet: tell me if you cannot find it). Wrong trick / bail / out of time = a letter (S, K, A, T, E on the right). Then you set one: the FIRST trick you land; he copies it with a chance (a letter for him if he misses). 5 letters lose. Nollies count as their own trick (a nollie is not an ollie).
24w. Is a S.K.A.T.E. attempt too short / too strict (the 10 s are the original's)? Is the opponent too good / too bad at copying? (SKATE copy chance: 0.45 + 0.12 x difficulty - 0.0015 x the trick's base points)

## D6. Story, sponsors, ending, credits (M7)
25a. New career (delete data/profile0.sav, or use a fresh memory stick folder): Start Career > pick a skater > Reda's introduction (the speaker's name top left, the text in a panel, X = next page, O = skip). Is the text readable on the PSP screen? Does the order feel right (the scene, then the map)?
25b. After the five tutorials and the first pool: Pop Quiz, then Paris: the trip scene shows when Paris opens; Go To Event on Paris Connection shows the Lucas scene first (once), then starts; after winning it, the next scene.
25c. A sponsor event (Pool C, e.g. a board-sponsor event at the MegaPark): Go To Event shows the sponsor's scene first (its picture and name above); win it: the result menu says SPONSOR with SIGN WITH <NAME> / NOT NOW. NOT NOW: back to the usual result menu, the event stays open (try another sponsor). SIGN: "Signed with ..." on the result, back on the map the win scene, the "has been signed to" screen with the sponsor's picture, then the pool-end scene.
25d. Map > the row under the last area: Sponsors (four slots with the chosen sponsors and their pictures).
25e. Finish the career (or use a profile with everything done: hard; at least check that the last scenes read right on the PSP): the Thrasher cover, the end text, then the credits (scrolling, X = faster, O = leave); Options > Credits plays them too. Options opened from the pause menu has no Credits entry.
25f. An old profile (from before M7): it must load, and the scenes of what it has done already must NOT replay.
25g. Memory: the new pictures (21 sponsor icons + the cover) add about 0.3 MB to ui.pak: anything strange in the heap lines on the menu?

## D7. Stance and Game settings (M8)
26a. Options > Game settings: Stance / Units / Sound effects (Music is not listed while the music is off). Left / right change, X steps on. They are saved (leave and come back).
26b. Stance: Freeskate with Koston or Lucas Puig (regular in the ROM) and with Danny Way or Aaron (goofy): are they mirror images of each other on the board? Then force Regular / Goofy in the settings and ride Aaron: does the rider turn to the other side, board and feet together (no feet floating, no board the wrong way round, no flipped textures that look broken)? Tell me what looks wrong.
26c. Tricks while regular: a kickflip / heelflip / pop shuvit still do their own thing (the animation is mirrored: does it look natural?). A turn to the left in the air now scores "FS SPIN" (regular) and "BS SPIN" (goofy): the trick line at the bottom left. Same for a rail on the right (BS for goofy, FS for regular) and the wallride side.
26d. The trick guide (Options > Trick guide > Spins) names the spins by the stance of the rider you ride.
26e. Units: Options > Information > Speed shows km/h on Meters and mph on Feet.
26f. Sound effects volume: 0% is silent, 100% as before, in between in steps of ten (also from the pause menu's Options while skating).
26g. The skater select screen shows regular riders mirrored (the rider turns on the spot).

## D8. My Skate (M9)
27a. Main menu: My Skate is selectable now (Customize in it is greyed). Game Settings from it is the same page as in Options (O goes back to My Skate), Credits too.
27b. Select Profile: three slots, the one in use is marked green. Choose another: a new career starts there (a new profile asks for the skater, plays Reda's introduction), the first one is kept. Quit the game, start it again: it comes back on the last profile used. Square on a used slot asks, then clears it.
27c. Log Book > Statistics / Personal Bests: skate for a few minutes (push, grind, manual, powerslide, a long jump, bail a few times, land some combos): do the numbers move sensibly? (playtime, distance travelled / grinded / manual / powerslide, pushes, wipeouts, airgrab time; best airtime, longest grind / manual / powerslide, best scoring sequence). Feet or metres follow Game settings > Units.
27d. Trick Book: every trick of the game (81) with the how-to text on your buttons; a green mark once you have landed it in a combo that was banked (a plain ollie does not count; a bail loses the combo's tricks).
27e. Unlockables: the skaters that are unlocked by name (the others ???) and the counts of gear.
27f. The old profile (before M9) loads: the statistics start at zero.

## D9. Customize (M11)
28a. My Skate > Customize: the viewer on the left shows a skater that turns; the list on the right: Gender, Build, Skin tone, Face, Hair (+ Hair colour), Beard (+ Beard colour), Hat (+ Hat design), Top, Logo, Bottom, Shoes, Done (the colour and design rows appear when there is hair / a beard / a hat). LEFT / RIGHT (or X) change a row, L / R five at a time on the long lists. Does the viewer follow every change (it rebuilds the skater: how long does a change take? a noticeable pause is a finding)?
28b. Look at the skater from the viewer: the top / bottom / shoes / face / skin must be where they belong (no stretched textures, no wrong colours at the arms, hands or neck, no gaps between the parts). Try a woman, each build, a top with short sleeves (T-shirt) and with long ones (hoodie, cardigan), shorts and trousers.
28c. Done keeps it and makes it the career's rider. Free skate and Select Skater (map, Square) list "Custom skater". Ride it: the board, the animations and the tricks are the same as for the pros (the skeleton is shared): any part that bends wrongly or pokes through the clothes is a finding.
28d. O leaves Customize without keeping what was changed. Opening it again starts from the skater that was made.
28e. The new parts: every hair style in the three colours (does it sit on the head, no hair through the face?), a hat over each hair style (hair and hat together may poke through each other: tell me which), beards (they are a texture on the face), the logos on the top (they must read the right way round on the chest: the picture is mirrored on purpose; tell me if any is backwards), a woman's cap for long hair (women only).
28f. Memory: the parts (1.7 MB) are loaded only while Customize is open and while a custom skater is being built: anything odd in the heap line afterwards (Options > Information > Heap)?

## D10. Recorded runs on the screen, ambient skaters, board art and gear unlocks (M14)
29a. Races: the opponents skate the path the original's skaters took (not the straight line through the gates): on the San Francisco hill, in the Spillway tube, in Old Town and at Lake Sherwin. Do they stay on the ground / in the tube, bail and respawn sometimes (the original's recordings have bails), and are they at the finish gate when the HUD says they finished? Is the pace still beatable (the cap of 9.5 m/s on the player's course is unchanged)?
29b. Jam events: the opponent skates around the place doing his recorded tricks (flips, grinds, grabs, pushes, bails) while the clock runs; his score on the HUD is still the recorded one. Best Trick: the opponents start over at the start of every round; the two 20-second Best Trick contests have no opponents on screen. S.K.A.T.E.: when he sets a trick he skates up and does it; after your trick he does a copy or fails one. Tell me if the timing is wrong (he does it after you start, or never).
29c. Freeskate: three riders skate around the place (random pros), route after route, and do not start next to you. Check: frame rate with them (Options > Information > FPS) against a place without them (Spillway has none), the heap after loading (3 more skaters = 2.5 MB), loading time of the session, riders that float, sink, or ride through walls (the routes are the original's but the collision of the port differs). They do not collide with you: riding through one is not a bug, but tell me if it looks bad.
29d. My Skate > Customize: new rows Deck and Wheels (the board in the viewer shows the art; the art must sit where the board shows it: deck bottom, wheels on the wheels, no stretching, no magenta). Start a NEW profile: shoes offer 4 pairs (generic), decks 7, wheels 5 designs, hats: fedora, flat cap and helmet only (with the diver / helmet designs). Win a board sponsor challenge (sign Almost, Baker ...): the sponsor's decks, wheels and caps appear at once (also its shoes after a shoe sponsor). Done, then ride the custom skater: the board has the chosen art (also in the free-skate preview). Hats were pushed out of the head and the hair under them pulled in (clipping fix): check every hat style with a few hair styles (afro, mohawk, long): does the hat sit on the head, hair not through it, no gap? Does anything in the list look wrong for the hat design of the style (a fedora in grey / blue is a guess)?
29e. An old profile with a custom skater: it keeps what it had (the old art, the old shoes), Customize opens on it and moves anything it may not have to the first value it may.

## D11. Story scenes with the guest on screen (the NIS decode)
30a. Start a new career: the first story scene (the introduction) now shows the guest (Reda, holding a board) standing in a level behind the dialogue panel, the camera 2 m in front / behind him, 1 m up, looking at his chest; the speaker's name is the title. Does the scene load (one LOADING frame), how long does it take, and does the front end still work afterwards (start the event, go back to the map, another scene)? Pages advance with X as before; O skips the whole scene.
30b. THE QUESTION: does the guest face you, or turn his back? The numbers of the original put him at a fixed 180 degree turn and the camera on the +z side of him; I could not tell from the code which way his front looks, so the default is "faces the camera" and SQUARE turns him round (the hint at the bottom says so). Tell me which way the original has him (surely he should face you: if SQUARE is needed, the default is wrong and I will swap it), and whether the turn is smooth (it is a plain flip, no animation).
30c. The guest plays a conversation clip (the original's: talking, gestures, picking up / putting down the board) and goes on to another one when it ends, with a short blend (0.3 s). Is the motion right: no sliding, floating above or sinking into the floor, no pops at the change of clip, his board where his hand is? A scene of two people (the first trip to Paris, scenes 3 and 4) changes the guest half way: does the new person appear at the right page?
30d. Where he stands: in a scene of a sponsor event he stands on the event's start grid; others in the place's starting spot. Is he on the ground, not inside a wall or a ramp, and is the camera inside nothing (the 1.4 m and 1.5 m camera distances of some scenes are closer)? Look at a few places (MegaPark, South Bank, Paris, Shanghai, Rio).
30e. The 12 scenes that name no place (the introduction, the trips, the later chapters) are filmed in the place of the event they belong to, or in the first level when there is none (my choice: the original names none). Tell me if one looks wrong there. Scene 57 (a wide shot without a speaker) stays text only.
30f. Frame rate on the story pages (Options > Information > FPS is not shown there: judge by eye) and memory: the level and the guest are loaded while a scene is on screen and freed when it ends or an event starts (the event must start as quickly as before).

## E. Other levels, memory, speed
25. Play 5 minutes each in two other places (suggested: Community Center, Spillway). Report: falls through the floor, walls you ride through, places you cannot leave, FPS dips.
26. HUD detail (Options > Information: FPS, Heap): note FPS in a busy spot, and the heap numbers on your PSP model (a PSP-1000 has less RAM than a 2000/3000).
27. Quit to menu and pick a different level three times in a row (memory must not run out).

## Report format
`<item number>: ok` / `fail: what happened` - guesses are fine ("feels too floaty"). Anything not listed that annoyed you counts too.
