// receptor.c (VERSIÓN COMPLETA CON MODOS)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <termios.h>
#include <sys/select.h>
#include "memoria_compartida.h"


volatile sig_atomic_t keep_running = 1;
struct termios orig_termios;

void signal_handler(int signum) {
    (void)signum;
    keep_running = 0;
}

void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enable_raw_mode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);
    
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int wait_for_keypress() {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    
    printf(COLOR_CYAN "  [Presione cualquier tecla para continuar...]" COLOR_RESET "\r");
    fflush(stdout);
    
    int result = select(STDIN_FILENO + 1, &readfds, NULL, NULL, NULL);
    
    if (result > 0) {
        char c;
        read(STDIN_FILENO, &c, 1);
        printf("                                                    \r");
        return 1;
    }
    
    return 0;
}

void wait_automatic(int interval_ms) {
    usleep(interval_ms * 1000);
}

int main(int argc, char* argv[]){
    
    if (argc != 4) {
        fprintf(stderr, "Uso: %s <identificador_shm> <llave_desencriptacion> <modo>\n", argv[0]);
        fprintf(stderr, "Modos:\n");
        fprintf(stderr, "  auto:<milisegundos>  - Modo automático (ej: auto:500)\n");
        fprintf(stderr, "  manual               - Modo manual (presionar tecla)\n");
        fprintf(stderr, "\nEjemplos:\n");
        fprintf(stderr, "  %s /mi_shm 42 auto:1000    # Leer cada 1 segundo\n", argv[0]);
        fprintf(stderr, "  %s /mi_shm 42 manual       # Leer al presionar tecla\n", argv[0]);
        return 1;
    }

    const char *shm_name = argv[1];
    unsigned char llave = (unsigned char)atoi(argv[2]);
    char *modo_str = argv[3];
    
    // Parsear el modo de ejecución
    int modo_automatico = 0;
    int intervalo_ms = 1000;
    
    if (strncmp(modo_str, "auto:", 5) == 0) {
        modo_automatico = 1;
        intervalo_ms = atoi(modo_str + 5);
        if (intervalo_ms <= 0) {
            fprintf(stderr, "Error: Intervalo debe ser positivo\n");
            return 1;
        }
    } else if (strcmp(modo_str, "manual") == 0) {
        modo_automatico = 0;
    } else {
        fprintf(stderr, "Error: Modo inválido. Use 'auto:<ms>' o 'manual'\n");
        return 1;
    }
    
    printf("=== Receptor iniciado ===\n");
    printf("Llave de desencriptación: 0x%02X\n", llave);
    if (modo_automatico) {
        printf("Modo: " COLOR_BLUE "AUTOMÁTICO" COLOR_RESET " (intervalo: %d ms)\n\n", intervalo_ms);
    } else {
        printf("Modo: " COLOR_YELLOW "MANUAL" COLOR_RESET " (presionar tecla para leer)\n\n");
        enable_raw_mode();
    }

    signal(SIGINT, signal_handler);

    // Abrir memoria compartida (igual que antes)
    int shm_fd = shm_open(shm_name, O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("Error: No se puede abrir la memoria compartida");
        fprintf(stderr, "¿Ejecutó el inicializador primero?\n");
        return 1;
    }

    size_t base_size = sizeof(shared_mem_t);
    shared_mem_t *shm_temp = mmap(NULL, base_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    if (shm_temp == MAP_FAILED) {
        perror("Error al mapear memoria compartida (temporal)");
        close(shm_fd);
        return 1;
    }

    int buffer_size = shm_temp->buffer_size;
    
    if (buffer_size <= 0 || buffer_size > 10000) {
        fprintf(stderr, "Error: buffer_size inválido (%d)\n", buffer_size);
        munmap(shm_temp, base_size);
        close(shm_fd);
        return 1;
    }

    munmap(shm_temp, base_size);

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

    while (keep_running) {
        // Verificar flag de finalización
        sem_wait(&shm->mutex);
        int debe_finalizar = shm->finalizar;
        sem_post(&shm->mutex);
        
        if (debe_finalizar) {
            printf("\n" COLOR_YELLOW "Receptor: Señal de finalización recibida\n" COLOR_RESET);
            break;
        }

        // MODO DE EJECUCIÓN: Esperar según el modo
        if (modo_automatico) {
            wait_automatic(intervalo_ms);
        } else {
            if (!wait_for_keypress()) {
                break;
            }
        }

        // Intentar leer (puede bloquearse si buffer está vacío)
        if (sem_trywait(&shm->espacios_ocupados) == -1) {
            if (errno == EAGAIN) {
                printf(COLOR_RED "Buffer vacío, esperando datos...\n" COLOR_RESET);
                sem_wait(&shm->espacios_ocupados);
                
                // Verificar de nuevo después de despertar
                sem_wait(&shm->mutex);
                debe_finalizar = shm->finalizar;
                sem_post(&shm->mutex);
                
                if (debe_finalizar) {
                    sem_post(&shm->espacios_ocupados);
                    break;
                }
            } else {
                if (errno != EINTR) {
                    perror("Error en sem_trywait");
                }
                break;
            }
        }

        sem_wait(&shm->mutex);
        
        int pos = shm->read_index % buffer_size;
        unsigned char encrypted = shm->buffer[pos].valor;
        int posicion_original = shm->buffer[pos].posicion;
        time_t timestamp = shm->buffer[pos].timestamp;
        
        shm->read_index++;
        
        sem_post(&shm->mutex);
        
        unsigned char decrypted = encrypted ^ llave;
        
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

    sem_wait(&shm->mutex);
    shm->receptores_activos--;
    sem_post(&shm->mutex);

    munmap(shm, shm_size);
    close(shm_fd);

    return 0;
}