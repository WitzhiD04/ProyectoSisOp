/**************************************************************
#         		Pontificia Universidad Javeriana
#     Autor: Carlos Daniel Guiza
#     Fecha: 15 de Mayo de 2025
#     Materia: Sistemas Operativos
#     Tema: Proyecto - Sistema para el prestamo de libros
#     Fichero: solicitante.c
#	Descripcion: Implementación del proceso solicitante que envía operaciones 
                al receptor a través de named pipes. 
                Soporta modo interactivo y lectura desde archivo
#****************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include "solicitante.h"

// Función para leer respuestas del pipe (usada por ambas funciones)
void leerRespuesta(int fdResp, const char *pipeRecibe, char tipo, int isbn) {
    //Char para almacenar la respuesta junto al total de intentos para abrir el pipe
    char respuesta[256];
    int intentos = 10;
    int total_bytes = 0;

    //Da 10 intentos al pipe para abrirse
    while (intentos-- > 0) {
        //Se lee el pipe de respuesta y se imprime lo recibido
        int bytes = read(fdResp, respuesta + total_bytes, sizeof(respuesta) - 1 - total_bytes);
        if (bytes > 0) {
            total_bytes += bytes;
            if (respuesta[total_bytes - 1] == '\0') { // Mensaje completo recibido
                printf("Respuesta del receptor para operación %c, ISBN %d: %s\n", tipo, isbn, respuesta);
                return;
            }
        } else if (bytes == 0) {
            // Fin (pipe cerrado por el otro extremo)
            printf("El pipe de respuesta %s fue cerrado por el receptor\n", pipeRecibe);
            return;
        } else {
            printf("Error al leer el pipe de respuesta \n");
            return;
        }
        usleep(100000); // Esperar 100ms antes de reintentar
    }
    printf("No se recibió respuesta para la operación %c, ISBN %d después de varios intentos\n", tipo, isbn);
}

// Lee operaciones desde un archivo de texto y las envía al receptor
void leerArchivo(char *nomArchivo, int fd, pid_t pid, const char *pipeRecibe, int fdResp) {
    //Se abre el archivo en modo lectura
    FILE *archivo = fopen(nomArchivo, "r");
    //Se verifica que el archivo haya sido leído exitosamente
    if (!archivo) {
        printf("Error al abrir el archivo %s\n", nomArchivo);
        close(fd);
        close(fdResp);
        exit(1);
    }
    //Char para almacenar línea
    char linea[256];
    int Qmandado = 0;
    // While que va hasta que no lea mas líneas en el archivo
    while (fgets(linea, sizeof(linea), archivo)) {
         //Ignorar líneas vacias
        if (linea[0] == '\n' || linea[0] == '\0') continue;
        struct Operaciones op;
        //Verifica que la línea tenga el formato válido
        if (sscanf(linea, "%c, %249[^,], %d", &op.tipo, op.nombre, &op.isbn) == 3) {
            // Leer respuesta para la operación Q
            if (op.tipo == 'Q') {
                Qmandado = 1;
                char mensaje[256];
                //Se escribe el mensaje en el pipe
                snprintf(mensaje, sizeof(mensaje), "Q,Salir,0,%d", pid);
                write(fd, mensaje, strlen(mensaje) + 1);
                break;
            }
            //Se escribe el mensaje en el pipe y se llama a leer respuesta para esperar la respuesta de receptor
            char mensaje[256];
            snprintf(mensaje, sizeof(mensaje), "%c,%s,%d,%d", op.tipo, op.nombre, op.isbn, pid);
            write(fd, mensaje, strlen(mensaje) + 1);
            leerRespuesta(fdResp, pipeRecibe, op.tipo, op.isbn);

        } else {
            printf("Error al leer la línea: %s\n", linea);
        }
    }
     // Si no se mandó Q, preguntar al usuario si desea mandarlo
    if (!Qmandado) {
        char opcion[4];
        while (1) {
            printf("No se ha enviado la operación de salida (Q). Digite s cuando desee enviarla: ");
            if (fgets(opcion, sizeof(opcion), stdin)) {
                if (opcion[0] == 's' || opcion[0] == 'S') {
                    Qmandado = 1;
                    break;
                } else if (opcion[0] == 'n' || opcion[0] == 'N') {
                    continue;
                }
            }
        }
    }

    //Cerrar archivo
    fclose(archivo);
}

//Implementa un menú para que el usuario ingrese operaciones manualmente si no quiere con el archivo
void menu(int fd, pid_t pid, const char *pipeRecibe, int fdResp) {
    //Entero que sirve para verificar si se siguen digitando operaciones o no
    int continuar = 1;
    //While que va mientras continuar sea verdadero
    while (continuar) {
        //Pedir al usuario que digite la información de la operación
        struct Operaciones op;
        printf("Operación (D/R/P): ");
        scanf(" %c", &op.tipo);

        printf("Nombre del libro: ");
        scanf(" %249[^\n]", op.nombre);
        while (getchar() != '\n');

        printf("ISBN: ");
        scanf("%d", &op.isbn);
        while (getchar() != '\n');

            //Se verifica que la operación que se haya digitado sea una de las 3 disponibles, de lo contrario se vuelve a preguntar
        if (op.tipo != 'D' && op.tipo != 'R' && op.tipo != 'P') {
            printf("Operación inválida. Debe ser D, R o P.\n");
            continue;
        }

        //Se manda el mensaje en el pipe y se llama a leer respuesta del receptor
        char mensaje[256];
        snprintf(mensaje, sizeof(mensaje), "%c,%s,%d,%d", op.tipo, op.nombre, op.isbn, pid);
        write(fd, mensaje, strlen(mensaje) + 1);
        leerRespuesta(fdResp, pipeRecibe, op.tipo, op.isbn);

        //Verificación en caso de que el usuario quiera digitar más opciones o no
        int cont = -1;
        while (cont < 0 || cont > 1) {
            printf("Introduzca si desea enviar otra operación [0 para sí, 1 para no]: ");
            scanf("%d", &cont);
            while (getchar() != '\n');
        }

        if (cont == 1) {
            continuar = 0;
        }
    }

    //Al acabar, se manda automáticamente la operación de salida
    char mensaje[256];
    snprintf(mensaje, sizeof(mensaje), "Q,Salir,0,%d", pid);
    write(fd, mensaje, strlen(mensaje) + 1);
    // Leer respuesta para la operación Q
    //leerRespuesta(fdResp, pipeRecibe, 'Q', 0);
}

//Función principal del solicitante. Inicializa los pipes y ejecuta el modo interactivo o de archivo
int main(int argc, char *argv[]) {
    //Se verifica el número de argumentos pasados, para ver si es válido o no
    if (argc != 3 && argc != 5) {
        printf("\n\tUse: $./solicitante [-i file] -p pipeReceptor\n");
        exit(1);
    }
    //Variables por si toca guardar datos según lo que se pase de argumento
    char *pipeRec = NULL;
    char *nomArchivo = NULL;
    
    //Recorre los argumentos y revisa que banderas hay y cuales no, guardando la información respectiva
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            pipeRec = argv[++i];
        } else if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            nomArchivo = argv[++i];
        }
    }

    //Se cierra el programa en caso de no haber nombre de pipe 
    if (!pipeRec) {
        printf("\n\tError: Debe especificar un pipe receptor con -p\n");
        exit(1);
    }

    // Se intenta abrir el pipe en modo escritura
    int fd = open(pipeRec, O_WRONLY);
    if (fd < 0) {
        printf("Error al abrir el pipe %s\n", pipeRec);
        exit(1);
    }

    //Se guarda el id del proceso para crear el pipe que recibe respuestas
    pid_t pid = getpid();
    char pipeRecibe[20];
    snprintf(pipeRecibe, sizeof(pipeRecibe), "pipe_%d", pid);

    //Se intenta crear el pipe, en este caso estará abierto en ambos sentidos para evitar problemas, se verifica que se abra bien 
    //Y que no exista ya
    if (mkfifo(pipeRecibe, 0666) == -1 && errno != EEXIST) {
        printf("Error al crear el pipe de respuesta %s\n", pipeRecibe);
        close(fd);
        exit(1);
    }

    //Se intenta abrir este nuevo pipe que recibe respuestas del receptor
    int fdResp = open(pipeRecibe, O_RDWR);  //abierto en ambos sentidos para evitar problemas
    if (fdResp < 0) {
        printf("Error al abrir el pipe de respuesta %s\n", pipeRecibe);
        close(fd);
        unlink(pipeRecibe);
        exit(1);
    }
    //Se verifica si se tiene nombre de archivo, si no, se manda al menú

    if (nomArchivo) {
        leerArchivo(nomArchivo, fd, pid, pipeRecibe, fdResp);
    } else {
        menu(fd, pid, pipeRecibe, fdResp);
    }

    //Se espera a que el usuario digite s para salir, para luego cerrar los pipes
    char comando[3];
    while (1) {
        printf("Ingrese 's' para salir: ");
        if (scanf("%2s", comando) != 1) {
            while (getchar() != '\n');
            continue;
        }
        while (getchar() != '\n');
        if (strcmp(comando, "s") == 0) {
            break;
        }
    }
    close(fd);
    close(fdResp);
    unlink(pipeRecibe);
    return 0;
}

