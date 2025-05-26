# Compilador y banderas
CC = gcc
CFLAGS = -Wall -pthread

# Archivos fuente y encabezado
RECEPTOR = receptor
SOLICITANTE = solicitante

# Regla principal
all: receptor solicitante

# Compilar receptor
receptor: receptor.c receptor.h
	$(CC) $(CFLAGS) -o $(RECEPTOR) receptor.c

# Compilar solicitante
solicitante: solicitante.c solicitante.h
	$(CC) $(CFLAGS) -o $(SOLICITANTE) solicitante.c

# Limpiar ejecutables y pipes
clean:
	rm -f receptor solicitante pipe_* pipeReceptor
