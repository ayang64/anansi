#
# The contents of this file are subject to the Mozilla Public License
# Version 1.1 (the "License"); you may not use this file except in
# compliance with the License. You may obtain a copy of the License at
# http://www.mozilla.org/.
#
# Software distributed under the License is distributed on an "AS IS"
# basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
# the License for the specific language governing rights and limitations
# under the License.
#
# The Original Code is AOLserver Code and related documentation
# distributed by AOL.
#
# The Initial Developer of the Original Code is America Online,
# Inc. Portions created by AOL are Copyright (C) 1999 America Online,
# Inc. All Rights Reserved.
#
# Alternatively, the contents of this file may be used under the terms
# of the GNU General Public License (the "GPL"), in which case the
# provisions of GPL are applicable instead of those above.  If you wish
# to allow use of your version of this file only under the terms of the
# GPL and not to allow others to use your version of this file under the
# License, indicate your decision by deleting the provisions above and
# replace them with the notice and other provisions required by the GPL.
# If you do not delete the provisions above, a recipient may use your
# version of this file under either the License or the GPL.
#
#
# $Header$
#

NSBUILD=1
include include/Makefile.global

dirs   = nsthread nsd nssock nscgi nscp nslog nsperm nsdb nsdbtest

# Unix only modules
ifeq (,$(findstring MINGW,$(uname)))
   dirs += nsproxy
endif

distfiles = $(dirs) doc tcl contrib include tests win32 configure m4 \
	Makefile autogen.sh install-sh missing aclocal.m4 configure.in \
	README ChangeLog NEWS sample-config.tcl.in simple-config.tcl \
	nsd-config.tcl index.adp license.terms naviserver.rdf naviserver.rdf.in

all:
	@for i in $(dirs); do \
		( cd $$i && $(MAKE) all ) || exit 1; \
	done

install: install-dirs install-include install-tcl install-modules \
	install-config install-doc install-examples install-notice

install-notice:
	@echo ""
	@echo ""
	@echo "You can now run NaviServer by typing one of the commands below: "
	@echo ""
	@if [ "`whoami`" = "root" ]; then \
	  echo "  Because you are running as root, the server needs unprivileged user to be"; \
	  echo "  specified and permissions on log directory to be setup first:"; \
	  echo ""; \
	  echo "  chown -R nobody $(NAVISERVER)/logs"; \
	  echo ""; \
	  echo "  If you want the server to be run as other user, replace nobody with"; \
	  echo "  your user name and re-run command above before starting the server"; \
	  user="-u nobody"; \
	  chown -R nobody $(NAVISERVER)/logs; \
	fi; \
	echo ""; \
	echo "$(NAVISERVER)/bin/nsd -f $$user -t $(NAVISERVER)/conf/nsd-config.tcl"; \
	echo " or"; \
	echo "$(NAVISERVER)/bin/nsd -f $$user -t $(NAVISERVER)/conf/sample-config.tcl"; \
	echo " or"; \
	echo "$(NAVISERVER)/bin/nsd -f $$user -t $(NAVISERVER)/conf/simple-config.tcl"; \
	echo ""

install-dirs: all
	@for i in bin lib logs include tcl pages conf modules cgi-bin; do \
		$(MKDIR) $(NAVISERVER)/$$i; \
	done

install-config: all
	@for i in nsd-config.tcl sample-config.tcl simple-config.tcl; do \
		$(INSTALL_DATA) $$i $(NAVISERVER)/conf/; \
	done
	@for i in index.adp; do \
		$(INSTALL_DATA) $$i $(NAVISERVER)/pages/; \
	done
	$(INSTALL_SH) install-sh $(INSTBIN)/

install-modules: all
	@for i in $(dirs); do \
		(cd $$i && $(MAKE) install) || exit 1; \
	done

