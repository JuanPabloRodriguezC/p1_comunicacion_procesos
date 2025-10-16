#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include "memoria_compartida.h"

// Códigos de color ANSI
#define COLOR_RESET   "\x1b[0m"
#define COLOR_BLUE    "\x1b[34m"
#define COLOR_MAGENTA "\x1b[35m"
#define COLOR_CYAN    "\x1b[36m"
#define COLOR_YELLOW  "\x1b[33m"

// Variable global para señal de finalización
volatile sig_atomic_t keep_running = 1;

void signal_handler(int signum) {
    (void)signum;
    keep_running = 0;
}

int main(int argc, char* argv[]){
    
    if (argc != 3) {
        fprintf(stderr, "Uso: %s <identificador_shm> <llave_desencriptacion>\n", argv[0]);
        fprintf(stderr, "Ejemplo: %s /mi_shm 42\n", argv[0]);
        return 1;
    }

    const char *shm_name = argv[1];
    unsigned char llave = (unsigned char)atoi(argv[2]);
    
    printf("=== Receptor iniciado ===\n");
    printf("Llave de desencriptación: 0x%02X\n\n", llave);

    // Configurar manejador de señales (Ctrl+C)
    signal(SIGINT, signal_handler);

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

    // Registrar este receptor
    sem_wait(&shm->mutex);
    shm->receptores_activos++;
    sem_post(&shm->mutex);

    // Crear archivo de salida (opcional: puedes cambiar el nombre)
    FILE *output = fopen("output_receptor.txt", "w");
    if (!output) {
        perror("Error al crear archivo de salida");
        sem_wait(&shm->mutex);
        shm->receptores_activos--;
        sem_post(&shm->mutex);
        munmap(shm, shm_size);
        close(shm_fd);
        return 1;
    }

    printf("\n" COLOR_CYAN "%-10s %-8s %-10s %-20s" COLOR_RESET "\n", 
           "Carácter", "ASCII", "Posición", "Timestamp");
    printf("--------------------------------------------------------\n");

    int char_count = 0;

    // CICLO PRINCIPAL DE LECTURA
    // El receptor se ejecuta indefinidamente hasta que:
    // 1. Se reciba una señal (Ctrl+C)
    // 2. El flag 'finalizar' se active (por el finalizador)
    while (keep_running) {
        // PASO 1: Esperar a que haya un dato disponible
        // Si el buffer está vacío, este proceso se bloqueará aquí
        // IMPORTANTE: sem_wait puede ser interrumpido por señales
        if (sem_wait(&shm->espacios_ocupados) == -1) {
            if (errno == EINTR) {
                // Interrumpido por señal, verificar si debemos salir
                break;
            }
            perror("Error en sem_wait");
            break;
        }

        // Verificar flag de finalización (set por el finalizador)
        sem_wait(&shm->mutex);
        int debe_finalizar = shm->finalizar;
        sem_post(&shm->mutex);
        
        if (debe_finalizar) {
            // Devolver el semáforo porque no vamos a leer
            sem_post(&shm->espacios_ocupados);
            break;
        }

        // PASO 2: Obtener acceso exclusivo a los índices
        sem_wait(&shm->mutex);
        
        // Calcular la posición de lectura en el buffer circular
        int pos = shm->read_index % buffer_size;
        
        // Leer el carácter encriptado
        unsigned char encrypted = shm->buffer[pos].valor;
        int posicion_original = shm->buffer[pos].posicion;
        time_t timestamp = shm->buffer[pos].timestamp;
        
        // Avanzar el índice de lectura
        shm->read_index++;
        
        // Liberar el mutex
        sem_post(&shm->mutex);
        
        // PASO 3: Desencriptar el carácter (XOR con la misma llave)
        unsigned char decrypted = encrypted ^ llave;
        
        // PASO 4: Señalar que hay un espacio libre
        sem_post(&shm->espacios_libres);
        
        // Escribir al archivo de salida
        fputc(decrypted, output);
        fflush(output);  // Asegurar que se escriba inmediatamente
        
        // Mostrar información del carácter leído
        char display_char = (decrypted >= 32 && decrypted < 127) ? decrypted : '.';
        struct tm *tm_info = localtime(&timestamp);
        char time_str[20];
        strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);
        
        printf(COLOR_BLUE "'%c'" COLOR_RESET "        %-8d %-10d %s\n", 
               display_char, decrypted, posicion_original, time_str);
        
        char_count++;
    }

    fclose(output);

    printf("\n" COLOR_YELLOW "Receptor finalizó: %d caracteres leídos" COLOR_RESET "\n", char_count);
    printf("Texto guardado en: output_receptor.txt\n");

    // Desregistrar este receptor
    sem_wait(&shm->mutex);
    shm->receptores_activos--;
    sem_post(&shm->mutex);

    // Limpiar recursos
    munmap(shm, shm_size);
    close(shm_fd);

    return 0;
}