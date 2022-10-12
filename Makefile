include Makefile.vars

jakobmenu: jakobmenu.c config.h
	${CC} -o jakobmenu ${CFLAGS} jakobmenu.c ${LDFLAGS}

clean:
	rm -f jakobmenu

distclean: clean
	rm -f Makefile.vars config.h test.c a.out

install: jakobmenu
	install -m 755 jakobmenu ${PREFIX}/bin/jakobmenu