install-tcl: all
	@for i in tcl/*.tcl; do \
		$(INSTALL_DATA) $$i $(NAVISERVER)/tcl/; \
	done

install-include: all
	@for i in include/*.h include/Makefile.global include/Makefile.module; do \
		$(INSTALL_DATA) $$i $(INSTHDR)/; \
	done

install-tests:
	$(CP) -r tests $(INSTSRVPAG)

install-doc:
	@$(MKDIR) $(NAVISERVER)/pages/doc $(NAVISERVER)/pages/doc/files
	@echo Installing html files in $(NAVISERVER)/pages/doc...
	@if test -d doc/html ; then \
	    for i in `find doc/html -maxdepth 1 -name "*.html" -print`; do \
		$(INSTALL_DATA) $$i $(NAVISERVER)/pages/doc; \
	    done ; \
	    for i in `find doc/html -maxdepth 1 -name "*.css" -print`; do \
		$(INSTALL_DATA) $$i $(NAVISERVER)/pages/doc; \
	    done ; \
	fi
	@if test -d doc/html/files ; then \
	    for i in `find doc/html/files -name "*.html" -print`; do \
		$(INSTALL_DATA) $$i $(NAVISERVER)/pages/doc/files; \
	    done ; \
	fi
	@for n in 1 3 n; do \
	    d=$(NAVISERVER)/man/man$$n; \
	    echo Installing nroff files in $$d...; \
	    $(MKDIR) $$d; \
	    if test -d doc/man ; then \
		for i in `find doc/man -name "*.$$n" -print`; do \
		    $(INSTALL_DATA) $$i $$d; \
		done; \
	    fi ; \
	done

install-examples:
	@$(MKDIR) $(NAVISERVER)/pages/examples
	@for i in contrib/examples/*.adp contrib/examples/*.tcl; do \
		$(INSTALL_DATA) $$i $(NAVISERVER)/pages/examples/; \
	done

build-doc:
	@if [ 1 = 1 ]; then \
	    hd=`pwd`; \
	    cd doc/src && $(MKDIR) ../html ../man ; \
	    echo Generating docs from .man pages in `pwd`; \
	    echo Emitting html files to $$hd/doc/html/ ...; \
	    dtplite -o ../html/ -style nsd.css html .; \
	    echo Emitting nroff files to $$hd/doc/man/ ...; \
	    for f in *.man; do \
		dtplite -o ../man/`basename $$f .man`.n nroff $$f; \
	    done; \
	    cd $$hd ; \
	    echo Generating docs done. ; \
	fi

#
# Testing:
#

NS_TEST_CFG		= -u root -c -d -t $(srcdir)/tests/test.nscfg
NS_TEST_ALL		= $(srcdir)/tests/all.tcl $(TCLTESTARGS)
LD_LIBRARY_PATH	= LD_LIBRARY_PATH="./nsd:./nsthread:../nsdb:../nsproxy:$$LD_LIBRARY_PATH"

check: test

test: all
	$(LD_LIBRARY_PATH) ./nsd/nsd $(NS_TEST_CFG) $(NS_TEST_ALL)

runtest: all
	$(LD_LIBRARY_PATH) ./nsd/nsd $(NS_TEST_CFG)

gdbtest: all
	@echo set args $(NS_TEST_CFG) $(NS_TEST_ALL) > gdb.run
	$(LD_LIBRARY_PATH); gdb -x gdb.run ./nsd/nsd
	rm gdb.run

gdbruntest: all
	@echo set args $(NS_TEST_CFG) > gdb.run
	$(LD_LIBRARY_PATH); gdb -x gdb.run ./nsd/nsd
	rm gdb.run

memcheck: all
	$(LD_LIBRARY_PATH) valgrind --tool=memcheck ./nsd/nsd $(NS_TEST_CFG) $(NS_TEST_ALL)



checkexports: all
	@for i in $(dirs); do \
		nm -p $$i/*.so | awk '$$2 ~ /[TDB]/ { print $$3 }' | sort -n | uniq | grep -v '^[Nn]s\|^TclX\|^_'; \
	done

clean:
	@for i in $(dirs); do \
		(cd $$i && $(MAKE) clean) || exit 1; \
	done

clean-bak: clean
	@find . -name '*~' -exec rm "{}" ";"

distclean: clean
	$(RM) config.status config.log config.cache autom4te.cache aclocal.m4 configure \
	include/{Makefile.global,Makefile.module,config.h,config.h.in,stamp-h1} \
	naviserver-$(NS_PATCH_LEVEL).tar.gz sample-config.tcl

dist: clean
	$(RM) naviserver-$(NS_PATCH_LEVEL)
	$(MKDIR) naviserver-$(NS_PATCH_LEVEL)
	$(CP) $(distfiles) naviserver-$(NS_PATCH_LEVEL)
	$(RM) naviserver-$(NS_PATCH_LEVEL)/include/{config.h,nsversion.h,Makefile.global,Makefile.module,stamp-h1}
	tar czf naviserver-$(NS_PATCH_LEVEL).tar.gz naviserver-$(NS_PATCH_LEVEL)
	$(RM) naviserver-$(NS_PATCH_LEVEL)


.PHONY: all install install-binaries install-doc install-tests clean distclean
