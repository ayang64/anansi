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

dirs   = nsthread nsd nssock nscgi nscp nslog nsperm nsdb

all: 
	@for i in $(dirs); do \
		( cd $$i && $(MAKE) all ) || exit 1; \
	done

install: install-binaries install-doc

install-binaries: all
	for i in bin lib log include modules/tcl servers/server1/pages; do \
		$(MKDIR) $(NAVISERVER)/$$i; \
	done
	for i in include/*.h include/Makefile.global include/Makefile.module; do \
		$(INSTALL_DATA) $$i $(INSTHDR)/; \
	done
	for i in tcl/*.tcl; do \
		$(INSTALL_DATA) $$i $(NAVISERVER)/modules/tcl/; \
	done
	$(INSTALL_DATA) sample-config.tcl $(NAVISERVER)/
	$(INSTALL_SH) install-sh $(INSTBIN)/
	for i in $(dirs); do \
		(cd $$i && $(MAKE) install) || exit 1; \
	done

install-tests:
	$(CP) -r tests $(INSTSRVPAG)

install-doc:
	cd doc && /bin/sh ./install-doc $(NAVISERVER)

test: all
	LD_LIBRARY_PATH="./nsd:./nsthread" ./nsd/nsd -c -d -t tests/test.nscfg all.tcl $(TESTFLAGS) $(TCLTESTARGS)

runtest: all
	LD_LIBRARY_PATH="./nsd:./nsthread" ./nsd/nsd -c -d -t tests/test.nscfg

gdb: all
	@echo "set args -c -d -t tests/test.nscfg all.tcl $(TESTFLAGS) $(TCLTESTARGS)" > gdb.run
	LD_LIBRARY_PATH="./nsd:./nsthread" gdb -x gdb.run ./nsd/nsd
	rm gdb.run

checkexports: all
	@for i in $(dirs); do \
		nm -p $$i/*.so | awk '$$2 ~ /[TDB]/ { print $$3 }' | sort -n | uniq | grep -v '^[Nn]s\|^TclX\|^_'; \
	done

clean:
	@for i in $(dirs); do \
		(cd $$i && $(MAKE) clean) || exit 1; \
	done

distclean: clean
	$(RM) config.status config.log config.cache include/Makefile.global include/Makefile.module include/config.h

.PHONY: all install install-binaries install-doc install-tests clean distclean
