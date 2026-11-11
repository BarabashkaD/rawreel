# Design history

These are the design specifications (`specs/`) that rawreel was built from, kept unchanged as a
record. They describe the project as it was planned at their dates, including names that changed
later (the dvv-build repository was then called `rawreel-workspace`, and the libraries were sibling
folders of one workspace rather than submodules in `libs/`); the READMEs describe the current state.
The implementation plans written from these specs are not published.

## Milestones

Milestones M0 to M4-L (Linux) were built from the specs dated 2026-10-19 to 2026-10-24. After
M4-L, the workspace was restructured: `rawreel-workspace` became dvv-build, and the libraries
became their own versioned repositories, pinned here as submodules in `libs/`. rawreel 0.9.0, the
first public preview, was released on 2026-10-28 from the tag `v0.9.0`, with the libraries at 0.1.0
(dvv-components 0.1.1):
https://github.com/BarabashkaD/rawreel/releases/tag/v0.9.0

The specs dated 2026-11-07 and later cover the work after 0.9.0. rawreel 0.10.0, which added macOS
on Apple Silicon, was released on 2026-11-08 from the tag `v0.10.0`:
https://github.com/BarabashkaD/rawreel/releases/tag/v0.10.0
