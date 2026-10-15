# Development and contributions

Build and run the self-contained regression commands in README.md before submitting a change. Keep source comments, documentation and UI strings in English.

Do not commit commander journals, browser profiles, sample coordinates, local paths, authentication data, binaries produced by a build, RenderDoc captures, or extracted game shader/image fixtures. The checked-in `defaults/` preset is intended to be shareable; runtime copies are ignored by Git.

Changes to the shared integration protocol must update its version and document the one-time Elite restart. Keep the existing menu/map detector and cursor alignment regressions intact when extending cockpit profiles. New cockpit actions must wait for observed selection feedback before confirming.

A pull request should describe the resulting behavior, validation performed, and any headset/controller/game-rendering combinations actually tested. Do not report offline replay as live VR verification.
