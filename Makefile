include Makefile.vars

jakobmenu: jakobmenu.c config.h
	${CC} -o jakobmenu ${CFLAGS} jakobmenu.c ${LDFLAGS}

clean:
	rm -f jakobmenu

distclean: clean
	rm -f Makefile.vars config.h test.c a.out

install: jakobmenu
	install -D -m 755 jakobmenu "${PREFIX}/bin/jakobmenu"
	install -D -m 644 jakobmenu.conf "${PREFIX}/share/jakobmenu/jakobmenu.conf"
	install -D -m 644 README.md "${PREFIX}/share/doc/jakobmenu/README.md"

uninstall:
	rm -f "${PREFIX}/bin/jakobmenu"
	rm -f "${PREFIX}/share/jakobmenu/jakobmenu.conf"
	rm -f "${PREFIX}/share/doc/jakobmenu/README.md"
