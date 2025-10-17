#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <termios.h>
#include <sys/select.h>
#include "memoria_compartida.h"



// Configuración del terminal para modo raw (leer sin esperar Enter)
struct termios orig_termios;
volatile sig_atomic_t keep_running = 1;

void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enable_raw_mode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);
    
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);  // Deshabilitar echo y modo canónico
    raw.c_cc[VMIN] = 0;   // Retornar inmediatamente
    raw.c_cc[VTIME] = 0;  // Sin timeout
    
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

// Esperar por una tecla en modo manual
int wait_for_keypress() {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    
    printf(COLOR_CYAN "  [Presione cualquier tecla para continuar...]" COLOR_RESET "\r");
    fflush(stdout);
    
    // select() se bloqueará hasta que haya entrada disponible
    int result = select(STDIN_FILENO + 1, &readfds, NULL, NULL, NULL);
    
    if (result > 0) {
        char c;
        read(STDIN_FILENO, &c, 1);  // Consumir la tecla
        printf("                                                    \r");  // Limpiar línea
        return 1;
    }
    
    return 0;
}

// Esperar en modo automático
void wait_automatic(int interval_ms) {
    usleep(interval_ms * 1000);
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Uso: %s <identificador_shm> <llave_encriptacion> <modo>\n", argv[0]);
        fprintf(stderr, "Modos:\n");
        fprintf(stderr, "  auto:<milisegundos>\n");
        fprintf(stderr, "  manual\n");
        fprintf(stderr, "\nEjemplos:\n");
        fprintf(stderr, "  %s /mi_memoria 42 auto:1000    # Escribir cada 1 segundo\n", argv[0]);
        fprintf(stderr, "  %s /mi_memoria 42 manual       # Escribir al presionar tecla\n", argv[0]);
        return 1;
    }

    const char *shm_name = argv[1];
    unsigned char llave = (unsigned char)atoi(argv[2]);
    char *modo_str = argv[3];

    //valores default
    int modo_automatico = 0;
    int intervalo_ms = 1000;
    
    //verifica cual modo se escoge
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
    
    printf("=== Emisor iniciado ===\n");
    printf("Llave de encriptación: 0x%02X\n", llave);
    if (modo_automatico) {
        printf("Modo: " COLOR_GREEN "AUTOMÁTICO" COLOR_RESET " (intervalo: %d ms)\n\n", intervalo_ms);
    } else {
        printf("Modo: " COLOR_YELLOW "MANUAL" COLOR_RESET " (presionar tecla para escribir)\n\n");
        enable_raw_mode();
    }

    // Abrir memoria compartida
    int shm_fd = shm_open(shm_name, O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("Error: No se puede abrir la memoria compartida");
        fprintf(stderr, "¿Ejecutó el inicializador primero?\n");
        return 1;
    }

    // Mapear memoria compartida temporal (no se conoce el tamaño)
    size_t base_size = sizeof(shared_mem_t);
    shared_mem_t *shm_temp = mmap(NULL, base_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    if (shm_temp == MAP_FAILED) {
        perror("Error al mapear memoria compartida (temporal)");
        close(shm_fd);
        return 1;
    }

    int buffer_size = shm_temp->buffer_size;
    char filename[MAX_FILENAME];
    strncpy(filename, shm_temp->filename, MAX_FILENAME);

    munmap(shm_temp, base_size);

    // vuelve a abrir memoria compartida con tamaño correcto
    size_t shm_size = sizeof(shared_mem_t) + (buffer_size * sizeof(char_info_t));
    shared_mem_t *shm = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    
    if (shm == MAP_FAILED) {
        perror("Error al mapear memoria compartida (completa)");
        close(shm_fd);
        return 1;
    }

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
    
    while (keep_running) {
        // Verificar flag de finalización
        sem_wait(&shm->mutex);
        int debe_finalizar = shm->finalizar;
        sem_post(&shm->mutex);
        
        if (debe_finalizar) {
            printf("\n" COLOR_YELLOW "Emisor: Señal de finalización recibida\n" COLOR_RESET);
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
        
        // ===== COORDINAR LECTURA DEL ARCHIVO =====
        sem_wait(&shm->file_mutex);  // ← Mutex para el archivo
        
        // Ir a la posición compartida
        if (fseek(archivo, shm->file_read_position, SEEK_SET) != 0) {
            sem_post(&shm->file_mutex);
            perror("Error en fseek");
            break;
        }
        
        // Leer UN carácter
        int c = fgetc(archivo);
        
        if (c == EOF) {
            // Fin del archivo alcanzado
            sem_post(&shm->file_mutex);
            printf("\n" COLOR_YELLOW "Emisor: Fin del archivo alcanzado\n" COLOR_RESET);
            break;
        }
        
        // Avanzar la posición compartida
        shm->file_read_position++;
        
        sem_post(&shm->file_mutex);  // ← Liberar mutex del archivo
        // ===== FIN DE COORDINACIÓN =====
        
        // Ahora intentar escribir en el buffer
        if (sem_trywait(&shm->espacios_libres) == -1) {
            if (errno == EAGAIN) {
                printf(COLOR_RED "Buffer lleno, esperando espacio...\n" COLOR_RESET);
                sem_wait(&shm->espacios_libres);
                
                // Verificar de nuevo si debemos finalizar después de despertar
                sem_wait(&shm->mutex);
                debe_finalizar = shm->finalizar;
                sem_post(&shm->mutex);
                
                if (debe_finalizar) {
                    sem_post(&shm->espacios_libres);  // Devolver el semáforo
                    break;
                }
            } else {
                perror("Error en sem_trywait");
                break;
            }
        }
        
        // Obtener acceso exclusivo a los índices del buffer
        sem_wait(&shm->mutex);
        
        int pos = shm->write_index % buffer_size;
        unsigned char encrypted = (unsigned char)c ^ llave;
        
        shm->buffer[pos].valor = encrypted;
        shm->buffer[pos].posicion = shm->write_index;
        shm->buffer[pos].timestamp = time(NULL);
        
        shm->write_index++;
        shm->chars_transferidos++;
        
        sem_post(&shm->mutex);
        sem_post(&shm->espacios_ocupados);
        
        // Mostrar información del carácter escrito
        char display_char = (c >= 32 && c < 127) ? c : '.';
        struct tm *tm_info = localtime(&shm->buffer[pos].timestamp);
        char time_str[20];
        strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);
        
        printf(COLOR_GREEN "'%c'" COLOR_RESET "        %-8d %-10d %s\n", 
               display_char, c, shm->buffer[pos].posicion, time_str);
        
        char_count++;
    }

    fclose(archivo);

    printf("\n" COLOR_YELLOW "Emisor finalizó: %d caracteres escritos" COLOR_RESET "\n", char_count);

    // Desregistrar este emisor
    sem_wait(&shm->mutex);
    shm->emisores_activos--;
    sem_post(&shm->mutex);

    munmap(shm, shm_size);
    close(shm_fd);

    return 0;
}