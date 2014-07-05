EXEC = SPC1-to-1888
OBJS = SPC1_to_1888.o ieee1888_XMLgenerator.o ieee1888_XMLparser.o ieee1888_server.o ieee1888_client.o ieee1888_object_factory.o ieee1888_util.o
LIBPTHREAD = -lpthread

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

