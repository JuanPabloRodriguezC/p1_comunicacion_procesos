CC = gcc
CFLAGS = -Wall -Wextra -pthread -lrt
TARGETS = initializer emisor receptor finalizer

all: $(TARGETS)

inicializador: inicializador.c
	$(CC) $(CFLAGS) -o initializer inicializador.c

emisor: emisor.c
	$(CC) $(CFLAGS) -o emisor emisor.c

receptor: receptor.c
	$(CC) $(CFLAGS) -o receptor receptor.c

finalizador: finalizador.c
	$(CC) $(CFLAGS) -o finalizador finalizador.c

clean:
	rm -f $(TARGETS)
	rm -f /dev/shm/mi_shm*

.PHONY: all clean