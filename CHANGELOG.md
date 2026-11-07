# Changelog

## Unreleased
- New: pings. Press G (change it, turn it off, or pick the sound, range and icons in the Mod Configuration Menu) to mark the spot you're
  aiming at. Players near you see an icon, your name and how far away it is, for 15 seconds, at the edge of the screen
  if it's out of view. Containers and items show a loot icon and their name, workbenches and the like a utility icon.
  If you aim at nothing, it says "Over here" where you stand.

## 0.1.10
- Everyone needs 0.1.10 to play together.
- New: a log of nearby conversations on the left of the screen (by LoAlCo). When a friend near you talks to someone,
  you see what they picked and what they were told, in your HUD's colour. Its own page in the Mod Configuration Menu
  (New Vegas Co-op > Conversations): on or off, distance, how many lines, how long they stay, text size, spacing, an
  outline, shadow or dark bar to read it over bright places, dividers and colour.
- The new game's co-op questions and Quick start (with its "finish your character" menu) are only for a new, level 1
  character. A played character ending up in Doc Mitchell's house, as Tale of Two Wastelands' train from the Capital
  Wasteland seems to do, was asked again and could redo their S.P.E.C.I.A.L. and skills.

## 0.1.9
- Everyone needs 0.1.9 to play together.
- Weapons and armour lying on the ground keep their condition and weapon mods for everyone: a worn, modded gun dropped
  by one player came out brand new and without its mods in the others' games (a free repair; duplicated ones were all
  at full condition).
- Two players grabbing the same item at once: the slower one gives back that very item, not another of the same kind
  from their inventory (say, their own gun in place of the worn, modded one they just grabbed).
- Closing the game's window during co-op (instead of using Exit Game) no longer crashes the game as it closes, and a
  crash while the game is closing no longer leaves it hanging for a minute.
- Mr. House now dies in friends' games too when he's killed in the host's (their copy of him stayed alive).
- Looting a body as a friend: it keeps the clothes and armour it wore, at the host's condition. A friend's game
  dressed most NPCs in its own random outfits, so a body's gear changed as it was looted, and a body could take out
  its own gun as it died.
- Fixed NPCs missing from a friend's game while the host could see them: on the Strip, two Securitrons, a traveller
  and four NCR soldiers vanished for a friend who walked past one of the Strip's street scenes (the game's own
  script for the scene ran in the friend's game alone, with the friend's own save's values).
- Fixed from a check of the whole mod against the decompiled game:
  - Joining again without restarting the game: boxes and bodies showed your own loot instead of the host's until
    you took something.
  - After the host loaded an earlier save, friends' games kept switching on or off what the host's had before the
    load (NPCs and objects the host didn't have).
  - Someone who joined a game and then hosted one could show their new friends the old host's face.
  - When a friend skipped a line in a conversation, the NPC kept talking in your game once many NPCs had spoken in a
    session.
  - The game over's kill no longer asks the game to cut off a body part.
  - Crash reports: a crash could bounce between ours and Steam's crash reporter, and a small runtime error could use up
    the crash dump.
  - Mod downloads: a host's file list can't fill a friend's disk any more (checked against free space), and a few
    more Windows device names are refused as file names.
  - A friend not yet let in no longer loads or saves when the host says so.
  - A typo in NVCoop.ini's Mode no longer turns on the testing mode that hosts by IP by itself.

## 0.1.8
- Co-op balanced and friendly fire are now two switches in the Mod Configuration Menu, for all your games, both off
  at first, and changeable any time (the host's count for everyone; friends already in get a change at once). A new
  game asks once, with a note to leave Co-op balanced off if you use mods that change the game's balance. There's no
  "Normal" mode to pick any more, and saves no longer keep a mode of their own: if you played Co-op balanced,
  switch it on once in MCM.
- Mod Organizer 2: a different load order isn't "fixed" by co-op any more (MO2 sets the order itself: the fix could
  leave a game that crashed on start). Match the host's order in MO2 instead; the message says so.
- A different load order now names the first plugin where yours and the host's split.

## 0.1.7
- Loading a save in co-op no longer pops up the Co-op Teleporter's box by itself.

## 0.1.6
- Tale of Two Wastelands 3.4: its Fallout 3 archives (Fallout3 - *.bsa, made by its installer on each PC) no longer count as different between players, and an archive invalidation file (Fallout - Invalidation.bsa, made by mod managers) is no longer compared at all.

## 0.1.5
- Tale of Two Wastelands: players on the same TTW version are no longer told their TTW archives differ (its installer builds them on each PC, never quite the same), and TTW's files are never offered for download (they're made from your own Fallout 3). A different TTW version is still caught, with the right advice.
- Drag downed friends: tap Z next to one to grab them, tap Z again to let go. You walk with your weapon away while dragging, and their bleed-out timer is paused until you let go, so you can pull them to cover and revive them there (hold E, as before).

