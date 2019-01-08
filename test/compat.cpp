/** Test .h files compatibility with C++.
Copyright (c) 2018 Simon Zolin
*/

#include <FFOS/asyncio.h>
#include <FFOS/atomic.h>
#include <FFOS/cpu.h>
#include <FFOS/dir.h>
#include <FFOS/error.h>
#include <FFOS/file.h>
#include <FFOS/mem.h>
#include <FFOS/number.h>
#include <FFOS/process.h>
#include <FFOS/queue.h>
#include <FFOS/random.h>
#include <FFOS/sig.h>
#include <FFOS/socket.h>
#include <FFOS/string.h>
#include <FFOS/test.h>
#include <FFOS/thread.h>
#include <FFOS/time.h>
#include <FFOS/timer.h>
#include <FFOS/types.h>

#if defined FF_LINUX && 0
#include <FF/adev/alsa.h>
#include <FF/adev/pulse.h>

#elif defined FF_BSD && 0
#include <FF/adev/oss.h>

#elif defined FF_WIN
#include <FF/adev/dsound.h>
#include <FF/adev/wasapi.h>

#include <FF/sys/wohandler.h>
#include <FF/sys/wreg.h>

#include <FF/gui/loader.h>
#include <FF/gui/winapi.h>
#endif

#include <FF/aformat/aac-adts.h>
#include <FF/aformat/flac.h>
#include <FF/aformat/mp3.h>
#include <FF/aformat/mpc.h>
#include <FF/aformat/wav.h>
#include <FF/audio/aac.h>
#include <FF/audio/alac.h>
#include <FF/audio/ape.h>
#include <FF/audio/flac.h>
#include <FF/audio/icy.h>
#include <FF/audio/mp3lame.h>
#include <FF/audio/mpeg.h>
#include <FF/audio/musepack.h>
#include <FF/audio/opus.h>
#include <FF/audio/pcm.h>
#include <FF/audio/soxr.h>
#include <FF/audio/vorbis.h>
#include <FF/audio/wavpack.h>
#include <FF/mformat/avi.h>
#include <FF/mformat/mkv.h>
#include <FF/mformat/mp4.h>
#include <FF/mformat/ogg.h>
#include <FF/mtags/apetag.h>
#include <FF/mtags/id3.h>
#include <FF/mtags/mmtag.h>
#include <FF/mtags/vorbistag.h>

#include <FF/db/sqlite.h>
#include <FF/db/db.h>

#include <FF/pack/7z.h>
#include <FF/pack/gz.h>
#include <FF/pack/iso.h>
#include <FF/pack/tar.h>
#include <FF/pack/xz.h>
#include <FF/pack/zip.h>

#include <FF/pic/bmp.h>
#include <FF/pic/jpeg.h>
#include <FF/pic/pic.h>
#include <FF/pic/png.h>

#include <FF/array.h>
#include <FF/bitops.h>
#include <FF/chain.h>
#include <FF/crc.h>
#include <FF/data/conf.h>
#include <FF/data/cue.h>
#include <FF/data/json.h>
#include <FF/data/m3u.h>
#include <FF/data/parse.h>
#include <FF/data/pls.h>
#include <FF/data/psarg.h>
#include <FF/data/utf8.h>
#include <FF/data/xml.h>
#include <FF/hashtab.h>
#include <FF/list.h>
#include <FF/net/dns.h>
#include <FF/net/http.h>
#include <FF/net/proto.h>
#include <FF/net/ssl.h>
#include <FF/net/url.h>
#include <FF/number.h>
#include <FF/path.h>
#include <FF/rbtree.h>
#include <FF/ring.h>
#include <FF/string.h>
#include <FF/sys/dir.h>
#include <FF/sys/filemap.h>
#include <FF/sys/sendfile.h>
#include <FF/sys/taskqueue.h>
#include <FF/sys/timer-queue.h>
#include <FF/time.h>
