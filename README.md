---------------
FAST-FORWARD (FF) LIBRARY
---------------

FF library contains a huge amount of useful functionality helping to develop applications in C and C++ faster.  It boosts development speed and also encourages developers to make the right design choices, splitting their project code into an application code and a bunch of service functions that can be reused in any other project.

FF is built on top of FFOS which provides cross-platform abilities.  FF is written with all 3 aspects in mind:

* Performance.
One of the key aspects of the library: it is designed to run 24/7 in server environment.

* Convenience.
The interfaces are easy to use while trying to provide as much control to user as possible.

* Code complexity (small resulting code size, readability).
FF can be quite easily extended, modified or rewritten block by block.

FF is absolutely free, which means that you can ship it standalone, bundle it inside your software, copy and modify it.

## Documentation

Documentation can be found in `doc/index.md`.  It provides a quick-start guidance on how to use an interface.  For advanced features you need to open `.h` files and look through the available functions.  The most of the functions are briefly documented inline in header files.  Many functions have test use-cases which are located in `test/` directory, so you can look for an example there.

--------
FEATURES
--------

* Base - `FF/`
	* string - `FF/string.h`
	* array - `FF/array.h`
	* linked-list - `FF/chain.h`, `FF/list.h`
	* red-black tree - `FF/rbtree.h`
	* hash table - `FF/hashtab.h`
	* operations with bits - `FF/bitops.h`
	* ring buffer - `FF/ring.h`
	* date and time functions - `FF/time.h`

* System - `FF/sys/`
	* filesystem path functions - `FF/path.h`
	* filesystem directory functions - `FF/sys/dir.h`
	* user task queue - `FF/sys/taskqueue.h`
	* timer queue - `FF/sys/timer-queue.h`
	* file mapping - `FF/sys/filemap.h`
	* send file - `FF/sys/sendfile.h`
	* Windows registry functions - `FF/sys/wreg.h`
	* Windows object handler - `FF/sys/wohandler.h`

* Network - `FF/net/`
	* URL parser - `FF/net/url.h`
	* IPv4/IPv6 address conversion functions - `FF/net/url.h`
	* Ethernet, IP, ARP, ICMP, TCP, UDP packets - `FF/net/proto.h`
	* HTTP - `FF/net/http.h`
	* HTTP client interface - `FF/net/http-client.h`
	* DNS - `FF/net/dns.h`
	* Asynchronous DNS resolver - `FF/net/dns-client.h`
	* SSL (libssl, libcrypto) - `FF/net/ssl.h`
	* TLS reader - `FF/net/tls.h`
	* WebSocket reader/writer - `FF/net/websocket.h`
	* Intel DPDK wrapper - `FF/net/dpdk.h`

* Data - `FF/data/`
	* JSON reader/writer  - `FF/data/json.h`
	* configuration file reader/writer  - `FF/data/conf.h`
	* command-line arguments parser  - `FF/data/psarg.h`
	* deserialization of structured data using a predefined scheme  - `FF/data/parse.h`
	* UTF-8 decode/encode  - `FF/data/utf8.h`
	* CRC32 - `FF/crc.h`

* Compression - `FF/pack/`
	* deflate (libz) - `FF/pack/gz.h`
	* LZMA (liblzma) - `FF/pack/7z.h`

* Packing - `FF/pack/`
	* .xz (read) - `FF/pack/xz.h`
	* .gz (read/write) - `FF/pack/gz.h`
	* .zip (read/write) - `FF/pack/zip.h`
	* .7z (read) - `FF/pack/7z.h`
	* .tar (read/write) - `FF/pack/tar.h`
	* .iso (read/write) - `FF/pack/iso.h`

* Picture - `FF/pic/`
	* BMP - `FF/pic/bmp.h`
	* JPEG (libjpeg) - `FF/pic/jpeg.h`
	* PNG (libpng) - `FF/pic/png.h`

* GUI - `FF/gui/`
	* GUI loader - `FF/gui/loader.h`
	* Windows API GUI - `FF/gui/winapi.h`
	* GTK+ GUI - `FF/gui-gtk/gtk.h`

* Database - `FF/db/`
	* SQLite (libsqlite) - `FF/db/sqlite.h`
	* PostgreSQL (libpq) - `FF/db/postgre.h`

### Multimedia

* Audio container - `FF/aformat/`
	* .aac (read) - `FF/aformat/aac-adts.h`
	* .flac (write) - `FF/aformat/flac.h`
	* .mp3 (read/write) - `FF/aformat/mp3.h`
	* .mpc (read) - `FF/aformat/mpc.h`
	* .wav (read/write) - `FF/aformat/wav.h`

* Multimedia container - `FF/mformat/`
	* .avi (read) - `FF/mformat/avi.h`
	* .mkv (read) - `FF/mformat/mkv.h`
	* .mp4 (read/write) - `FF/mformat/mp4.h`
	* .ogg (read/write) - `FF/mformat/ogg.h`

* Multimedia meta - `FF/mtags/`
	* APE tag - `FF/mtags/apetag.h`
	* ID3v1, ID3v2 - `FF/mtags/id3.h`
	* Vorbis comments - `FF/mtags/vorbistag.h`

* Multimedia playlist - `FF/data/`
	* .cue (read) - `FF/data/cue.h`
	* .m3u (read/write) - `FF/data/m3u.h`
	* .pls (read) - `FF/data/pls.h`

* Audio I/O - `FF/adev/`
	* Direct Sound playback/capture - `FF/adev/dsound.h`
	* WASAPI playback/capture - `FF/adev/wasapi.h`
	* ALSA playback/capture - `FF/adev/alsa.h`
	* Pulse Audio playback - `FF/adev/pulse.h`
	* OSS playback - `FF/adev/oss.h`

* Audio - `FF/audio/`
	* ICY - `FF/audio/icy.h`
	* PCM operations: mix, convert, gain/attenuate - `FF/audio/pcm.h`
	* sample rate conversion (libsoxr) - `FF/audio/soxr.h`
	* APE (libMAC) - `FF/audio/ape.h`
	* AAC (libfdk-aac) - `FF/audio/aac.h`
	* Vorbis (libvorbis) - `FF/audio/vorbis.h`
	* Opus (libopus) - `FF/audio/opus.h`
	* MPEG (libmpg123, libmp3lame) - `FF/audio/mpeg.h`
	* Musepack (libmpc) - `FF/audio/musepack.h`
	* FLAC (libflac) - `FF/audio/flac.h`
	* ALAC (libalac) - `FF/audio/alac.h`
	* WavPack (libwavpack) - `FF/audio/wavpack.h`


--------
LICENSE
--------

The code provided here is free for use in open-source and proprietary projects.

You may distribute, redistribute, modify the whole code or the parts of it, just keep the original copyright statement inside the files.

--------

Simon Zolin
