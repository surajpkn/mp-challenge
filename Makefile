CFLAGS += -I/usr/local/include/json-c -g
LDFLAGS += -L/usr/local/lib -ljson-c -lcurl

all:
	gcc $(CFLAGS) machinepark.c -o machinepark $(LDFLAGS)

clean:
	rm -rf machinepark
