#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include "memoria_compartida.h"

// Códigos de color ANSI
#define COLOR_RESET   "\x1b[0m"
#define COLOR_GREEN   "\x1b[32m"
#define COLOR_CYAN    "\x1b[36m"
#define COLOR_YELLOW  "\x1b[33m"

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Uso: %s <identificador_shm> <llave_encriptacion>\n", argv[0]);
        fprintf(stderr, "Ejemplo: %s /mi_shm 42\n", argv[0]);
        return 1;
    }

    const char *shm_name = argv[1];
    unsigned char llave = (unsigned char)atoi(argv[2]);
    
    printf("=== Emisor iniciado ===\n");
    printf("Llave de encriptación: 0x%02X\n\n", llave);

    // Abrir memoria compartida
    int shm_fd = shm_open(shm_name, O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("Error: No se puede abrir la memoria compartida");
        fprintf(stderr, "¿Ejecutó el inicializador primero?\n");
        return 1;
    }

    // Paso 1: Mapear solo la estructura base para leer buffer_size
    size_t base_size = sizeof(shared_mem_t);
    shared_mem_t *shm_temp = mmap(NULL, base_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    if (shm_temp == MAP_FAILED) {
        perror("Error al mapear memoria compartida (temporal)");
        close(shm_fd);
        return 1;
    }

    // Leer el buffer_size
    int buffer_size = shm_temp->buffer_size;
    char filename[MAX_FILENAME];
    strncpy(filename, shm_temp->filename, MAX_FILENAME);
    
    if (buffer_size <= 0 || buffer_size > 10000) {
        fprintf(stderr, "Error: buffer_size inválido (%d)\n", buffer_size);
        munmap(shm_temp, base_size);
        close(shm_fd);
        return 1;
    }

    munmap(shm_temp, base_size);

    // Paso 2: Mapear con el tamaño completo
    size_t shm_size = sizeof(shared_mem_t) + (buffer_size * sizeof(char_info_t));
    shared_mem_t *shm = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    
    if (shm == MAP_FAILED) {
        perror("Error al mapear memoria compartida (completa)");
        close(shm_fd);
        return 1;
    }

    printf("Memoria compartida conectada (buffer: %d caracteres)\n", buffer_size);
    printf("Archivo fuente: %s\n", filename);

    // Registrar este emisor
    sem_wait(&shm->mutex);
    shm->emisores_activos++;
    sem_post(&shm->mutex);

    // Abrir el archivo fuente
    FILE *archivo = fopen(filename, "r");
    if (!archivo) {
        perror("Error al abrir archivo fuente");
        sem_wait(&shm->mutex);
        shm->emisores_activos--;
        sem_post(&shm->mutex);
        munmap(shm, shm_size);
        close(shm_fd);
        return 1;
    }

    printf("\n" COLOR_CYAN "%-10s %-8s %-10s %-20s" COLOR_RESET "\n", 
           "Carácter", "ASCII", "Posición", "Timestamp");
    printf("--------------------------------------------------------\n");

    // Ciclo principal: leer y escribir caracteres
    int char_count = 0;
    int c;
    
    while ((c = fgetc(archivo)) != EOF) {
        // PASO 1: Esperar a que haya un espacio libre
        // Si el buffer está lleno, este proceso se bloqueará aquí
        sem_wait(&shm->espacios_libres);
        
        // PASO 2: Obtener acceso exclusivo a los índices
        sem_wait(&shm->mutex);
        
        // Calcular la posición de escritura en el buffer circular
        int pos = shm->write_index % buffer_size;
        
        // Encriptar el carácter con XOR
        unsigned char encrypted = (unsigned char)c ^ llave;
        
        // Escribir en el buffer
        shm->buffer[pos].valor = encrypted;
        shm->buffer[pos].posicion = shm->write_index;
        shm->buffer[pos].timestamp = time(NULL);
        
        // Avanzar el índice de escritura
        shm->write_index++;
        shm->chars_transferidos++;
        
        // Liberar el mutex
        sem_post(&shm->mutex);
        
        // PASO 3: Señalar que hay un nuevo dato disponible
        sem_post(&shm->espacios_ocupados);
        
        // Mostrar información del carácter escrito
        char display_char = (c >= 32 && c < 127) ? c : '.';
        struct tm *tm_info = localtime(&shm->buffer[pos].timestamp);
        char time_str[20];
        strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);
        
        printf(COLOR_GREEN "'%c'" COLOR_RESET "        %-8d %-10d %s\n", 
               display_char, c, shm->buffer[pos].posicion, time_str);
        
        char_count++;
        
        // Opcional: pequeña pausa para demostración
        // usleep(100000); // 100ms
    }

    fclose(archivo);

    printf("\n" COLOR_YELLOW "Emisor finalizó: %d caracteres escritos" COLOR_RESET "\n", char_count);

    // Desregistrar este emisor
    sem_wait(&shm->mutex);
    shm->emisores_activos--;
    sem_post(&shm->mutex);

    // Limpiar recursos
    munmap(shm, shm_size);
    close(shm_fd);

    return 0;
}