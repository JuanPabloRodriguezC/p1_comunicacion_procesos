// finalizador.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include "memoria_compartida.h"

// Variable global para manejar la señal
volatile sig_atomic_t signal_received = 0;
shared_mem_t *global_shm = NULL;
size_t global_shm_size = 0;

void signal_handler(int signum) {
    printf("\n" COLOR_YELLOW "Señal recibida (%d). Iniciando finalización...\n" COLOR_RESET, signum);
    signal_received = 1;
}

void print_separator(void) {
    printf(COLOR_CYAN "========================================" COLOR_RESET "\n");
}

void print_statistics(shared_mem_t *shm) {
    print_separator();
    printf(COLOR_BOLD COLOR_CYAN "    ESTADÍSTICAS FINALES\n" COLOR_RESET);
    print_separator();
    
    printf("\n" COLOR_GREEN "Transferencia de datos:\n" COLOR_RESET);
    printf("Total de caracteres transferidos: " COLOR_YELLOW "%d\n" COLOR_RESET, 
           shm->chars_transferidos);
    
    // Calcular caracteres en memoria (diferencia entre escritos y leídos)
    int chars_en_memoria = shm->write_index - shm->read_index;
    if (chars_en_memoria < 0) chars_en_memoria = 0;
    
    printf("Caracteres en memoria compartida: " COLOR_YELLOW "%d\n" COLOR_RESET, 
           chars_en_memoria);
    printf("Tamaño del buffer: " COLOR_YELLOW "%d\n" COLOR_RESET, 
           shm->buffer_size);
    
    printf("\n" COLOR_GREEN "Procesos:\n" COLOR_RESET);
    printf("Emisores activos: " COLOR_YELLOW "%d\n" COLOR_RESET, 
           shm->emisores_activos);
    printf("Receptores activos: " COLOR_YELLOW "%d\n" COLOR_RESET, 
           shm->receptores_activos);
    
    printf("\n" COLOR_GREEN "Uso de memoria:\n" COLOR_RESET);
    size_t memoria_utilizada = sizeof(shared_mem_t) + 
                               (shm->buffer_size * sizeof(char_info_t));
    printf("Memoria total utilizada: " COLOR_YELLOW "%zu bytes\n" COLOR_RESET, 
           memoria_utilizada);
    printf(" Memoria de control: " COLOR_YELLOW "%zu bytes\n" COLOR_RESET, 
           sizeof(shared_mem_t));
    printf("Memoria del buffer: " COLOR_YELLOW "%zu bytes\n" COLOR_RESET, 
           shm->buffer_size * sizeof(char_info_t));
    printf("Archivo fuente: " COLOR_YELLOW "%s\n" COLOR_RESET, 
           shm->filename);
    print_separator();
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <identificador_shm>\n", argv[0]);
        fprintf(stderr, "Ejemplo: %s /mi_shm\n", argv[0]);
        return 1;
    }

    const char *shm_name = argv[1];
    
    printf(COLOR_BOLD COLOR_RED "=== FINALIZADOR INICIADO ===\n" COLOR_RESET);
    printf("Identificador de memoria compartida: %s\n", shm_name);
    printf("\n" COLOR_YELLOW "Presione Ctrl+C para finalizar todos los procesos.\n" COLOR_RESET);
    print_separator();

    // Configurar manejadores de señales
    signal(SIGINT, signal_handler);   // Ctrl+C
    signal(SIGTERM, signal_handler);  // kill
    signal(SIGUSR1, signal_handler);  // Señal personalizada

    // Abrir memoria compartida
    int shm_fd = shm_open(shm_name, O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("Error: No se puede abrir la memoria compartida");
        fprintf(stderr, "Ejecutar inicializador primero\n");
        return 1;
    }
    size_t base_size = sizeof(shared_mem_t);
    shared_mem_t *shm_temp = mmap(NULL, base_size, PROT_READ | PROT_WRITE, 
                                   MAP_SHARED, shm_fd, 0);

    if (shm_temp == MAP_FAILED) {
        perror("Error al mapear memoria compartida");
        close(shm_fd);
        return 1;
    }

    // Leer el buffer_size
    int buffer_size = shm_temp->buffer_size;

    munmap(shm_temp, base_size);

    // Inicializar de nuevo con tamano correcto
    size_t shm_size = sizeof(shared_mem_t) + (buffer_size * sizeof(char_info_t));
    shared_mem_t *shm = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, 
                             MAP_SHARED, shm_fd, 0);
    
    if (shm == MAP_FAILED) {
        perror("Error al mapear memoria compartida");
        close(shm_fd);
        return 1;
    }

    global_shm = shm;
    global_shm_size = shm_size;

    printf(COLOR_GREEN "Conectado a la memoria compartida\n" COLOR_RESET);
    printf("Buffer size: %d bytes\n", buffer_size);
    printf("\n" COLOR_CYAN "Esperando señal de finalización...\n" COLOR_RESET);

    while (!signal_received) {
        sleep(1);
    }

    printf("\n" COLOR_RED "Iniciando secuencia de finalización...\n" COLOR_RESET);

    //Activar flag de finalización
    sem_wait(&shm->mutex);
    shm->finalizar = 1;
    int emisores_activos = shm->emisores_activos;
    int receptores_activos = shm->receptores_activos;
    sem_post(&shm->mutex);

    printf(COLOR_YELLOW "Flag de finalización activado\n" COLOR_RESET);
    printf("Emisores activos detectados: %d\n", emisores_activos);
    printf("Receptores activos detectados: %d\n", receptores_activos);
    printf(COLOR_YELLOW "\nDespertando receptores bloqueados...\n" COLOR_RESET);
    for (int i = 0; i < receptores_activos + 5; i++) {
        sem_post(&shm->espacios_ocupados);
    }

    // Los emisores pueden estar esperando en sem_wait(&espacios_libres)
    printf(COLOR_YELLOW "Despertando emisores bloqueados...\n" COLOR_RESET);
    for (int i = 0; i < emisores_activos + 5; i++) {
        sem_post(&shm->espacios_libres);
    }
    printf(COLOR_YELLOW "\n Esperando a que los procesos terminen...\n" COLOR_RESET);
    
    int timeout = 10;  // Máximo 10 segundos de espera
    int elapsed = 0;
    
    while (elapsed < timeout) {
        sem_wait(&shm->mutex);
        int emisores = shm->emisores_activos;
        int receptores = shm->receptores_activos;
        sem_post(&shm->mutex);
        
        if (emisores == 0 && receptores == 0) {
            printf(COLOR_GREEN "Todos los procesos han finalizado\n" COLOR_RESET);
            break;
        }
        
        printf("[%d/%d] Emisores: %d, Receptores: %d\r", 
               elapsed + 1, timeout, emisores, receptores);
        fflush(stdout);
        
        sleep(1);
        elapsed++;
    }
    printf("\n");

    // Verificación final
    sem_wait(&shm->mutex);
    int emisores_final = shm->emisores_activos;
    int receptores_final = shm->receptores_activos;
    sem_post(&shm->mutex);

    if (emisores_final > 0 || receptores_final > 0) {
        printf("Emisores restantes: %d\n", emisores_final);
        printf("Receptores restantes: %d\n", receptores_final);
    }

    printf("\n");
    print_statistics(shm);

    // Destruir semáforos
    sem_destroy(&shm->espacios_libres);
    sem_destroy(&shm->espacios_ocupados);
    sem_destroy(&shm->mutex);
    printf("Semáforos destruidos\n");

    // Desmapear memoria
    munmap(shm, shm_size);
    close(shm_fd);
    printf("Memoria desmapeada\n");

    // Eliminar el objeto de memoria compartida
    if (shm_unlink(shm_name) == 0) {
        printf("Memoria compartida eliminada\n");
    } else {
        perror("No se elimino la memoria");
    }

    printf("\n" COLOR_GREEN COLOR_BOLD "Finalización completada\n" COLOR_RESET);
    print_separator();

    return 0;
}