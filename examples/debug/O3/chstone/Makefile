#DIRS = adpcm aes blowfish dfadd dfdiv dfmul dfsin gsm jpeg mips motion sha
DIRS = adpcm aes blowfish dfadd dfmul gsm jpeg mips motion sha

include ../../../Makefile.config

$(TARGETS):
	for dir in $(DIRS); do make $@ -C $$dir; done

backup:
	mkdir -p backup
	for dir in $(DIRS); do cp $$dir/*.v backup; done

.PHONY : backup

cat:
	find . -iname "$(FILENAME)" -exec cat {} \; | sort > chstone_$(FILENAME)
