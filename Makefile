CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -pthread -lrt
OUTDIR = out
TARGETS = $(OUTDIR)/inicializador $(OUTDIR)/emisor $(OUTDIR)/receptor $(OUTDIR)/finalizador

all: $(OUTDIR) $(TARGETS)

$(OUTDIR):
	mkdir -p $(OUTDIR)

$(OUTDIR)/inicializador: inicializador.c memoria_compartida.h
	$(CC) $(CFLAGS) -o $(OUTDIR)/inicializador inicializador.c $(LDFLAGS)

$(OUTDIR)/emisor: emisor.c memoria_compartida.h
	$(CC) $(CFLAGS) -o $(OUTDIR)/emisor emisor.c $(LDFLAGS)

$(OUTDIR)/receptor: receptor.c memoria_compartida.h
	$(CC) $(CFLAGS) -o $(OUTDIR)/receptor receptor.c $(LDFLAGS)

$(OUTDIR)/finalizador: finalizador.c memoria_compartida.h
	$(CC) $(CFLAGS) -o $(OUTDIR)/finalizador finalizador.c $(LDFLAGS)

clean:
	rm -f $(TARGETS)
	rm -f /dev/shm/mi_shm*
	rm -f output_receptor.txt

.PHONY: all clean