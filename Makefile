#
# ack - ack replacement written in C
# © 2011-2012 Roman Kraevskiy <rkraevskiy@gmail.com>
#



VERSION = "1.0.beta3"
PREFIX = "/usr"


TARGET="ack"
TARBALLNAME=${TARGET}-${VERSION}

SRC = main.c
OBJ = ${SRC:.c=.o}
LIBS= -lpcre -lpcreposix 
#CFLAGS= -Wall -std=c99 -D_POSIX_SOURCE -D_GNU_SOURCE -D_BSD_SOURCE -DVERSION=\"${VERSION}\" -O0 -pg
#LDFLAGS= -pg
CFLAGS= -Wall -std=c99 -D_POSIX_SOURCE -D_GNU_SOURCE -D_BSD_SOURCE -DVERSION=\"${VERSION}\" -Ofast
LDFLAGS= 
CC=cc

.SUFFIXES: .c .o

all: options ${TARGET}

options:
	@echo  build options:
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"

.c.o:
	@echo CC $<
	@${CC} -c ${CFLAGS} $<


${TARGET}: ${OBJ}
	@echo CC -o $@ ${OBJ} ${LDFLAGS} ${LIBS}
	@${CC} -o $@ ${OBJ} ${LDFLAGS} ${LIBS}

clean:
	@echo cleaning
	@rm -f ${TARGET} ${OBJ}

dist: clean
	@echo creating dist tarball
	@mkdir -p ${TARBALLNAME}
	@cp -R LICENSE Makefile README ${SRC} ${TARBALLNAME}
	@tar -cf -${VERSION}.tar ${TARBALLNAME}
	@gzip ${TARBALLNAME}.tar
	@rm -rf ${TARBALLNAME}

install: all
	@echo installing executable file to ${DESTDIR}${PREFIX}/bin
	@mkdir -p ${DESTDIR}${PREFIX}/bin
	@cp ${TARGET} ${DESTDIR}${PREFIX}/bin
	@chmod 755 ${DESTDIR}${PREFIX}/bin/${TARGET}

uninstall:
	@echo removing executable file from ${DESTDIR}${PREFIX}/bin
	@rm -f ${DESTDIR}${PREFIX}/bin/${TARGET}

.PHONY: all options clean dist install uninstall

