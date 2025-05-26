/**************************************************************
#         		Pontificia Universidad Javeriana
#     Autor: Carlos Daniel Guiza
#     Fecha: 15 de Mayo de 2025
#     Materia: Sistemas Operativos
#     Tema: Proyecto - Sistema para el prestamo de libros
#     Fichero: solicitante.h
#	Descripcion: Archivo de encabezado para solicitante.c. 
#                Define la estructura Operaciones y los prototipos de funciones utilizadas por el solicitante
#****************************************************************/

#ifndef SOLICITANTE_H
#define SOLICITANTE_H

// Estructura que representa una operación enviada al receptor.
struct Operaciones {
    char tipo;
    char nombre[250];
    int isbn;
};

// Funciones del solicitante
void leerRespuesta(int fdResp, const char *pipeRecibe, char tipo, int isbn);
void leerArchivo(char *nomArchivo, int fd, pid_t pid, const char *pipeRecibe, int fdResp);
void menu(int fd, pid_t pid, const char *pipeRecibe, int fdResp);

#endif
