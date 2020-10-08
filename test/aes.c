/** ff: aes-ff.h tester
2020, Simon Zolin
*/

#include <FFOS/test.h>
#include <aes/aes-ff.h>

void test_aes()
{
	aes_ctx e, d;
	byte iv[16];
	ffmem_copy(iv, "1234567890123456", 16);
	byte enc[16];
	byte dec[16];

	const char *key = "1234567890123456";
	x(0 == aes_encrypt_init(&e, (byte*)key, 16, AES_CFB));
	x(0 == aes_decrypt_init(&d, (byte*)key, 16, AES_CFB));

	const char *data = "1234567890123456";
	x(0 == aes_encrypt_chunk(&e, (byte*)data, enc, 16, iv));

	ffmem_copy(iv, "1234567890123456", 16);
	x(0 == aes_decrypt_chunk(&d, enc, dec, 16, iv));
	x(!ffmem_cmp(dec, data, 16));
}

int main()
{
	test_aes();
	return 0;
}
