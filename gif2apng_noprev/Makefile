PACKAGE    = gif2apng
CC         = gcc
SRC_DIRS   = . 7z zopfli
CFLAGS     = -Wall -pedantic
CFLAGS_OPT = -O2
CFLAGS_7Z  = -Wno-sign-compare -Wno-reorder -Wno-maybe-uninitialized -Wno-parentheses
LDFLAGS    = -s
LIBS       = -lstdc++ -lm -lz

INCUDE_DIRS := $(addprefix -I./, $(SRC_DIRS))
OBJ_DIRS := $(addprefix obj/, $(SRC_DIRS))
OBJECTS := $(addprefix obj/, $(wildcard $(addsuffix /*.c*, $(SRC_DIRS))))
OBJECTS := $(OBJECTS:.c=.o)
OBJECTS := $(OBJECTS:.cc=.o)
OBJECTS := $(OBJECTS:.cpp=.o)

all : $(PACKAGE)

$(PACKAGE) : objdirs $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJECTS) $(LIBS)

objdirs :
	mkdir -p $(OBJ_DIRS)

obj/%.o : %.cpp
	$(CC) $(CFLAGS) -o $@ -c $< $(INCUDE_DIRS) $(CFLAGS_OPT)

obj/%.o : %.c
	$(CC) $(CFLAGS) -o $@ -c $< $(INCUDE_DIRS) $(CFLAGS_OPT)

obj/%.o : %.cc
	$(CC) $(CFLAGS) -o $@ -c $< $(INCUDE_DIRS) $(CFLAGS_OPT) $(CFLAGS_7Z)

.PHONY : clean

clean :
	rm -rf $(PACKAGE) obj
