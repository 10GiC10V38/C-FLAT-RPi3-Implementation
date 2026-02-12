CC = aarch64-linux-gnu-gcc
CFLAGS = -Wall -I$(TEEC_EXPORT)/include -I../../secure-world/host
LDFLAGS = -L$(TEEC_EXPORT)/lib -lteec

test_simple: test_simple.c ../../secure-world/host/cflat_host.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f test_simple
