SBIN_DIR=sbin
PROGRAM = $(SBIN_DIR)/sbin/shell  \
	$(SBIN_DIR)/sbin/help

EXTRA_CLEAN += $(PROGRAM)

$(SBIN_DIR)/sbin/%: $(SBIN_DIR)/%.c lib/libewok.a
	$(CC) $(CFLAGS) -c -o $@.o $<
	$(LD) -Ttext=100 $@.o lib/libewok.a -o $@
	rm -f $@.o
