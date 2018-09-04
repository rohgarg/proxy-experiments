FILE=proxy.c
CFLAGS=-g3 -O0
PROXY_LINKER_FLAGS=-Wl,-Ttext-segment -Wl,0x800000 \
                   -Wl,-Trodata-segment -Wl,0xb00000 \
                   -Wl,-Tdata -Wl,0xd00000

build: copy-bits proxy

# This could be compiled -static or not, as we please.
copy-bits: copy-bits.c
	gcc ${CFLAGS} -o $@ $<

proxy: lastlib.o ${FILE} libproxy.a sbrk.o #  lastlib.o
	gcc -static ${CFLAGS} ${PROXY_LINKER_FLAGS} -o $@ $^ -lmpi -lrt -lpthread -lc

sbrk.o: sbrk.c
	gcc ${CFLAGS} -o $@ -c $<

lastlib.o: lastlib.c
	gcc ${CFLAGS} -o $@ -c $<

libproxy.a: libproxy.o
	ar cr $@ $<
libproxy.o: libproxy.c
	mpicc -o $@ -c $<

run: a.out
	./a.out

vi vim: ${FILE}
	vim $<
touch: ${FILE}
	$@ $<
gdb: ${basename proxy.c ${FILE}}
	$@ $<

clean:
	rm -f a.out proxy libproxy.{o,a} copy-bits sbrk.o lastlib.o

distclean: clean

dist: distclean
	dir=`basename $$PWD` && cd .. && tar czvf $$dir.tgz ./$$dir
	dir=`basename $$PWD` && ls -l ../$$dir.tgz
