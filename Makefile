TARGET = Lamplighter
OBJS = src/main.o

CFLAGS = -O2 -G0 -Wall
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

LIBS = -lpspgum -lpspgu -lpspdebug -lm

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = Lamplighter of Willowmere

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