## 0.1.4
- New license (LICENSE.txt, now in the download too): use and change it freely, no reuploads; a changed version you share must be meaningfully changed, credit the original, and be open source under the same license (code rewritten or ported from ours, by hand or with AI tools, counts as ours; ideas, features and balance values are free to use).
- TTW's and New California's quests are shared like the base game's (the host's game runs them for everyone), with nothing to set: before, they ran separately in each player's game unless listed in HostQuestMods.
- Other starts than Doc Mitchell's house work (TTW's Vault 101, New California, DUST): the quick start question only comes up in Doc's house, co-op set to start from NVCoop.ini no longer waits for New Vegas's tutorial, and a friend's own start can give back the Pip-Boy and fighting it locked (they stayed locked).
- Fixed the Mod Configuration Menu not opening since 0.1.3: our Co-op Address button in the pause menu got in its way (MCM only shows its own button when the pause menu has its usual buttons). Your address for friends joining by IP is now under MCM > New Vegas Co-op > Show My Address.
- Co-op balanced: bigger fights from two players on (they started at three). Each enemy that fights the group brings half an extra one with 2 players (about 50% more enemies), a whole one with 3 (twice as many) and one and a half with 4, with at most 8 extras alive at once. And they all come now: when many enemies started fighting at once, some never did.
- Co-op balanced: ammo is worth twice as much (vendors charge double for it; selling it pays more too), and boxes and vendors hand out half the rounds they did, on top of giving ammo half as often.
- Co-op balanced: fewer skill points a level now really works (7 + half your Intelligence instead of the game's 10 + half: the game ignored the setting it used before), and kills give a quarter less XP as meant (by mistake they could give more).
- A mod's script taking away a perk the host doesn't have (Enhanced Movement does it all the time) is no longer sent to every friend many times a second: a perk a script adds or takes away only goes to friends when it really changes.
- Fixed a friend's game crashing in big fights: an enemy that died on the friend's screen but not the host's is brought back, and one swinging a melee weapon or its fists right away could crash it.
- Fixed a crash in the game itself when the console or a mod's script takes all of a character's items (RemoveAllItems) while they wear one of two identical outfits, as freshly placed Powder Gangers do.
- Fixed a crash choosing how many of an item to take or put in a box (the "how many?" prompt) just as the box's contents were matched to the host's: sharing boxes' contents now waits until the prompt is closed.

## 0.1.3
- Fast travel points unlock for everyone: a place any player discovers can be fast travelled to by the whole group (and it stays in everyone's save).
- A Co-op Teleporter in everyone's Aid items while playing together: use it to go to a friend or bring a friend to you (for when someone gets stuck). It can't be dropped or sold, and it's taken away when co-op ends.
- Friends show on your Pip-Boy map (with their character's name), and as a tick on the compass when they're near.
- Friends sitting in chairs show sitting in your game too (and stand up when they do).
- Friends' characters no longer push you or each other when everyone arrives at the same spot (they stand just beside you instead), and they're hidden during cutscenes so they don't block the view.
- Join by IP address, for port forwarding or a free VPN (Tailscale, ZeroTier, Radmin VPN), all from the main menu: Host Co-op asks who can join (Steam friends, or by IP too), and Join Co-op asks how (a Steam friend's game, or by IP: type the address, shown as stars and remembered for next time). Addresses are never shown on their own or written to the logs (safe for streaming): the host finds theirs in the pause menu's Co-op Address. The host still lets everyone in.
- Works with animation mods that use kNVSE (tested with Enhanced Movement's sprint): your friends see the animations those mods give your character, at your real speed (sprinting included). Everyone needs the same animation mods installed.
- Friends' characters follow their players much more closely: about half the delay (around 300 ms down to 140 ms in our tests), no more flicking between walking and running, and fewer snap-backs.
- Downed players lie on the ground in pain instead of ragdolling (the ragdoll kept glitching). A downed friend gets a red "DOWNED" marker over them with the seconds left before they bleed out, and holding E to revive shows a progress bar under the crosshair.
- If JIP LN NVSE, kNVSE or Real Time Menus is missing (or didn't load), the game now says so on screen and won't start co-op, instead of freezing later (saving or loading from the menu froze without Real Time Menus).
- Everyone needs 0.1.3 to play together.

## 0.1.2
- Boxes and bodies no longer change their items right after a friend opens them (a friend's game first showed its own random loot, or the 500 "show" bullets on a dead enemy, then switched to the host's). They're now matched as soon as the friend aims at them.
- A friend's own reputation changes (killing a townsperson, say) now count for everyone, instead of being undone by the host's a few seconds later. Everyone else also gets the "Infamy gained" / "Fame gained" message.
- Everyone needs 0.1.2 to play together.

## 0.1.1
- Level-ups wait out fights for everyone: the level-up screen no longer pops up for friends while enemies are still attacking them.
- Starting a new game with Join Co-op now waits until Quick Start is completely done (character finished and saved) before joining, instead of pulling you in with the "finish your character" screen still open.
- Friends' characters no longer cower ("hide") when enemies are near.
- If the game freezes for 8+ seconds during co-op, NVCoop.log now records where it got stuck, so freeze reports can be fixed.
- Everyone needs 0.1.1 to play together (0.1.0 and 0.1.1 can't connect).

## 0.1.0
- First release.
