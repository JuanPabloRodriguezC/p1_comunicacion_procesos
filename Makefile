CC = gcc
CFLAGS = -Wall -Wextra -pthread -lrt -O2
OUTDIR = out
TARGETS = $(OUTDIR)/inicializador $(OUTDIR)/emisor $(OUTDIR)/receptor $(OUTDIR)/finalizador

all: $(OUTDIR) $(TARGETS)

$(OUTDIR):
	mkdir -p $(OUTDIR)

$(OUTDIR)/inicializador: inicializador.c memoria_compartida.h
	$(CC) $(CFLAGS) -o $(OUTDIR)/inicializador inicializador.c

$(OUTDIR)/emisor: emisor.c memoria_compartida.h
	$(CC) $(CFLAGS) -o $(OUTDIR)/emisor emisor.c

$(OUTDIR)/receptor: receptor.c memoria_compartida.h
	$(CC) $(CFLAGS) -o $(OUTDIR)/receptor receptor.c

$(OUTDIR)/finalizador: finalizador.c memoria_compartida.h
	$(CC) $(CFLAGS) -o $(OUTDIR)/finalizador finalizador.c

clean:
	rm -f $(TARGETS)
	rm -f /dev/shm/mi_shm*
	rm -f output_receptor.txt

.PHONY: all clean