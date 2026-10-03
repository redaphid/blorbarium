# Design notes archive

Archived from the design session of 2026-10-02 to 2026-10-03 so they outlive it. Files are copied unedited.

**Read `design/user-asks.md` first.** It lists the user's binding decisions; later items override earlier ones, including anything in the design documents below.

## Index

- `design/user-asks.md` - the user's binding decisions and requests, in order.
- `decisions.tsv` - log of decisions (what, why, evidence, result).
- `design/research-creatures.md` - how Creatures (C1 to Docking Station) worked, and what a single ESP32-S3 pet should keep or drop.
- `design/explore-device.md` - survey of the device layer and toolchain, from the claude-notification-screen repo.
- `design/explore-ota.md` - survey of OTA over BLE in cyber-puck and claude-notification-screen.
- `design/grungo.md` - what Grungo is and what blorbarium needs to draw him.
- `design/art-plan.md` - art needs and how the sprite-expressions pipeline makes each piece.
- `design/final/DESIGN.md` - v1 final design of the creature engine.
- `design/final/SYNTHESIS.md` - v1 synthesis note: base candidate and what was grafted in.
- `design/v2/candidate-a/DESIGN.md` - v2 arena candidate A design.
- `design/v2/candidate-b/DESIGN.md` - v2 arena candidate B design.
- `design/v2/candidate-c/DESIGN.md` - v2 arena candidate C design.
- `design/v2/final/DESIGN.md` - v2 final design (alife v2).
- `design/v2/final/BUILD-PLAN.md` - v2 build plan: small checkable units ordered by fun per cost.
- `design/v2/final/SYNTHESIS.md` - v2 synthesis of the candidates.
- `design/v2/final/sketch/` - header and `.def` sketch sources for the v2 final design, with a usage check.
- `learning-depth.md` - what Grungo can learn and how deep it goes (read-only investigation).
- `e2e-learning.md` - end-to-end simulator test: does Grungo learn from what happens to him.
- `verify-engine.md` - independent verification of the `engine` branch at a693a0b.
- `fixup-engine.md` - fix-up pass on `engine` from a693a0b to 36ceffd.
- `sprite-space.md` - sprite space on the badge and room for more Grungo.
- `stories/stories.md` - five stories about the blorbarium the user wants, with the mechanics each needs.

Not archived: `depth-results.md` did not exist in the scratchpad; story images and build outputs were left out.
