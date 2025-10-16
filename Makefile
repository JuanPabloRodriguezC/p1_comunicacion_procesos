CC = gcc
CFLAGS = -Wall -Wextra -pthread -lrt -O2
TARGETS = inicializador emisor receptor finalizador

all: $(TARGETS)

inicializador: inicializador.c memoria_compartida.h
	$(CC) $(CFLAGS) -o inicializador inicializador.c

emisor: emisor.c memoria_compartida.h
	$(CC) $(CFLAGS) -o emisor emisor.c

receptor: receptor.c memoria_compartida.h
	$(CC) $(CFLAGS) -o receptor receptor.c

finalizador: finalizador.c memoria_compartida.h
	$(CC) $(CFLAGS) -o finalizador finalizador.c

clean:
	rm -f $(TARGETS)
	rm -f /dev/shm/mi_shm*
	rm -f output_receptor.txt

.PHONY: all clean