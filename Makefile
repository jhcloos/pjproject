include build.mak
include build/host-$(HOST_NAME).mak

DIRS = pjlib pjlib-util pjnath third_party pjmedia pjsip pjsip-apps

ifdef MINSIZE
MAKE_FLAGS := MINSIZE=1
endif

all clean dep depend distclean doc print realclean:
	for dir in $(DIRS); do \
		if $(MAKE) $(MAKE_FLAGS) -C $$dir/build $@; then \
		    true; \
		else \
		    exit 1; \
		fi; \
	done

LIBS = 	pjlib/lib/libpj-$(TARGET_NAME).a \
	pjlib-util/lib/libpjlib-util-$(TARGET_NAME).a \
	pjnath/lib/libpjnath-$(TARGET_NAME).a \
	pjmedia/lib/libpjmedia-$(TARGET_NAME).a \
	pjmedia/lib/libpjmedia-codec-$(TARGET_NAME).a \
       	pjsip/lib/libpjsip-$(TARGET_NAME).a \
	pjsip/lib/libpjsip-ua-$(TARGET_NAME).a \
	pjsip/lib/libpjsip-simple-$(TARGET_NAME).a \
	pjsip/lib/libpjsua-$(TARGET_NAME).a
BINS = 	pjsip-apps/bin/pjsua-$(TARGET_NAME)$(HOST_EXE) 

size:
	@echo -n 'Date: '
	@date
	@echo
	@for lib in $(LIBS); do \
		echo "$$lib:"; \
		size -t $$lib | awk '{print $$1 "\t" $$2 "\t" $$3 "\t" $$6}'; \
		echo; \
	done
	@echo
	@for bin in $(BINS); do \
		echo "size $$bin:"; \
		size $$bin; \
	done

#dos2unix:
#	for f in `find . | egrep '(mak|h|c|S|s|Makefile)$$'`; do \
#		dos2unix "$$f" > dos2unix.tmp; \
#		cp dos2unix.tmp "$$f"; \
#	done
#	rm -f dos2unix.tmp

xhdrid:
	for f in `find . | egrep '\.(h|c|S|s|cpp|hpp)$$'`; do \
		echo Processing $$f...; \
		cat $$f | sed 's/.*\$$Author\$$/ */' > /tmp/id; \
		cp /tmp/id $$f; \
	done

