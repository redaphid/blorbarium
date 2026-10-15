# Situations

A situation is the `when` column of `lib/blorb/include/blorb/defs/thoughts.def`:
the condition under which a line may be said. Several lines can share one.
Each line in `personalities/_shared.txt` is headed by its name and its situation.

**Thoughts** (lines that are not `p_...`) come now and then, between quiet gaps of
one to three minutes. Of the lines whose situation holds, the highest priority
wins. Priority 7 and up (`kUrgentThought`) cuts a quiet gap short.

**Prophecies** (lines named `p_...`) come when the board is shaken: instead of
hopping he foretells. One is drawn among those whose situation holds, weighted
by his oracle's taste for the topic (food, danger, sky, owner, doom, luck,
pond) times one plus the priority.

A `#` in a line shows a number. Only the situations marked **#** give one.
An `@` shows the name the owner gave him (only the `named...` situations), and
a `*` a line the owner taught him from the phone (only `taught`). One of
`#`, `@`, `*` per line.

The `wall_...`, `named...` and `taught` situations are what a phone adds. A
pet that never meets a phone never says them; he still says everything else.

| situation | when it holds |
|---|---|
| hungry | He is half hungry or more. |
| starving | He is very hungry (eight tenths or more). |
| hoarding | He is awake and not hungry, and a fresh pellet sits in the dish. |
| chewing | He has a fresh pellet in his mouth. |
| rotten_bite | He has bitten a rotten pellet. |
| rotten_near | He is awake and a rotten pellet sits in the dish. |
| sick | Poison from rotten food is in him. |
| hurt | He is injured (a battle, a bad fall). |
| knocked | He is awake and someone just tapped the dish. |
| shaken | He is hopping from a shake right now. |
| dreads_shake | He is awake, being held, and has learned that shaking frightens him. |
| dreads | He has learned that shaking frightens him. |
| scared | He is fairly frightened. |
| ancestor_fear | **#** He is a little frightened, and a fear he inherited was learned by an ancestor. # is that ancestor's generation. |
| heirloom | **#** He is awake and resting, and carries a lesson an ancestor learned. # is that ancestor's generation. |
| foreseen | **#** He carries a lesson an ancestor learned. # is that ancestor's generation. |
| bored | He is awake and very bored. |
| chasing | He is chasing something. |
| held | Someone is holding the board, but not cradling it. |
| cradled | Someone is cradling the board gently. |
| upside_down | He is awake and the board is upside down. |
| drowsy | He is awake and very sleepy. |
| dreaming | He is asleep and dreaming. |
| foreseeing | He is in his oracle trance. |
| sees_pellet | He is in the trance and there is a pellet in the dish. |
| sees_phone | He is in the trance and a phone is near. |
| sees_famine | He is in the trance, the dish is empty and the pantry is bare. |
| elder | He is awake and an elder. |
| old | He is an elder. |
| night | He is awake at night. |
| dark | It is night. |
| light | It is day. |
| lonely | He is awake and very lonely. |
| missing_you | He is fairly lonely. |
| phone_near | A phone is near. |
| phone_long_gone | He is awake and no phone has visited for twelve hours of play. |
| learned_food | He is awake and a little hungry, and has learned that eating fixes hunger. |
| always | **#** Any time he is awake. # is his generation. |
| anytime | **#** Any time at all. # is his generation. |
| wall_hour | **#** He is awake and a phone has told him the real time since he was last plugged in. # is the hour on a 12-hour clock. |
| wall_morning | He is awake, and the real time (from a phone) is 6 to 10 in the morning. |
| wall_late | He is awake, and the real time (from a phone) is midnight to 5 in the morning. |
| wall_monday | He is awake, and a phone says it is Monday. |
| wall_weekend | He is awake, and a phone says it is Saturday or Sunday. |
| wall_friday | A phone says it is Friday. |
| named_awake | He is awake and the owner gave him a name on the website. @ is that name. |
| named | The owner gave him a name on the website. @ is that name. |
| taught | He is awake and the owner taught him a line from the website. * is one of those lines. |
