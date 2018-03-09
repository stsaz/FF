---------------
FAST-FORWARD (FF) LIBRARY
---------------

FF library contains some useful functionality helping to develop applications in C and C++.

FF is built on top of FFOS which provides cross-platform abilities.  Performance is one of the key aspects of the library: it is designed to run 24/7 in server environment.

--------
FEATURES
--------

* Base - `FF/`
	* string
	* array
	* linked-list
	* red-black tree
	* hash table
	* operations with bits

* System - `FF/sys/`
	* filesystem path functions
	* user task queue
	* timer queue
	* file mapping

* Network - `FF/net/`
	* URL parser
	* IPv4/IPv6 address conversion functions
	* HTTP
	* DNS
	* SSL (libssl, libcrypto)

* Data - `FF/data/`
	* JSON: parse and serialize
	* configuration parser
	* command-line arguments parser
	* deserialization of structured data using a predefined scheme
	* UTF-8 decode/encode
	* CRC32

* Compression - `FF/pack/`
	* deflate (libz)
	* LZMA (liblzma)

* Packing - `FF/pack/`
	* .xz (r)
	* .gz (rw)
	* .zip (rw)
	* .7z (r)
	* .tar (rw)
	* .iso (r)

* Picture - `FF/pic/`
	* BMP
	* JPEG (libjpeg)
	* PNG (libpng)

* GUI - `FF/gui/`
	* GUI loader
	* Windows API GUI

* Database - `FF/db/`
	* SQLite (libsqlite)

### Multimedia

* Audio container - `FF/audio/`
	* .aac (r)
	* .flac (w)
	* .mp3 (rw)
	* .mpc (r)
	* .wav (rw)

* Multimedia container - `FF/mformat/`
	* .avi (r)
	* .mkv (r)
	* .mp4 (rw)
	* .ogg (rw)

* Multimedia meta - `FF/mtags/`
	* APE tag
	* ID3v1
	* ID3v2
	* Vorbis comments

* Multimedia playlist - `FF/data/`
	* .cue (r)
	* .m3u (rw)
	* .pls (r)

* Audio I/O - `FF/adev/`
	* Direct Sound playback/capture
	* WASAPI playback/capture
	* ALSA playback/capture
	* Pulse Audio playback
	* OSS playback

* Audio - `FF/aformat/`, `FF/audio/`
	* ICY
	* PCM operations: mix, convert, gain/attenuate
	* sample rate conversion (libsoxr)
	* APE (libMAC)
	* AAC (libfdk-aac)
	* Vorbis (libvorbis)
	* Opus (libopus)
	* MPEG (libmpg123, libmp3lame)
	* Musepack (libmpc)
	* FLAC (libflac)
	* ALAC (libalac)
	* WavPack (libwavpack)


--------
LICENSE
--------

The code provided here is free for use in open-source and proprietary projects.

You may distribute, redistribute, modify the whole code or the parts of it, just keep the original copyright statement inside the files.

--------

Simon Zolin
