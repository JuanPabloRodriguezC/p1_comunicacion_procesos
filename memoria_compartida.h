#ifndef MEMORIA_COMPARTIDA_H
#define MEMORIA_COMPARTIDA_H

#include <semaphore.h>
#include <time.h>

#define MAX_FILENAME 256
// Códigos de color ANSI
#define COLOR_RESET   "\x1b[0m"
#define COLOR_GREEN   "\x1b[32m"
#define COLOR_CYAN    "\x1b[36m"
#define COLOR_YELLOW  "\x1b[33m"
#define COLOR_RED     "\x1b[31m"
#define COLOR_BLUE    "\x1b[34m"
#define COLOR_MAGENTA "\x1b[35m"
#define COLOR_BOLD    "\x1b[1m"

// Información de auditoría de cada carácter
typedef struct {
    char valor; 
    int posicion;
    time_t timestamp;
} char_info_t;

// Estructura de la memoria compartida
typedef struct {
    sem_t espacios_libres; 
    sem_t espacios_ocupados;
    sem_t mutex;// Protege el acceso memoria compartida
    sem_t file_mutex;
    
    char filename[MAX_FILENAME];  

    int file_read_position;
    int write_index;          // Dónde escribir el próximo carácter
    int read_index;           // Dónde leer el próximo carácter
    int chars_transferidos;   // estadisticas
    int emisores_activos;
    int receptores_activos;
    int finalizar;            // Senal finalizacion

    int buffer_size;
    char_info_t buffer[];
} shared_mem_t;

#endif