# Changelog

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
