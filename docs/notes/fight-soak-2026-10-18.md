# Fight mode on two real badges, 2026-10-18

Two badges on a Linux laptop's cable (`5C94095003` = ACM0, MAC d4:05:92:77:1b:30;
`5C94094497` = ACM1, MAC d4:05:92:77:1c:b0), real ESP-NOW between them, no BOOT
press anywhere unless a scenario says so. Driven by `tools/duel_soak.py`; logs
were in `.soak/` (not committed).

## Findings

1. **An unpinned PlatformIO platform sealed every badge it flashed.** This
   laptop had a Tasmota espressif32 (IDF 4.4.5) installed; its LittleFS could
   not mount `petfs`, so both badges booted Sealed ("SAFE BUT STUCK - ASK A
   GROWN-UP"). Nothing was written; reflashing with `espressif32@6.9.0`
   (IDF 4.4.7, what the releases use) resumed both pets. Fixed in 797b052.

2. **A badge that hears nobody blocked every fight in the room** (fixed,
   0891feb, firmware 0.7.10). Each badge claims the lowest-address friend it
   hears and kept the claim while that friend stayed open. A badge everyone
   hears but who hears nobody (across a room, in a case, a body in between:
   a one-way link) claims nobody forever, so with a low address he held
   everyone's claim forever. Under loss a second deadlock: two badges who
   each first heard such a badge each claimed it, and then looked "already
   paired" to each other forever. On the hardware, with three such badges in
   range (DEBUG net ghosts 3 0 low), A and B side by side **never paired in
   120 s**; without them, 1.0 s. After the fix: 13.7 s past three, 1.4 s past
   six busy pairs. See the commit body for the rules.

3. **Two readiness lockouts a kid can hit with no button** (by design, NOT
   changed; owner's call):
   - *Too sleepy to wake.* Asleep with sleepiness >= 0.9, a hang (or double
     press) is refused (`gate=asleep:sleepy:1.00 result=refused`) and he
     sleeps on. Pets get that sleepy at pet night and after a few duels, so
     for hours no hang starts a fight. `test_two_dishes`'s
     `AnAsleepPetWakesForAFightUnlessTooSleepy` codifies it. 0.7.9 removed
     the same lockout for awake pets because the case has no button;
     this one remains. Candidate cause of "we could never enter battle mode".
   - *Three losses in a row want a cuddle.* `gate=cuddle`: he beacons but is
     never matched until cradled (held still in a hand ~3 s). It clears with
     the IMU alone (`DEBUG cradle` proved it through the real detector), but
     a kid must know to do it.

4. **Only one DEBUG line could be pending** on the serial link, so a script's
   back-to-back lines silently ran only the last. Now a queue of 8.

## Scenarios (tools/duel_soak.py) and results on the fixed firmware

Each scenario reboots both badges radio-deaf (`DEBUG reboot quiet`), cuddles
them, sets its conditions, lifts the blackout, and checks: one outcome hash on
both sides, opposite sides, no one-sided meeting, no crash or surprise reboot.

| Scenario | What | Result |
|---|---|---|
| clean | no input at all | PASS, join 1.0-1.3 s, every run |
| hang_wake | both asleep; a hang each wakes him into a fight | PASS (smoke run); one harness flake (pet woke before the check) |
| swing_through | 5 flips of 400 ms | PASS, never an ask |
| wobbly_hang | tremor + jolts while upside down | PASS |
| hang_in_lobby | hang leaves, fight mode reopens, still finds the friend | PASS |
| hangs_in_battle | random hangs (wobbly too) all battle long | PASS, one hash |
| noise_in_battle | presses, holds, doubles, leans, hangs mid-battle | PASS, one hash |
| hang_on_friends | a hang leaves the friends screen | PASS |
| loss_20 | ~20 % frames lost each way | PASS |
| ghosts_deaf / phantom / paired / storm | made-up badges in the room | PASS / PASS / PASS / fixed after (2), see below |
| too_sleepy | documents finding 3 | refused as designed |

Not run to completion before the wind-down: loss_50, loss_80, dup_reorder,
blackout_short / one_way / long, reset_midbattle, legacy_double,
back_to_back, ghosts_storm on the final build. Run them with
`uv run tools/duel_soak.py /dev/ttyACM0 /dev/ttyACM1 --log soak.log`.

Health over the run: free heap never under 41 KB, stack high-water >= 9.3 KB,
no panic, watchdog or brownout.

## More than two badges

Two boards can only make a crowded room by faking it. `DEBUG net ghosts n
mode [low|high]` replays a real friend's beacon under made-up MACs: mode 0
hears nobody, 1 claims me and never greets, 2 is busy with another ghost.
The native suite covers real crowds: 3, 4 and 9 badges in a storm, a deaf
badge below or six above two who hear each other, a phantom, four plus a deaf
one, and a messy room of six plus two deaf and a phantom (25 storm seeds).
All pass; with the old pairing the deaf-badge tests fail.

## Native suite on this laptop

748 pass. test_ota and test_adopt_species do not compile under g++ 11
(`std::variant` default constructor) and test_dish's stack budget reads 6968
against 6144; all three fail identically without these changes (toolchain).
