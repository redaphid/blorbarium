# The simulator

`sim/sim.cpp` is the firmware in a window, or headless. It includes
`src/main.cpp` whole, so it runs the same setup, loop and dish. Its flags and
script lines are listed at the top of `sim.cpp`. `tools/sim_film.sh` builds it
and holds the frame goldens. `tools/sim_video.sh` and `tools/reel/` film single
pets.

## Filming a duel

Two simulators can hear each other. Each one's duel radio (the `Air` in
`docs/design/BATTLES.md`) runs over a socket instead of ESP-NOW
(`sim/serial_air.h`). `tools/duel_film.py` starts two sims on the two ends of
one socketpair, records both screens, and composes them side by side.

1. Write a duel script in `tools/duels/<name>.txt`. Copy `tools/duels/link.txt`
   and change the sides, the weather and the lines. The format is at the top
   of `tools/duel_film.py`.
2. Run it in WSL `survivor`:

       MSYS_NO_PATHCONV=1 wsl -d survivor --exec bash /mnt/d/<this checkout>/tools/duel_film.sh --twice <name>

   The script builds the sim, then writes `<name>-wide.mp4` (1920x1080),
   `<name>-square.mp4` (1080x1080) and `check/` to
   `D:\projects\bak\blorbarium\duels`. Set `OUT=` to write somewhere else.
3. Look at `check/<name>-*.png` before you show the film to anyone. There is
   one frame from the middle of each caption in each layout, and a contact
   sheet.

`--twice` records the duel twice and fails unless both runs give the same
frames and the same radio log. A run takes about a minute and a half for 22 s
of film.

### What makes a film replay

Each sim runs its own fixed clock. Before a step at time `now`, a sim waits
until its friend has passed `now - latency`. After that, nothing the friend
still has to send can arrive by `now`. So every frame lands on the same step in
every run, however fast either process goes. The latency is at least 1 ms, and
at that minimum the two sims never wait on each other at once.
`test/test_serial_air` proves this with two threads. When one sim exits, the
other stops waiting and runs on alone.

### The weather

`#> weather loss=25,dup=10,reorder=20,late=600,seed=4` sets what the receiver
does to each frame. It drops a share of the frames and doubles some. It holds
some back by up to `late` ms, so later frames overtake them. The knobs also
take `latency` (default 3 ms) and `rssi` (default -45 dBm, which every frame is
heard at). The receiver draws all of it from the seed, so a film replays with
its weather.

In the film, each frame flies from the sender's disc to the receiver's disc.
Broadcasts are teal and unicasts are amber. A lost frame bursts red halfway. A
late frame flies more slowly, and a double flies twice. The ticker under each
disc lists what that radio sent, heard and lost, and what its kid did.

### The kid

`#> side B tinker 3c:84:27:aa:10:5e kid=7:11000:5000` gives a side a kid
(`sim --kid seed:from:turn`, `sim/duel_bot.h`). From `from` ms, the kid presses
BOOT twice, which is the duel gesture. Then, each `turn` ms, the kid thinks,
tilts toward one of three rims, and knocks to lock in. Now and then the kid
also shakes, cheers with a double knock, or presses BOOT for the power. The
kid plays blind on a fixed schedule, because nothing tells it yet when a turn
opens.

### Until the duel engine lands

The Dish does not take the radio yet. Until D9 adds `DishOptions::air`, the sim
reads the radio itself:

- `!air open` and `!air close` switch the radio on and off. A closed radio
  sends nothing, and frames that arrive while it is closed are lost.
- `!air ping <word>` broadcasts `ping <word>`. The other sim answers each ping
  it hears with `pong <word>`, sent back to the pinger.

Every frame is one stderr line, which the film reads:

    air <ms> sent|received|lost|deaf <seq> <other mac> <len> <text, or hex>
    kid <ms> <what the kid did>
