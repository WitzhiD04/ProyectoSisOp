/**************************************************************
#         		Pontificia Universidad Javeriana
#     Autor: Carlos Daniel Guiza
#     Fecha: 15 de Mayo de 2025
#     Materia: Sistemas Operativos
#     Tema: Proyecto - Sistema para el prestamo de libros
#     Fichero: receptor.c
#	Descripcion: Implementación del proceso receptor que gestiona la base de datos de libros y 
#                procesa operaciones (préstamo, devolución, renovación) enviadas por el solicitante. 
#                Utiliza hilos y named pipes para comunicación y sincronización.
#****************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include "receptor.h"

// Variables globales para el buffer y los mutex
struct Operaciones buffer[BUFFER_TAM];
// Contador de cuantas operaciones hay en el buffer
int bufferCont = 0;
pthread_mutex_t mutex;
pthread_cond_t cond_no_lleno = PTHREAD_COND_INITIALIZER;
pthread_cond_t cond_no_vacio = PTHREAD_COND_INITIALIZER;
// Se usa para saber cuando se terminan los hilos
int terminar = 0;

// Función que lee la base de datos de libros desde un archivo de texto y la carga en memoria
int leerDB(char *nomArchivo, struct Libros *libros) {
    // Se abre el archivo en modo lectura y se verifica que se haya creado correctamente
    FILE *archivo = fopen(nomArchivo, "r");
    if (!archivo) {
        printf("Error al abrir el archivo %s\n", nomArchivo);
        exit(1);
    }

    //Char que contendrá la linea leída
    char linea[256];
    //Contador de libros leídos
    int cont = 0;
    // While que va hasta que no lea mas líneas en el archivo o sobrepase el máximo de libros
    while (fgets(linea, sizeof(linea), archivo) && cont < MAX_LIBROS) {
        //Ignorar líneas vacias
        if (linea[0] == '\n' || linea[0] == '\0') continue;
        // Elimina el salto de línea
        linea[strcspn(linea, "\n")] = 0;
        // Salta a la siguiente iteración si es inválido
        if (sscanf(linea, "%249[^,],%d,%d", libros[cont].nombre, &libros[cont].isbn, &libros[cont].numEj) == 3) {
            if (libros[cont].numEj <= 0 || libros[cont].numEj > MAX_EJEMPLAR) {
                printf("Número de ejemplares inválido para ISBN %d: %d\n", libros[cont].isbn, libros[cont].numEj);
                continue;
            }
            printf("Libro leído: %s, ISBN: %d, NumEj: %d\n", libros[cont].nombre, libros[cont].isbn, libros[cont].numEj);
            //Leer ejemplares de libros
            for (int i = 0; i < libros[cont].numEj && fgets(linea, sizeof(linea), archivo); i++) {
                // Elimina salto de línea
                linea[strcspn(linea, "\n")] = 0;
                //Se guarda en un puntero para hacer los cambios con mayor comodidad
                struct Ejemplar *e = &libros[cont].ejemplares[i];
                //Char para guardar de  manera efectiva la fecha del ejemplar
                char fecha_str[11];
                //Verifica que la fecha tenga el formato válido y las guarda por separado en un entero, para luego guardarla
                //juntas en ejemplar
                if (sscanf(linea, "%d%*c%*c%c%*c%10[^,]", &e->numero, &e->status, fecha_str) == 3) {
                    int dia, mes, anio;
                    if (sscanf(fecha_str, "%d-%d-%d", &dia, &mes, &anio) == 3) {
                        snprintf(e->fecha, sizeof(e->fecha), "%02d-%02d-%04d", dia, mes, anio);
                        printf("Ejemplar leído: Num: %d, Status: %c, Fecha: %s\n", e->numero, e->status, e->fecha);
                    } else {
                        //error en caso de formato inválido
                        printf("Error al parsear la fecha: %s\n", fecha_str);
                        snprintf(e->fecha, sizeof(e->fecha), "01-01-2000");
                    }
                } else {
                    printf("Error con la línea de ejemplar: %s\n", linea);
                    continue;
                }
            }
            // Incrementa contador de libros
            cont++;
        }
    }
    //Cierra el archivo
    fclose(archivo);
    return cont;
}

//Añade una operación al buffer compartido, esperando si está lleno
void anadirBuffer(struct Operaciones *op) {
    // Bloquea el mutex para acceso exclusivo al buffer
    pthread_mutex_lock(&mutex);
    // Espera si el buffer está lleno y manda la señal de esperar en caso de que lo este
    while (bufferCont >= BUFFER_TAM) {
        pthread_cond_wait(&cond_no_lleno, &mutex);
    }
    // Añade la operación y aumenta el contador
    buffer[bufferCont++] = *op;
    // Notifica que hay datos disponibles
    pthread_cond_signal(&cond_no_vacio);
    //Libera el mutex
    pthread_mutex_unlock(&mutex);
}
//Lee y elimina una operación del buffer compartido, esperando si está vacío
struct Operaciones leerBuffer() {
    // Bloquea el mutex para acceso exclusivo al buffer
    pthread_mutex_lock(&mutex);
    // Espera en caso de que el buffer este vacio, evitando cualquier problema
    while (bufferCont == 0 && !terminar) {
        pthread_cond_wait(&cond_no_vacio, &mutex);
    }

    // Libera el mutex si no hay más datos
    if (bufferCont == 0) {
        pthread_mutex_unlock(&mutex);
        struct Operaciones op = {'Q', "", 0, 0};
        return op;
    }
    // Extrae operaciones de forma LIFO
    struct Operaciones op = buffer[--bufferCont];
    //Da la señal de que no está lleno el buffer
    pthread_cond_signal(&cond_no_lleno);
    //Libera el mutex
    pthread_mutex_unlock(&mutex);
    return op;
}

// Envía una respuesta al solicitante a través de un pipe nombrado específico
void enviarRespuesta(int pid, const char *mensaje) {
    //Char que guardara la respuesta
    char pipe2[20];
    // Construye el nombre del pipe a partir del pid mandado en la operación
    snprintf(pipe2, sizeof(pipe2), "pipe_%d", pid);
    int fd = -1, intentos = 5;

        //Da 5 intentos al pipe para abrirse
    while (intentos-- > 0) {
        // Intenta abrir el pipe de respuesta
        fd = open(pipe2, O_WRONLY);
        // Sale del bucle si se abre con éxito
        if (fd >= 0) { 
            break;
        }
        usleep(100000); // Espera para reintentar
    }

    // Muestra error si no se abre
    if (fd < 0) {
        printf("No se pudo abrir el pipe %s\n", pipe2);
        return;
    }

    // Escrube el mensaje en el pipe y manda error en caso de no poder enviarlo
    if (write(fd, mensaje, strlen(mensaje) + 1) == -1) {
        printf("Error al escribir en el pipe %s\n", pipe2);
    }

    close(fd);
}

// Lee una operación enviada por el solicitante a través del pipe principal
int leerPipe(int fd, struct Operaciones *op, int verbose) {
    //Char que guardara la respuesta
    char buffer[256];
    //Lee los datos del pipe
    int bytes = read(fd, buffer, sizeof(buffer) - 1);
    // No hay datos o fin 
    if (bytes <= 0) {
        return 0;
    } 

    buffer[bytes] = '\0';

        //Valida el formato en el que se recibió la operación
    if (sscanf(buffer, "%c,%249[^,],%d,%d", &op->tipo, op->nombre, &op->isbn, &op->pid) != 4) {
        printf("Formato inválido recibido: %s\n", buffer);
        return 0;
    }

    //Se imprime lo que se recibió en caso de haber activado verbose
    if (verbose) {
        printf("Recibido: tipo = %c, nombre = %s, isbn = %d, pid = %d\n", op->tipo, op->nombre, op->isbn, op->pid);
    }

    // Se añade al buffer y se marca para terminar los hilos en caso de ser Q
    if (op->tipo == 'Q') {
        anadirBuffer(op);
        terminar = 1;
        return 0;
        // Se retorna 1 en caso de ser devolución o renovación
    } else if (op->tipo == 'D' || op->tipo == 'R') {
        return 1;
        //Se retorna 2 en caso de ser préstamo
    } else if (op->tipo == 'P') {
        return 2;
    }

    return 0;
}

// Procesa las operaciones de devolución y renovación que esten en el buffer
void *auxiliar1(void *args) {
    // Se leen los argumentos pasados desde la creación del hilo
    struct Libros *libros = (struct Libros *)((void **)args)[0];
    int numLibros = *(int *)((void **)args)[1];

    //While que no tiene condición, se detiene si se usa un break
    while (1) {
    //Se lee una operación del buffer
        struct Operaciones op = leerBuffer();
        //Si el tipo es q, se sale del while
        if (op.tipo == 'Q') {
            break;
        }
        //Ciclo que recorre el el número de libros que hay en la base de datos
        for (int i = 0; i < numLibros; i++) {
            //Se verifica si el isbn y el nombre de libro de la operación es el mismo al libro actual
            if (libros[i].isbn == op.isbn && strcmp(libros[i].nombre, op.nombre) == 0) {
                // entero que sirve para saber si se encontro el ejemplar buscado
                int encontrado = 0;
                // Ciclo que recorre los ejemplares del libro encontrado
                for (int j = 0; j < libros[i].numEj; j++) {
                    // Se pregunta si el status del libro es prestado
                    if (libros[i].ejemplares[j].status == 'P' && !encontrado) {
                        // Condicional en caso de que el tipo de la op sea devolución
                        if (op.tipo == 'D') {
                            //Se cambia el status a devuelto
                            libros[i].ejemplares[j].status = 'D';
                            //Se notifica en pantalla
                            printf("Devolución realizada del libro: ISBN %d, Ejemplar %d\n", op.isbn, libros[i].ejemplares[j].numero);
                            //Se envía la respuesta al proceso solicitante y se marca como encontrado el libro
                            char respuesta[256];
                            snprintf(respuesta, sizeof(respuesta), "Devolución exitosa: ISBN %d, Ejemplar %d", op.isbn, libros[i].ejemplares[j].numero);
                            enviarRespuesta(op.pid, respuesta);
                            encontrado = 1;
                            break;
                            //Condicional en caso de que el tipo de la op sea renovar
                        } else if (op.tipo == 'R') {
                            // Se guarda las fechas en variables distintas para asegurar correctamente el cambio de fecha
                            char dia[3], mes[3], anio[5];
                            sscanf(libros[i].ejemplares[j].fecha, "%2s-%2s-%4s", dia, mes, anio);
                            int d = atoi(dia);
                            //se añaden 7 días
                            d += 7;
                            //Si días resulta mayor a 30 se resta 30 a los días
                            if (d > 30) {
                                d -= 30;
                                //aumenta el mes, si es mayor a 12 se vuelve el primer mes del año
                                int m = atoi(mes);
                                m++;
                                if (m < 1 || m > 12) {
                                    m = 1;
                                }
                                snprintf(mes, sizeof(mes), "%02d", m);
                            }
                            if (d < 1 || d > 30) d = 1; // Corrige en caso de aun haber un día inválido
                            // Se cmambia el día de entero a char
                            snprintf(dia, sizeof(dia), "%02d", d);
                            dia[2] = '\0';
                            mes[2] = '\0';
                            anio[4] = '\0';
                            //Se guarda el cambio en la fecha del ejemplar y se manda la respuesta al proceso solicitante
                            snprintf(libros[i].ejemplares[j].fecha, 11, "%2s-%2s-%4s", dia, mes, anio);
                            printf("Renovación procesada: ISBN %d, Ejemplar %d, Nueva fecha: %s\n", op.isbn, libros[i].ejemplares[j].numero, libros[i].ejemplares[j].fecha);
                            char respuesta[256];
                            snprintf(respuesta, sizeof(respuesta), "Renovación exitosa: ISBN %d, Ejemplar %d", op.isbn, libros[i].ejemplares[j].numero);
                            enviarRespuesta(op.pid, respuesta);
                            encontrado = 1;
                            break;
                        }
                    }
                }
                //Condicional en caso de no encontrar el ejemplar, se envía mensaje de error
                if (!encontrado) {
                    char respuesta[256];
                    snprintf(respuesta, sizeof(respuesta), "Error: No se encontró un ejemplar prestado para ISBN %d", op.isbn);
                    enviarRespuesta(op.pid, respuesta);
                    printf("No se encontró un ejemplar prestado para ISBN %d\n", op.isbn);
                }
                break;
            }
            //Condicional en caso de no encontrar un libro válidp, se envía mensaje de error
            if (i == numLibros - 1) {
                char respuesta[256];
                snprintf(respuesta, sizeof(respuesta), "Error: ISBN %d no encontrado o nombre erróneo", op.isbn);
                enviarRespuesta(op.pid, respuesta);
                printf("ISBN %d no encontrado\n", op.isbn);
            } 
        }
    }
    return NULL;
}

//Maneja comandos interactivos del usuario (s para salir, r para generar reporte)
void *auxiliar2(void *args) {
    // Se leen los argumentos pasados desde la creación del hilo
    struct Libros *libros = (struct Libros *)((void **)args)[0];
    int numLibros = *(int *)((void **)args)[1];
    //Se guarda el comando en este char
    char comando[3];

    //While que no tiene condición, se detiene si se usa un break
    while (1) {
        //Se válida que no se use mas de un caracter en los comandos
        if (scanf("%2s", comando) != 1) {
            while (getchar() != '\n'); // Limpia el buffer de entrada
            printf("Entrada inválida, utilice 's' para salir o 'r' para reporte\n");
            continue;
        }
        //// Limpia el buffer después de leer
        while (getchar() != '\n');
        //En caso de que se pida salir
        if (strcmp(comando, "s") == 0) {
            // Bloquea el mutex para modificar terminar
            pthread_mutex_lock(&mutex);
            //se marca para terminar los hilos
            terminar = 1;
            //Libera el mutex
            pthread_mutex_unlock(&mutex);
            // Notifica a otros hilos
            pthread_cond_broadcast(&cond_no_vacio);
            break;
            //En caso de que el comando sea de reporte
        } else if (strcmp(comando, "r") == 0) {
            printf("Reporte:\n");
            // Bloquea para acceso seguro a libros
            pthread_mutex_lock(&mutex);
            //Se imprimen los ejemplares
            for (int i = 0; i < numLibros; i++) {
                for (int j = 0; j < libros[i].numEj; j++) {
                    printf("%c, %s, %d, %d, %s\n", libros[i].ejemplares[j].status, libros[i].nombre, libros[i].isbn, libros[i].ejemplares[j].numero, libros[i].ejemplares[j].fecha);
                }
            }
            pthread_mutex_unlock(&mutex);
        } else {
            //Verificacion en caso de no ser r o s lo que se digita
            printf("Utilice solo 's' o 'r' si quiere acabar la ejecución o ver un reporte\n");
        }
    }
    return NULL;
}

// Procesa una operación de préstamo, actualizando el estado de un ejemplar disponible.
void prestamoProceso(struct Operaciones *op, struct Libros *libros, int numLibros) {
    //Para validar de que se encuentre el libro
    int libroEncontrado = 0;
    //Ciclo que recorre los libros
    for (int i = 0; i < numLibros; i++) {
        //Se verifica si el isbn y el nombre de libro de la operación es el mismo al libro actual
        if (libros[i].isbn == op->isbn && strcmp(libros[i].nombre, op->nombre) == 0) {
            //Marca que encontro el libro y hay otra validación por si se encontró ejemplar que no este prestado
            libroEncontrado = 1;
            int encontrado = 0;
            //Ciclo que recorre todos los ejemplares del libro
            for (int j = 0; j < libros[i].numEj; j++) {
                //Si encuentra uno no prestado, cambia el status a prestado y aumenta la fecha, de igual manera que en las renovaciones
                if (libros[i].ejemplares[j].status == 'D') {
                    libros[i].ejemplares[j].status = 'P';
                    char dia[3], mes[3], anio[5];
                    sscanf(libros[i].ejemplares[j].fecha, "%2s-%2s-%4s", dia, mes, anio);
                    int d = atoi(dia);
                    d += 7;
                    if (d > 30) {
                        d -= 30;
                        int m = atoi(mes);
                        m++;
                        if (m < 1 || m > 12) m = 1;
                        snprintf(mes, sizeof(mes), "%02d", m);
                    }
                    if (d < 1 || d > 30) d = 1;
                    snprintf(dia, sizeof(dia), "%02d", d);
                    dia[2] = '\0';
                    mes[2] = '\0';
                    anio[4] = '\0';
                    snprintf(libros[i].ejemplares[j].fecha, 11, "%2s-%2s-%4s", dia, mes, anio);
                    //Avisa que se realizó el préstamo y envia respuesta al proceso solicitante
                    printf("Préstamo realizado del libro: ISBN %d, Ejemplar %d\n", op->isbn, libros[i].ejemplares[j].numero);
                    char respuesta[256];
                    snprintf(respuesta, sizeof(respuesta), "Préstamo exitoso: ISBN %d, Ejemplar %d", op->isbn, libros[i].ejemplares[j].numero);
                    enviarRespuesta(op->pid, respuesta);
                    encontrado = 1;
                    return;
                }
            }
            //Si no encontro ejemplar manda mensaje de error
            if (!encontrado) {
                char respuesta[256];
                snprintf(respuesta, sizeof(respuesta), "Error: No se encontró un ejemplar disponible para ISBN %d", op->isbn);
                enviarRespuesta(op->pid, respuesta);
                printf("No se encontró un ejemplar disponible para ISBN %d\n", op->isbn);
            }
            return;
        }
    }
    //Si no encontro libro válido, manda mensaje de error
    if (!libroEncontrado) {
        char respuesta[256];
        snprintf(respuesta, sizeof(respuesta), "Error: ISBN %d no encontrado o nombre erróneo", op->isbn);
        enviarRespuesta(op->pid, respuesta);
        printf("ISBN %d no encontrado\n", op->isbn);
    }
}

// Guarda el estado final de la base de datos en un archivo de salida
void guardarSalida(char *fileSalida, struct Libros *libros, int numLibros) {
    //Abre el archivo en modo escritura
    FILE *salida = fopen(fileSalida, "w");
    //si hay error se le notifica al usuario
    if (!salida) {
        printf("Error al crear el archivo de salida\n");
        return;
    }
    // Guarda todos los libros y ejemplares en el archivo con el mismo formato de la base de datos
    for (int i = 0; i < numLibros; i++) {
        fprintf(salida, "%s,%d,%d\n", libros[i].nombre, libros[i].isbn, libros[i].numEj);
        for (int j = 0; j < libros[i].numEj; j++) {
            fprintf(salida, "%d,%c,%s\n", libros[i].ejemplares[j].numero, libros[i].ejemplares[j].status, libros[i].ejemplares[j].fecha);
        }
    }
    //Se cierra el archivo
    fclose(salida);
}


// Proceso principal. Inicializa los recursos, crea hilos, y procesa operaciones
int main(int argc, char *argv[]) {
    //Se verifica que se pase la cantidad de argumentos válida, de lo contrario se sale del programa
    if (argc != 5 && argc != 6 && argc != 7 && argc != 8) {
        printf("\n \t\tUse: $./receptor –p pipeReceptor –f filedatos [-v] [–s filesalida]\n");
        exit(1);
    }

    //Variables por si toca guardar datos según lo que se pase de argumento
    char *pipeRec = NULL;
    char *nomArchivo = NULL;
    int verbose = 0;
    char *fileSalida = NULL;
    //Arreglo de libros
    struct Libros libros[MAX_LIBROS];

        //Recorre los argumentos y revisa que banderas hay y cuales no, guardando la información respectiva
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            pipeRec = argv[++i];
        } else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            nomArchivo = argv[++i];
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            fileSalida = argv[++i];
        }
    }

    //Se cierra el programa en caso de no haber ni nombre de pipe ni nombre del archivo de la base de datos
    if (!pipeRec || !nomArchivo) {
        printf("\n \t\tUse: $./receptor –p pipeReceptor –f filedatos [-v] [–s filesalida]\n");
        exit(1);
    }

    // Se verifica que el pipe se haya creado con éxito y no exista desde antes
    if (mkfifo(pipeRec, 0666) == -1 && errno != EEXIST) {
        printf("Error al crear el pipe %s\n", pipeRec);
        exit(1);
    }
    // Abre el pipe en modo lectura/escritura y se verifica que se abra bien
    int fd = open(pipeRec, O_RDWR); // Evita EOF al inicio
    if (fd < 0) {
        printf("Error al abrir el pipe %s\n", pipeRec);
        exit(1);
    }
    // Se lee la base de datos y se verifica que se haya leído exitosamente
    int numLibros = leerDB(nomArchivo, libros);
    if (numLibros <= 0 || numLibros > MAX_LIBROS) {
        printf("Error cargando la base de datos\n");
        close(fd);
        unlink(pipeRec);
        exit(1);
    }

    //Se inicializa el mutex, se asigna memoria para los libros y se crea args para llevarlo a los métodos de los hilos
    pthread_mutex_init(&mutex, NULL);
    pthread_t hiloAux1, hiloAux2;
    void *args[2] = {libros, &numLibros};

    // Se crean los hilos
    pthread_create(&hiloAux1, NULL, auxiliar1, args);
    pthread_create(&hiloAux2, NULL, auxiliar2, args);

        //While encargado de leer el pipe y definir que hacer con lo que se lea
    struct Operaciones op;
    while (!terminar) {
        //Se lee el pipe y se devuelve el resultado, tal y como vimos antes
        int resultado = leerPipe(fd, &op, verbose);
        //Si resultado y contador es 0, se notifica a los usuarios y se menciona que ya no hay operaciones a los hilos
        if (resultado == 0 && bufferCont == 0) {
            pthread_cond_broadcast(&cond_no_vacio);
            break;
        }
        //resultado es igual a 1, se añade la operación al buffer
        if (resultado == 1) { // Operaciones D o R
            anadirBuffer(&op);
            //Si es 2, se llama directamente a prestamoProceso para manejar la operación
        } else if (resultado == 2) { // Operación P
            prestamoProceso(&op, libros, numLibros);
        }
    }

    //Se esperan a los hilos a que acabem y se cierra el pipe
    pthread_join(hiloAux1, NULL);
    pthread_join(hiloAux2, NULL);
    close(fd);

    //Si se marco que se quiere el archivo de salida, se llama al método respectivo
    if (fileSalida) {
        guardarSalida(fileSalida, libros, numLibros);
    }
    //Se destruye el mutex y se elimina el archivo del pipe
    pthread_mutex_destroy(&mutex);
    unlink(pipeRec);
    return 0;
}
