DIRS = pjlib pjsdp pjmedia pjsip

ifdef MINSIZE
MAKE_FLAGS := MINSIZE=1
endif

all clean dep depend distclean doc print realclean:
	for dir in $(DIRS); do \
	   if [ -d $$dir ]; then \
		if make $(MAKE_FLAGS) -C $$dir/build $@; then \
		    true; \
		else \
		    exit 1; \
		fi; \
	   fi \
	done

LIBS = pjlib/lib/libpj.a pjsdp/lib/libpjsdp.a pjmedia/lib/libpjmedia.a \
       pjsip/lib/libpjsip_core.a pjsip/lib/libpjsip_ua.a
BINS = pjsip/bin/pjsua$(EXE) 

size:
	@echo -n 'Date: '
	@date
	@echo
	@for lib in $(LIBS); do \
		echo "$$lib:"; \
		ar tv $$lib | awk '{print $$3 "\t" $$8}' | sort -n; \
		echo -n 'Total: '; \
		ar tv $$lib | awk '{print " + " $$3}' | xargs expr 0; \
		echo; \
	done
	@echo
	@for bin in $(BINS); do \
		echo "size $$bin:"; \
		size $$bin; \
	done

dos2unix:
	for f in `find . | egrep '(mak|h|c|S|s|Makefile)$$'`; do \
		dos2unix "$$f" > dos2unix.tmp; \
		cp dos2unix.tmp "$$f"; \
	done
	rm -f dos2unix.tmp

