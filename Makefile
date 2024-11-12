ifeq ($(PREFIX),)
	PREFIX := /usr/local
endif

CC=cc
CFLAGS=-l

ew: ew.c
	${CC} ew.c -o ew

install: ew
	install -d ${DESTDIR}${PREFIX}/bin/
	cp -f ew ${DESTDIR}${PREFIX}/bin/
