# TLS parser

Include:

	#include <FF/net/tls.h>

Parse TLS Client Hello or Server Hello:

	char data[] = ...;
	uint length = ...;
	fftls tls = {};
	fftls_input(&tls, data, length);

	for (;;) {

		enum FFTLS_R r = fftls_read(&tls);

		switch (r) {
		case FFTLS_RERR:
			//error
			return;

		case FFTLS_RMORE:
			fftls_input(&tls, data, length);
			append_more(data, ...);
			length = ...;
			fftls_input(&tls, data, length);
			continue;

		// handle other FFTLS_R* codes
		case FFTLS_R...:
			// use the appropriate field from 'struct fftls' to get the value
			continue;

		case FFTLS_RDONE:
			return;
		}
	}
