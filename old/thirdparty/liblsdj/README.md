# libLSDJ

![libLSDJ logo](https://4ntler.com/liblsdj_banner_github.png)

![Passing Tests](https://github.com/stijnfrishert/libLSDJ/workflows/Build%20and%20Test/badge.svg)

[Little Sound DJ](http://littlesounddj.com) is a wonderful tool that transforms your old gameboy into a music making machine. It has a thriving community of users that pushes their old hardware to its limits, in pursuit of new musical endeavours. It can however be cumbersome to manage songs and sounds outside of the gameboy.

In this light *libLSDJ* was developed, a cross-platform and fast C utility library for interacting with the LSDJ save format (.sav), song files (.lsdsng) and more. The end goal is to deliver *libLSDJ* with a suite of tools for working with everything LSDJ. Currently four such tools are included: *lsdsng-export*, *lsdsng-import*, *lsdj-mono* and *lsdj-wavetable-import*, and requests for other useful tools are very much welcomed.

The core library of *libLSDJ* was rewritten in v2.0.0 to be future-proof against changes in *LSDJ*. This means that the export and import tools should never corrupt your songs, even when *LSDJ* itself adds new features after the latest *libLSDJ* update (until the compression algorithm itself changes). Functions for tooling purposes do need to be added, but that is only a small task.

I'm also proud to say that TommityTom's [RetroPlug](https://github.com/tommitytom/RetroPlug) is built on *libLSDJ* for its save file management. If you're using the tools or the library yourself, let me know, I'm curious to hear who depends on this. :)

The library and tools are open source and freely available to anyone. If you'd like to show your appreciation, please consider buying one of my [albums](https://4ntler.bandcamp.com) or donate money through [PayPal](https://paypal.me/4ntler).

# Tools

## lsdsng-export

*lsdsng-export* is a command-line tool for exporting songs from a .sav to .lsdsng, and querying sav formats about their song content.

    lsdsng-export mymusic.sav|folder

    Options:
      -h, --help            Show the help screen
      -v, --verbose         Verbose output during export
      --noversion           Don't add version numbers to the filename
      -f, --folder          Put every lsdsng in its own folder
      -p, --print           Print a list of all songs in the sav, instead of exporting
      -d, --decimal         Use decimal notation for the version number, instead of hex
      -u, --underscore      Use an underscore for the special lightning bolt character, instead of x
      -o, --output arg      Output folder for the lsdsng's
      -i, --index arg       Single out a given project index to export, 0 or more
      -n, --name arg        Single out a given project by name to export
      -w, --working-memory  Single out the working-memory song to export
      --skip-working        Do not export the song in working-memory when no other projects are given

## lsdsng-import

*lsdsng-import* is a command-line tool for importing one or more songs from .lsdsng into a .sav file.

    lsdsng-import -o output.sav song1.lsgsng song2.lsdsng songs.sav...

    Options:
      -h, --help                Show the help screen
      -v, --verbose             Verbose output during import
      -o, --output arg          The output file (.sav)
      -w, --working-memory arg  The song to put in the working memory

## lsdj-mono

*lsdj-mono* is a command-line tool that transforms any .sav, .lsdsngs or folder containing such files to mono. In essence, it changes all `OL_` and `O_R` commands to `OLR` (leaving `O__` untouched), and sets all instruments to play `LR` as well.

    lsdj-mono mymusic.sav|mymusic.lsdsng ...

    Options:
      -h, --help        Show the help screen
      -v, --verbose     Verbose output during import
      -i, --instrument  Only adjust instruments
      -t, --table       Only adjust tables
      -p, --phrase      Only adjust phrases

## lsdj-wavetable-import

*lsdj-wavetable-import* is a command-line tool that imports *.snt* files (directly containing bytes that represent wavetable data) into your *.lsdsng* files. A repository of *.snt* files can be found over at [https://github.com/psgcabal/lsdjsynths](https://github.com/psgcabal/lsdjsynths).

    lsdj-wavetable-import source.lsdsng wavetables.snt -[s 0-F | i 0-FF]

    Options:
      -h, --help        Show the help screen
      -v, --verbose     Verbose output during import
      -i, --index arg   The wavetable index 00-FF where the wavetable data should be written
      -s, --synth arg   The synth number 0-F where the wavetable data should be written
      -0, --zero        Pad the synth with empty wavetables if the .snt file < 256 bytes
      -f, --force       Force writing the wavetables, even though non-default data may be in them
      -o, --output arg  The output .lsdsng to write to
      -d, --decimal     Is the number for --index or --synth a decimal (instead of hex)?

# System Requirements

The nature of *libLSDJ* as a C library makes it compilable on nearly all common operating systems. Both tools included have been tested on macOS Sierra and Windows 7/10 and seem to be working. DigiPack has also successfully built *libLSDJ* on Arch Linux.

# Download

Precompiled binaries can be found under [releases](https://github.com/stijnfrishert/liblsdj/releases).

# Help out?

If you'd like to help out, please let me know!

Bug reports can be filed in [issues](https://github.com/stijnfrishert/liblsdj/issues). Feature requests can also be added there, appropriately labeled.

Developers that would like to help out are warmly invited to do so. This project is open source for a reason; I know the chiptune scene as a loving and caring, open community, and to me this seems like a good way to give back. I personally have little experience with open sourcing code, and view this as a good opportunity to gain some.

# License

*libLSDJ* and its tools are released under the liberal MIT-license.

*libLSDJ* makes grateful use of the following dependencies:
 - [Catch2](https://github.com/catchorg/Catch2) (Boost Software License 1.0)
 - [ghc::filesystem](https://github.com/gulrak/filesystem) (MIT)
 - [popl](https://github.com/badaix/popl) (MIT)

---

Special thanks for Defense Mechanism (urbster1), .exe (rbong) and Johan Kotlinski (@jkotlinski) for thinking along and helping out where needed.
