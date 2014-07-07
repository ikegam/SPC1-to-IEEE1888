EXEC = SPC1-to-1888
OBJS = SPC1_to_1888.o
LIBPTHREAD = -lpthread
LDLIBS = $(shell pkg-config --libs light1888)
LDFLAGS = 
CFLAGS = $(shell pkg-config --cflags light1888)

# OBJS = IEEE1888UploadAgent.o GEmuNet.o GEmuNet-to-1888.o

all: $(EXEC)

$(EXEC): $(OBJS)
	$(CC) -g -Wall $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS) $(LIBPTHREAD)

clean: 
	-rm -f $(EXEC) *.elf *.gdb *.o *~

romfs:
	$(ROMFSINST) /bin/$(EXEC)

%.o: %.c
	$(CC) -c $(CFLAGS) -o $@ $<

