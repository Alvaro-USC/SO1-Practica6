// En Santiago de Compostela, a 5 de diciembre de 2025
// Autores:
//	- Fabrizio Marinelli González
//	- Álvaro Schwiedop Souto

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <ctype.h>
#include <string.h>
#include <sys/wait.h>
#include <signal.h>

volatile int flagSigusr1 = 0;
volatile int flagSigusr2 = 0;

void handlerSIGUSR1(int sig) {
	flagSigusr1 = 1;
}

void handlerSIGUSR2(int sig) {
	flagSigusr2 = 1;
}

int main(int argc, char *argv[]) {
	// Validacion de argumentos
	if (argc != 3) {
		printf("Uso: %s <entrada> <salida>\n", argv[0]);
		return 1;
	}

	// Abrir y analizar archivo de entrada
	int fdEntrada = open(argv[1], O_RDONLY);
	if (fdEntrada == -1) {
		perror("Error al abrir archivo de entrada");
		return 1;
	}

	struct stat estArchivo;
	if (fstat(fdEntrada, &estArchivo) == -1) {
		perror("Error en fstat");
		return 1;
	}

	// Leemos el archivo de entrada a un buffer en memoria heap
	char *buffer = malloc(estArchivo.st_size);
	if (!buffer) {
		perror("Error en malloc");
		return 1;
	}

	if (read(fdEntrada, buffer, estArchivo.st_size) != estArchivo.st_size) {
		perror("Error leyendo entrada");
		return 1;
	}
	close(fdEntrada);

	// Calculamos el tamaño final necesario
	// para eso iteramos una vez para saber cuanto ocuparon los asteriscos
	long tamFinal = estArchivo.st_size; // tamaño base
	for (int i = 0; i < estArchivo.st_size; i++) {
		if (isdigit(buffer[i])) {
			tamFinal += (buffer[i] - '0' - 1); // sumamos solo el extra
		}
	}

	// archivo de salida
	int fdSalida = open(argv[2], O_RDWR | O_CREAT | O_TRUNC, 0666);
	if (fdSalida == -1) {
		perror("Error creando archivo salida");
		return 1;
	}

	// Ajustamos el tamaño fisico del archivo antes de proyectar
	if (ftruncate(fdSalida, tamFinal) == -1) {
		perror("Error en ftruncate inicial");
		return 1;
	}

	// Proyectar archivo de salida
	char *mapSalida = mmap(NULL, tamFinal, PROT_READ | PROT_WRITE, MAP_SHARED, fdSalida, 0);
	if (mapSalida == MAP_FAILED) {
		perror("Error mmap salida");
		return 1;
	}
	
	if (signal(SIGUSR1, handlerSIGUSR1) == SIG_ERR) {
		perror("signal SIGUSR1");
		munmap(mapSalida, tamFinal);
		free(buffer);
		close(fdSalida);
		return 1;
	}
	
	if (signal(SIGUSR2, handlerSIGUSR2) == SIG_ERR) {
		perror("signal SIGUSR2");
		munmap(mapSalida, tamFinal);
		free(buffer);
		close(fdSalida);
		return 1;
	}
	
	// Calculamos la mitad del archivo de entrada
	long mitad = estArchivo.st_size / 2;

	pid_t pid = fork();

	if (pid == -1) {
		perror("Error en fork");
		return 1;
	}
	

	if (pid == 0) {
		// Proceso hijo transforma los numeros en asteriscos

		// Esperar a que el padre termine la primera mitad
		while (!flagSigusr1) pause();

		long indiceSalida = 0;

		for (long i = 0; i < estArchivo.st_size; i++) {
			char c = buffer[i];

			// Al llegar a la mitad, esperar a que el padre termine todo el archivo
			if (i == mitad) {
				while (!flagSigusr2) pause();
			}
			
			if (isdigit(c)) {
				int num = c - '0';

				// Construir string temporal y copiar con memcpy
				char stringTemp[10]; // Buffer temporal para asteriscos
				memset(stringTemp, '*', num);

				memcpy(&mapSalida[indiceSalida], stringTemp, num);
				// Avanzamos el indice de salida segun los asteriscos escritos
				indiceSalida += num; 
			} else {
				// Si es letra, el hijo solo avanza el indice (el padre ya escribio ahi)
				indiceSalida += 1;
			}
		}

		// Limpieza hijo
		munmap(mapSalida, tamFinal);
		exit(0);

	} else {
		// Proceso padre convierte letras a mayusculas

		long indiceSalida = 0;

		for (long i = 0; i < estArchivo.st_size; i++) {
			char c = buffer[i];

			// Si llegamos a la mitad, avisamos al hijo
			if (i == mitad) {
				kill(pid, SIGUSR1);
			}

			if (isdigit(c)) {
				// Si es numero, el padre no escribe, solo avanza el hueco para el hijo
				indiceSalida += (c - '0');
			} else {
				// Transformar a mayuscula usando buffer temporal
				char charTemp;
				if (islower(c)) {
					charTemp = toupper(c);
				} else {
					charTemp = c;
				}

				// Escribir usando memcpy desde variable temporal
				memcpy(&mapSalida[indiceSalida], &charTemp, 1);
				indiceSalida += 1;
			}
		}

		// Fin del procesamiento del padre
        	kill(pid, SIGUSR2);
        
		// Esperamos a que el hijo termine
		wait(NULL);

		// añadir asteriscos y añadir linea final
		int numAsteriscos = 0;
		for(long i = 0; i < tamFinal; i++) {
			if(mapSalida[i] == '*') numAsteriscos++;
		}

		char menFinal[128];
		sprintf(menFinal, "\nTotal asteriscos: %d\n", numAsteriscos);
		int longitudMen = strlen(menFinal);

		//  Modificamos el tamaño de fichero salida con ftruncate y la proyeccion

		// Liberamos la proyeccion anterior
		munmap(mapSalida, tamFinal);

		// Extendemos el archivo fisico
		if (ftruncate(fdSalida, tamFinal + longitudMen) == -1) {
			perror("Error ftruncate final");
		} else {
			// Creamos nueva proyeccion con el tamaño nuevo
			char *mapaCompleto = mmap(NULL, tamFinal + longitudMen, PROT_READ | PROT_WRITE, MAP_SHARED, fdSalida, 0);

			if (mapaCompleto != MAP_FAILED) {
				// Escribimos el mensaje al final
				memcpy(mapaCompleto + tamFinal, menFinal, longitudMen);

				// Aseguramos que se escriba
				msync(mapaCompleto, tamFinal + longitudMen, MS_SYNC);
				munmap(mapaCompleto, tamFinal + longitudMen);
			} else {
				perror("Error mmap final");
			}
		}

		free(buffer);
		close(fdSalida);

		printf("Resultado guardado en %s\n", argv[2]);
	}

	return 0;
}
