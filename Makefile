include Makefile.vars
DISTFOLDER = jakobmenu-${VERSION}

jakobmenu: jakobmenu.c config.h
	${CC} -o jakobmenu ${CFLAGS} jakobmenu.c ${LDFLAGS}

clean:
	rm -f jakobmenu

distclean: clean
	rm -rf Makefile.vars config.h test.c a.out *.tgz ${DISTFOLDER}

dist: jakobmenu
	mkdir -p ${DISTFOLDER}
	install -D -m 644 jakobmenu.conf ${DISTFOLDER}/jakobmenu.conf
	install -D -m 644 README.md ${DISTFOLDER}/README.md
	install -D -m 644 jakobmenu.c ${DISTFOLDER}/jakobmenu.c
	install -D -m 644 config.h ${DISTFOLDER}/config.h
	install -D -m 755 configure.pl ${DISTFOLDER}/configure.pl
	install -D -m 644 Makefile ${DISTFOLDER}/Makefile
	install -D -m 644 Makefile.vars ${DISTFOLDER}/Makefile.vars
	install -D -m 644 LICENSE ${DISTFOLDER}/LICENSE
	tar cvzf ${DISTFOLDER}.tgz ${DISTFOLDER}

install: jakobmenu
	install -D -m 755 jakobmenu "${PREFIX}/bin/jakobmenu"
	install -D -m 644 jakobmenu.conf "${PREFIX}/share/jakobmenu/jakobmenu.conf"
	install -D -m 644 README.md "${PREFIX}/share/doc/jakobmenu/README.md"

uninstall:
	rm -f "${PREFIX}/bin/jakobmenu"
	rm -f "${PREFIX}/share/jakobmenu/jakobmenu.conf"
	rmdir "${PREFIX}/share/jakobmenu" || true
	rm -f "${PREFIX}/share/doc/jakobmenu/README.md"
	rmdir "${PREFIX}/share/doc/jakobmenu" || true
