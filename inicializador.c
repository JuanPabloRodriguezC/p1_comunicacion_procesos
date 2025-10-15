// initializer.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <semaphore.h>
#include <errno.h>
#include "memoria_compartida.h"

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Uso: %s <identificador_shm> <tamaño_buffer> <archivo_fuente>\n", argv[0]);
        fprintf(stderr, "Ejemplo: %s /mi_shm 10 input.txt\n", argv[0]);
        return 1;
    }

    const char *shm_name = argv[1];
    
    // Validar y convertir el tamaño del buffer
    char *endptr;
    long buffer_size = strtol(argv[2], &endptr, 10);
    if (*endptr != '\0' || buffer_size <= 0 || buffer_size > 10000) {
        fprintf(stderr, "Error: El tamaño del buffer debe ser un entero positivo (1-10000)\n");
        return 1;
    }

    const char *filename = argv[3];
    
    // Verificar que el archivo existe
    FILE *test_file = fopen(filename, "r");
    if (!test_file) {
        fprintf(stderr, "Error: No se puede abrir el archivo '%s': %s\n", 
                filename, strerror(errno));
        return 1;
    }
    fclose(test_file);

    // Calcular tamaño total de la memoria compartida
    size_t shm_size = sizeof(shared_mem_t) + (buffer_size * sizeof(char_info_t));
    
    printf("=== Inicializador de Memoria Compartida ===\n");
    printf("Identificador: %s\n", shm_name);
    printf("Tamaño del buffer: %ld caracteres\n", buffer_size);
    printf("Archivo fuente: %s\n", filename);
    printf("Tamaño total de memoria: %zu bytes\n", shm_size);
    printf("\n");

    // Eliminar memoria compartida previa si existe
    shm_unlink(shm_name);

    // Crear memoria compartida
    int shm_fd = shm_open(shm_name, O_CREAT | O_RDWR | O_EXCL, 0666);
    if (shm_fd == -1) {
        perror("Error al crear memoria compartida");
        return 1;
    }

    // Establecer el tamaño
    if (ftruncate(shm_fd, shm_size) == -1) {
        perror("Error al establecer tamaño de memoria compartida");
        close(shm_fd);
        shm_unlink(shm_name);
        return 1;
    }

    // Mapear la memoria compartida
    shared_mem_t *shm = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    
    if (shm == MAP_FAILED) {
        perror("Error al mapear memoria compartida");
        close(shm_fd);
        shm_unlink(shm_name);
        return 1;
    }

    // Inicializar todos los campos a cero
    memset(shm, 0, shm_size);

    // Inicializar semáforos
    // CLAVE: espacios_libres empieza en buffer_size (todos los espacios están libres)
    if (sem_init(&shm->espacios_libres, 1, buffer_size) == -1) {
        perror("Error al inicializar espacios_libres");
        munmap(shm, shm_size);
        close(shm_fd);
        shm_unlink(shm_name);
        return 1;
    }

    // CLAVE: espacios_ocupados empieza en 0 (no hay datos para leer)
    if (sem_init(&shm->espacios_ocupados, 1, 0) == -1) {
        perror("Error al inicializar espacios_ocupados");
        sem_destroy(&shm->espacios_libres);
        munmap(shm, shm_size);
        close(shm_fd);
        shm_unlink(shm_name);
        return 1;
    }

    // mutex para proteger secciones críticas
    if (sem_init(&shm->mutex, 1, 1) == -1) {
        perror("Error al inicializar mutex");
        sem_destroy(&shm->espacios_libres);
        sem_destroy(&shm->espacios_ocupados);
        munmap(shm, shm_size);
        close(shm_fd);
        shm_unlink(shm_name);
        return 1;
    }

    printf("✓ Semáforos configurados\n");

    // Inicializar estructura de datos compartidos
    strncpy(shm->filename, filename, MAX_FILENAME - 1);
    shm->filename[MAX_FILENAME - 1] = '\0';
    shm->buffer_size = (int)buffer_size;
    shm->write_index = 0;
    shm->read_index = 0;
    shm->chars_transferidos = 0;
    shm->emisores_activos = 0;
    shm->receptores_activos = 0;
    shm->finalizar = 0;

    printf("✓ Memoria compartida inicializada exitosamente\n");

    // Inicializar el buffer circular
    for (int i = 0; i < buffer_size; i++) {
        shm->buffer[i].valor = 0;
        shm->buffer[i].posicion = -1;
        shm->buffer[i].timestamp = 0;
    }
    
    printf("✓ Buffer circular listo\n");
    printf("\nPuede ejecutar los procesos emisor y receptor ahora.\n");

    // Limpiar recursos
    munmap(shm, shm_size);
    close(shm_fd);

    return 0;
}