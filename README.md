# Synopsis

*sintl* is a tool for translating HTML5 web pages.  It's designed with
simplicity in mind; so for more complex needs, you may want to stick
with [itstool](http://itstool.org/) or other tools.

This repository consists of bleeding-edge code between versions: to keep
up to date with the current stable release of **sintl**, visit the
[website](https://kristaps.bsd.lv/sintl).
The website also contains canonical tool documentation.

What follows describes using the bleeding-edge version of the system.

# Installation

To use the bleeding-edge version of **sintl** (instead of from your
system's packages or a stable version), the process it the similar as
for source releases.

You'll need **expat** on your system.  It's usually a system library.

Begin by cloning or downloading.  Then configure with `./configure`,
compile with `make` (BSD make, so it may be `bmake` on your system),
then `make install` (or use `sudo` or `doas`, if applicable). 

```sh
./configure
make
make install
```

For development, a simple `make` will do.

# Regression and fuzzing

At this time, there's no facility for fuzzing **sintl**, but it's a
relatively easy thing to set up.

The regression tests may be run with the `regress` rule:

```sh
make regress
```

# License

All sources use the ISC (like OpenBSD) license.
See the [LICENSE.md](LICENSE.md) file for details.
