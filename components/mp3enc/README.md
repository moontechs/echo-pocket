# mp3enc

Wraps the [shine](https://github.com/toots/shine) fixed-point MP3 encoder
(vendored under `shine/`, unmodified except for flattening the include
layout) to convert recorder WAV files to MP3 before Telegram upload.

`shine/` is LGPL-2.1 (see `shine/COPYING`) — statically linked into this
firmware. `wav_to_mp3.c` is the only project-specific glue code.
