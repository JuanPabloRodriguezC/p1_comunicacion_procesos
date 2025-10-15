#ifndef MEMORIA_COMPARTIDA_H
#define MEMORIA_COMPARTIDA_H

#include <semaphore.h>
#include <time.h>

#define MAX_FILENAME 256

// Información de auditoría de cada carácter
typedef struct {
    char valor; 
    int posicion;
    time_t timestamp;
} char_info_t;

// Estructura de la memoria compartida
typedef struct {
    // Semáforos para el patrón productor-consumidor
    sem_t espacios_libres;    // Cuenta cuántos espacios hay disponibles para escribir
    sem_t espacios_ocupados;  // Cuenta cuántos espacios tienen datos para leer
    sem_t mutex;              // Protege el acceso a índices compartidos
    
    // Información del archivo y configuración
    char filename[MAX_FILENAME];  
    int buffer_size;
    
    // Índices del buffer circular
    int write_index;          // Dónde escribir el próximo carácter
    int read_index;           // Dónde leer el próximo carácter
    
    // Estadísticas
    int chars_transferidos;   // Total de caracteres que han pasado por el buffer
    int emisores_activos;
    int receptores_activos;
    int finalizar;            // Flag para señalar fin
    
    // Buffer circular (tamaño variable)
    char_info_t buffer[];
} shared_mem_t;

#endif