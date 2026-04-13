PROGRAM = rpmsg_sample_client_v2h
CFLAGS += -Wall -O2 -DCFG_CA5X
LINK_LIBS = -lopen_amp -lmetal -pthread

OBJS += main.o
OBJS += helper.o
OBJS += rz_rproc.o
OBJS += platform_info.o

.SUFFIXES: .c .o

.PHONY: all
all: $(PROGRAM)

$(PROGRAM): $(OBJS)
	$(CC) $(LDFLAGS) -o $(PROGRAM) $^ $(LINK_LIBS)

.c.o:
	$(CC) $(CFLAGS) -c $<

.PHONY: clean
clean:
	$(RM) $(PROGRAM) $(OBJS)
