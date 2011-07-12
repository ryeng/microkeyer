microkeyer: microkeyer.c microkeyer.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -o microkeyer microkeyer.c

.PHONY: clean

clean:
	-$(RM) microkeyer