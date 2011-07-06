CC=gcc
CFLAGS=-c -Wall -D__LINUX__ -g
LDFLAGS=-lcurl
SOURCES= test_utils.c CA.c utils.c cloudstorage.c
OBJECTS=$(SOURCES:.c=.o)
EXECUTABLE=test

all: $(SOURCES) $(EXECUTABLE)
	
$(EXECUTABLE): $(OBJECTS)
	$(CC) -o $@  $(OBJECTS) $(LDFLAGS)

.cpp.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm $(EXECUTABLE) $(OBJECTS)


