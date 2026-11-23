// ============================================================================
// SIMULADOR DE MEMORIA + PLANIFICACION ROUND ROBIN
// Combina dos cosas del curso de Sistemas Operativos:
//   1) Administracion de memoria: Peor Ajuste (Worst Fit) o Buddy System.
//   2) Planificacion de CPU: Round Robin (cada proceso usa el CPU un "cuanto").
// En cada CICLO: llega (o se reintenta) un proceso -> se intenta meter a memoria
// -> si entra, se forma en la cola RR -> se atiende al proceso del frente de la cola.
// ============================================================================

#include <iostream>
#include <cstdio>     // printf
#include <cstdlib>    // rand, srand
#include <ctime>      // time (para sembrar rand)
#include <climits>    // INT_MAX (para permitir cualquier entero positivo en el cuanto)
#include <string>
#include <vector>     // memoria y procesos (arreglos dinamicos)
#include <deque>      // cola de Round Robin (se saca por el frente, se mete por atras)
#include <conio.h>    // _kbhit, _getch
#include <windows.h>  // Sleep

using namespace std;

// CONSTANTES Y ESTRUCTURAS
const int PEOR_AJUSTE = 1;          // valor que representa el algoritmo 1
const int BUDDY_SYSTEM = 2;         // valor que representa el algoritmo 2
const int TAMANO_MINIMO_BUDDY = 32; // el bloque buddy mas chico permitido (KB)

// Un proceso del simulador.
struct Proceso {
    int id;               // numero de proceso (1, 2, 3...)
    int tamano_pedido;    // KB de memoria que solicita
    int cuanto_restante;  // unidades de CPU que todavia le faltan
    int rafaga_total;     // unidades de CPU totales (no cambia; sirve para estadisticas)
    int tiempo_llegada;   // valor del reloj cuando fue creado
    int tiempo_fin;       // valor del reloj cuando termino (-1 = aun no termina)
};

// Un bloque de memoria: puede ser un hueco libre o estar ocupado por un proceso.
// La memoria completa es un vector de bloques contiguos ordenados por direccion.
struct Bloque {
    int direccion;       // donde empieza el bloque (KB)
    int tamano;          // tamano del bloque (KB)
    int id_proceso;      // 0 = hueco libre
    int tamano_pedido;   // lo que de verdad pidio el proceso (en buddy puede ser menor que tamano)
};

// Parametros que el usuario elige al inicio.
struct Configuracion {
    int memoria_total_kb;
    int algoritmo;             // PEOR_AJUSTE o BUDDY_SYSTEM
    int cuanto_sistema;        // quantum de Round Robin (cualquier entero positivo >= 1)
    int tamano_maximo_proceso; // los procesos piden entre 1 y este valor
    int cuanto_maximo_proceso; // las rafagas van de 1 a este valor
    int retardo_ms;            // ms de espera automatica entre ciclos
};

// TODO el estado de la simulacion en un solo paquete. Se pasa por referencia (&)
// a todas las funciones para que puedan leerlo y modificarlo.
struct Simulador {
    Configuracion configuracion;
    vector<Bloque> memoria;        // bloques ordenados por direccion
    vector<Proceso> procesos;      // procesos[id - 1]
    deque<int> cola_round_robin;   // guarda posiciones dentro de procesos (no copias)
    int indice_en_espera;          // -1 si nadie espera memoria
    int reloj;                     // tiempo simulado de CPU
    int ciclo;                     // numero de ciclo actual
    int procesos_terminados;       // para estadisticas
    int veces_sin_memoria;         // cuantas veces un proceso no cupo
    long long suma_ocupacion_kb;   // acumulado de memoria ocupada por ciclo (para el promedio)
    bool salir;                    // true cuando el usuario presiona Q o ESC
};

// PROTOTIPOS
int leerEntero(string mensaje, int minimo, int maximo);
Configuracion pedirConfiguracion();
void inicializarSimulador(Simulador &simulador, Configuracion configuracion);
int generarProceso(Simulador &simulador);
int siguientePotenciaDeDos(int tamano);
bool asignarPeorAjuste(Simulador &simulador, Proceso &proceso);
bool asignarBuddy(Simulador &simulador, Proceso &proceso);
bool colocarEnMemoria(Simulador &simulador, int indice_proceso);
int liberarMemoria(Simulador &simulador, int id_proceso);
void condensarHuecos(Simulador &simulador);
void fusionarBuddies(Simulador &simulador, int posicion_bloque);
void atenderRoundRobin(Simulador &simulador);
void ejecutarCiclo(Simulador &simulador);
int buscarBloqueDeProceso(Simulador &simulador, int id_proceso);
int calcularMemoriaOcupada(Simulador &simulador);
void imprimirBloque(Simulador &simulador, Bloque bloque);
void imprimirProceso(Simulador &simulador, int indice_proceso);
void imprimirMemoria(Simulador &simulador);
void imprimirColaRoundRobin(Simulador &simulador);
void imprimirEncabezadoCiclo(Simulador &simulador);
void imprimirEstadisticas(Simulador &simulador);
void esperarSiguienteCiclo(Simulador &simulador);
void revisarTeclado(Simulador &simulador);
bool hayTeclaPresionada();
int leerTecla();
void dormir(int milisegundos);

// PROGRAMA PRINCIPAL
int main(void) {
    srand((unsigned)time(NULL));   // semilla aleatoria distinta en cada corrida
    Simulador simulador;
    bool repetir = false;

    do {
        Configuracion configuracion = pedirConfiguracion();
        inicializarSimulador(simulador, configuracion);

        cout << "\nControles: [P] pausar/reanudar   [+] mas rapido   [-] mas lento   [Q / ESC] detener simulacion\n";
        while (!simulador.salir) {
            ejecutarCiclo(simulador);  // el ciclo corre automaticamente
            if (!simulador.salir) {
                esperarSiguienteCiclo(simulador);
            }
        }
        imprimirEstadisticas(simulador);

        // Limpia teclas pendientes antes de esperar confirmacion para salir o reiniciar
        while (hayTeclaPresionada()) {
            leerTecla();
        }
        cout << "\nPresiona [R] para nueva simulacion o cualquier otra tecla para salir...";
        int tecla_final = leerTecla();
        repetir = (tecla_final == 'r' || tecla_final == 'R');
        if (repetir) {
            cout << "\n\n";
        }
    } while (repetir);

    return 0;
}

// IMPLEMENTACION: CONFIGURACION

// Pide un entero validado en el rango [minimo, maximo]. Repite hasta que sea valido.
int leerEntero(string mensaje, int minimo, int maximo) {
    int valor;
    while (true) {
        cout << mensaje;
        // "cin >> valor" falla si el usuario escribe letras; el && corta antes de comparar
        if (cin >> valor && valor >= minimo && valor <= maximo) {
            cin.ignore(10000, '\n');   // limpia el salto de linea pendiente
            return valor;
        }
        cin.clear();                   // quita el estado de error de cin
        cin.ignore(10000, '\n');       // descarta lo que quedo escrito en la linea
        if (maximo == INT_MAX) {
            printf("  Valor invalido. Debe ser un entero positivo (mayor o igual a %d).\n", minimo);
        } else {
            printf("  Valor invalido. Debe estar entre %d y %d.\n", minimo, maximo);
        }
    }
}

// Menu inicial: llena la estructura Configuracion con lo que elija el usuario.
Configuracion pedirConfiguracion() {
    Configuracion configuracion;
    cout << "=========================================================\n";
    cout << "  SIMULADOR DE MEMORIA Y PLANIFICACION (Round Robin)\n";
    cout << "  Equipo 6: Peor Ajuste + Buddy System\n";
    cout << "=========================================================\n";
    cout << "Algoritmo de colocacion:\n  1) Peor ajuste\n  2) Buddy System\n";
    configuracion.algoritmo = leerEntero("Opcion: ", 1, 2);

    cout << "Tamano de la memoria:\n  1) 1 MB (1024 KB)\n  2) 4 MB (4096 KB)\n  3) 8 MB (8192 KB)\n";
    int opcion_memoria = leerEntero("Opcion: ", 1, 3);
    int tamanos_memoria[3] = {1024, 4096, 8192};   // en KB (todos potencias de 2, requisito de buddy)
    configuracion.memoria_total_kb = tamanos_memoria[opcion_memoria - 1];

    // Sin limite superior: cualquier entero positivo >= 1
    configuracion.cuanto_sistema = leerEntero("Cuanto del sistema (entero positivo >= 1): ", 1, INT_MAX);

    // Un proceso no puede pedir mas que toda la memoria
    configuracion.tamano_maximo_proceso = leerEntero("Tamano maximo de un proceso en KB: ", 1, configuracion.memoria_total_kb);
    configuracion.cuanto_maximo_proceso = leerEntero("Cuanto maximo de un proceso (1-100): ", 1, 100);

    cout << "Velocidad automatica:\n  1) Lenta\n  2) Normal\n  3) Rapida\n";
    int opcion_velocidad = leerEntero("Opcion: ", 1, 3);
    int retardos[3] = {2000, 800, 200};            // ms de espera entre ciclos
    configuracion.retardo_ms = retardos[opcion_velocidad - 1];

    return configuracion;
}

// Deja el simulador en su estado inicial: toda la memoria es UN solo hueco libre.
void inicializarSimulador(Simulador &simulador, Configuracion configuracion) {
    simulador.configuracion = configuracion;
    simulador.memoria.clear();
    // {direccion, tamano, id_proceso, tamano_pedido} -> hueco de toda la memoria en dir 0
    Bloque memoria_completa = {0, configuracion.memoria_total_kb, 0, 0};
    simulador.memoria.push_back(memoria_completa);
    simulador.procesos.clear();
    simulador.cola_round_robin.clear();
    simulador.indice_en_espera = -1;
    simulador.reloj = 0;
    simulador.ciclo = 1;
    simulador.procesos_terminados = 0;
    simulador.veces_sin_memoria = 0;
    simulador.suma_ocupacion_kb = 0;
    simulador.salir = false;
}

// Crea un proceso aleatorio y lo guarda. Devuelve su POSICION en el vector.
int generarProceso(Simulador &simulador) {
    Proceso nuevo;
    nuevo.id = (int)simulador.procesos.size() + 1;   // ids consecutivos desde 1
    // 1 + rand() % N -> numero aleatorio entre 1 y N
    nuevo.tamano_pedido = 1 + rand() % simulador.configuracion.tamano_maximo_proceso;
    nuevo.cuanto_restante = 1 + rand() % simulador.configuracion.cuanto_maximo_proceso;
    nuevo.rafaga_total = nuevo.cuanto_restante;      // copia para recordar la rafaga original
    nuevo.tiempo_llegada = simulador.reloj;
    nuevo.tiempo_fin = -1;                           // -1 = todavia no termina
    simulador.procesos.push_back(nuevo);
    return (int)simulador.procesos.size() - 1;
}

// IMPLEMENTACION: COLOCACION EN MEMORIA

// Despachador: llama al algoritmo elegido. Devuelve true si el proceso cupo.
bool colocarEnMemoria(Simulador &simulador, int indice_proceso) {
    Proceso &proceso = simulador.procesos[indice_proceso];
    if (simulador.configuracion.algoritmo == PEOR_AJUSTE) {
        cout << "----- ASIGNANDO CON PEOR AJUSTE -----\n";
        return asignarPeorAjuste(simulador, proceso);
    }
    cout << "----- ASIGNANDO EN BUDDY SYSTEM -----\n";
    return asignarBuddy(simulador, proceso);
}

// PEOR AJUSTE: de todos los huecos donde cabe el proceso, elige el MAS GRANDE.
// Idea: el sobrante que queda tambien es grande y sigue siendo util.
bool asignarPeorAjuste(Simulador &simulador, Proceso &proceso) {
    int posicion_elegida = -1;   // -1 = todavia no se encontro hueco
    cout << "Huecos revisados: ";
    for (int i = 0; i < (int)simulador.memoria.size(); i++) {
        Bloque &bloque = simulador.memoria[i];
        if (bloque.id_proceso != 0) {
            continue;            // bloque ocupado: se ignora
        }
        printf("%d KB(%s) ", bloque.tamano, bloque.tamano >= proceso.tamano_pedido ? "cabe" : "no cabe");
        if (bloque.tamano >= proceso.tamano_pedido) {
            // Me quedo con el mayor: si es el primero que cabe, o es mas grande que el elegido
            if (posicion_elegida == -1 || bloque.tamano > simulador.memoria[posicion_elegida].tamano) {
                posicion_elegida = i;
            }
        }
    }
    cout << "\n";
    if (posicion_elegida == -1) {
        return false;            // ningun hueco alcanza
    }
    Bloque hueco = simulador.memoria[posicion_elegida];   // copia del hueco original
    printf("Se elige el hueco mas grande: %d KB en la direccion %d\n", hueco.tamano, hueco.direccion);

    // El hueco se convierte en un bloque ocupado del tamano EXACTO pedido...
    Bloque ocupado = {hueco.direccion, proceso.tamano_pedido, proceso.id, proceso.tamano_pedido};
    simulador.memoria[posicion_elegida] = ocupado;
    // ...y si sobro espacio, se crea un nuevo hueco justo despues con lo restante.
    if (hueco.tamano > proceso.tamano_pedido) {
        Bloque sobrante = {hueco.direccion + proceso.tamano_pedido, hueco.tamano - proceso.tamano_pedido, 0, 0};
        simulador.memoria.insert(simulador.memoria.begin() + posicion_elegida + 1, sobrante);
    }
    return true;
}

// Redondea hacia arriba a la siguiente potencia de 2 (minimo 32 KB).
// Ej: 33 -> 64, 100 -> 128, 5 -> 32.
int siguientePotenciaDeDos(int tamano) {
    int potencia = TAMANO_MINIMO_BUDDY;
    while (potencia < tamano) {
        potencia = potencia * 2;
    }
    return potencia;
}

// BUDDY SYSTEM: los bloques solo pueden medir potencias de 2. Se busca el hueco
// MAS CHICO que alcance y se parte a la mitad hasta que quede del tamano justo.
bool asignarBuddy(Simulador &simulador, Proceso &proceso) {
    int tamano_necesario = siguientePotenciaDeDos(proceso.tamano_pedido);
    int posicion_elegida = -1;
    // Mejor ajuste entre huecos libres: el de menor tamano que todavia cabe.
    for (int i = 0; i < (int)simulador.memoria.size(); i++) {
        Bloque &bloque = simulador.memoria[i];
        if (bloque.id_proceso == 0 && bloque.tamano >= tamano_necesario) {
            if (posicion_elegida == -1 || bloque.tamano < simulador.memoria[posicion_elegida].tamano) {
                posicion_elegida = i;
            }
        }
    }
    if (posicion_elegida == -1) {
        return false;
    }
    // Split: se parte a la mitad hasta llegar al tamano necesario (minimo 32 KB)
    while (simulador.memoria[posicion_elegida].tamano > tamano_necesario) {
        Bloque actual = simulador.memoria[posicion_elegida];
        int mitad = actual.tamano / 2;
        printf("Split: bloque de %d KB (@%d) -> buddies de %d KB (@%d y @%d)\n",
               actual.tamano, actual.direccion, mitad, actual.direccion, actual.direccion + mitad);
        Bloque izquierdo = {actual.direccion, mitad, 0, 0};
        Bloque derecho = {actual.direccion + mitad, mitad, 0, 0};
        simulador.memoria[posicion_elegida] = izquierdo;                                     // mitad izquierda reemplaza al bloque
        simulador.memoria.insert(simulador.memoria.begin() + posicion_elegida + 1, derecho); // mitad derecha va justo despues
    }
    // Ya tiene el tamano justo: se marca como ocupado.
    simulador.memoria[posicion_elegida].id_proceso = proceso.id;
    simulador.memoria[posicion_elegida].tamano_pedido = proceso.tamano_pedido;
    // Fragmentacion interna: lo que sobra dentro del bloque que el proceso no usa.
    printf("Bloque asignado: %d KB en @%d (pedido: %d KB, desperdicio interno: %d KB)\n",
           tamano_necesario, simulador.memoria[posicion_elegida].direccion,
           proceso.tamano_pedido, tamano_necesario - proceso.tamano_pedido);
    return true;
}

// IMPLEMENTACION: LIBERAR, CONDENSAR Y FUSIONAR

// Busca en que posicion de la memoria esta el bloque de un proceso (-1 si no esta).
int buscarBloqueDeProceso(Simulador &simulador, int id_proceso) {
    for (int i = 0; i < (int)simulador.memoria.size(); i++) {
        if (simulador.memoria[i].id_proceso == id_proceso) {
            return i;
        }
    }
    return -1;
}

// Marca el bloque del proceso como libre. Devuelve su posicion (para poder fusionar despues).
int liberarMemoria(Simulador &simulador, int id_proceso) {
    int posicion = buscarBloqueDeProceso(simulador, id_proceso);
    if (posicion != -1) {
        simulador.memoria[posicion].id_proceso = 0;
        simulador.memoria[posicion].tamano_pedido = 0;
    }
    return posicion;
}

// Para PEOR AJUSTE: une huecos libres que quedaron vecinos en uno solo mas grande.
void condensarHuecos(Simulador &simulador) {
    int uniones = 0;
    int i = 0;
    while (i + 1 < (int)simulador.memoria.size()) {
        if (simulador.memoria[i].id_proceso == 0 && simulador.memoria[i + 1].id_proceso == 0) {
            simulador.memoria[i].tamano += simulador.memoria[i + 1].tamano;   // absorbe al vecino
            simulador.memoria.erase(simulador.memoria.begin() + i + 1);       // elimina al vecino
            uniones++;
            // NO se avanza i: el nuevo hueco podria tener otro hueco libre a su lado
        } else {
            i++;
        }
    }
    if (uniones > 0) {
        cout << "Condensando huecos\n";
    } else {
        cout << "No hay huecos pegados que condensar\n";
    }
}

// Para BUDDY: intenta fusionar el bloque recien liberado con su "buddy" (companero).
// Solo se fusionan si el buddy esta libre Y entero (del mismo tamano). Se repite
// hacia arriba: dos de 32 -> uno de 64, luego si su buddy de 64 esta libre -> 128...
void fusionarBuddies(Simulador &simulador, int posicion_bloque) {
    while (true) {
        Bloque actual = simulador.memoria[posicion_bloque];
        if (actual.tamano == simulador.configuracion.memoria_total_kb) {
            break;   // ya es toda la memoria: no hay buddy
        }
        // Truco del buddy: la direccion de su companero es la propia XOR el tamano.
        // Ej: bloque de 64 en dir 0 -> buddy en 0^64 = 64; bloque en dir 64 -> buddy en 64^64 = 0.
        int direccion_buddy = actual.direccion ^ actual.tamano;   // XOR: la otra mitad del bloque padre
        int posicion_buddy = -1;
        for (int i = 0; i < (int)simulador.memoria.size(); i++) {
            if (simulador.memoria[i].direccion == direccion_buddy && simulador.memoria[i].tamano == actual.tamano) {
                posicion_buddy = i;
                break;
            }
        }
        if (posicion_buddy == -1) {
            // No hay bloque con esa direccion Y ese tamano: el buddy esta subdividido
            printf("El buddy (@%d) esta partido en bloques mas chicos: no se fusiona\n", direccion_buddy);
            break;
        }
        if (simulador.memoria[posicion_buddy].id_proceso != 0) {
            printf("El buddy (@%d) esta ocupado por el proceso %d: no se fusiona\n",
                   direccion_buddy, simulador.memoria[posicion_buddy].id_proceso);
            break;
        }
        // Ambos libres y del mismo tamano: se fusionan en el de la izquierda.
        int posicion_izquierda = posicion_bloque < posicion_buddy ? posicion_bloque : posicion_buddy;
        printf("Fusion: %d KB (@%d) + buddy %d KB (@%d) -> %d KB (@%d)\n",
               actual.tamano, actual.direccion,
               simulador.memoria[posicion_buddy].tamano, direccion_buddy,
               actual.tamano * 2, simulador.memoria[posicion_izquierda].direccion);
        simulador.memoria[posicion_izquierda].tamano = actual.tamano * 2;
        simulador.memoria.erase(simulador.memoria.begin() + posicion_izquierda + 1);  // desaparece el de la derecha
        posicion_bloque = posicion_izquierda;   // seguir intentando con el bloque ya fusionado
    }
}

// IMPLEMENTACION: ROUND ROBIN Y CICLO

// Atiende al proceso del frente de la cola: le da hasta un "cuanto" de CPU.
void atenderRoundRobin(Simulador &simulador) {
    if (simulador.cola_round_robin.empty()) {
        cout << "CPU libre: no hay procesos en la cola\n";
        cout << "Memoria al cierre del ciclo:\n";
        imprimirMemoria(simulador);
        return;
    }
    int indice = simulador.cola_round_robin.front();   // el primero de la fila
    simulador.cola_round_robin.pop_front();            // sale de la cola
    Proceso &proceso = simulador.procesos[indice];

    cout << "Atendiendo proceso ";
    imprimirProceso(simulador, indice);
    cout << "\n";

    // Usa el menor entre lo que le falta y el cuanto (si le falta poco, no gasta todo el cuanto).
    int cuanto_usado = proceso.cuanto_restante < simulador.configuracion.cuanto_sistema
                       ? proceso.cuanto_restante : simulador.configuracion.cuanto_sistema;
    int tiempo_inicio = simulador.reloj;
    proceso.cuanto_restante -= cuanto_usado;
    simulador.reloj += cuanto_usado;    // el reloj avanza solo cuando el CPU trabaja
    printf("  Usa el CPU del tiempo %d al %d (%d de %d unidades del cuanto). Le queda: %d\n",
           tiempo_inicio, simulador.reloj, cuanto_usado, simulador.configuracion.cuanto_sistema, proceso.cuanto_restante);

    if (proceso.cuanto_restante == 0) {
        // Termino: se registra su hora de fin y se libera su memoria.
        proceso.tiempo_fin = simulador.reloj;
        simulador.procesos_terminados++;
        cout << "Descargando proceso ";
        imprimirProceso(simulador, indice);
        cout << "\n";
        int posicion = liberarMemoria(simulador, proceso.id);
        if (simulador.configuracion.algoritmo == PEOR_AJUSTE) {
            condensarHuecos(simulador);
        } else {
            fusionarBuddies(simulador, posicion);
        }
    } else {
        // No termino: regresa al FINAL de la cola (esto es lo que hace "round robin").
        simulador.cola_round_robin.push_back(indice);
        cout << "  Se acabo el cuanto: el proceso regresa al final de la cola\n";
    }

    cout << "Lista Round Robin: ";
    imprimirColaRoundRobin(simulador);
    cout << "Memoria al cierre del ciclo:\n";
    imprimirMemoria(simulador);
}

// Un ciclo completo: (1) mostrar estado, (2) proceso nuevo o reintento,
// (3) intentar colocarlo en memoria, (4) registrar ocupacion, (5) atender CPU.
void ejecutarCiclo(Simulador &simulador) {
    imprimirEncabezadoCiclo(simulador);
    cout << "============== ESTADO ACTUAL ==============\n";
    cout << (simulador.configuracion.algoritmo == PEOR_AJUSTE ? " Peor ajuste:\n" : " Buddy System:\n");
    imprimirMemoria(simulador);

    int indice;
    if (simulador.indice_en_espera != -1) {
        // Si un proceso no cupo antes, NO se crea uno nuevo: se reintenta el mismo.
        indice = simulador.indice_en_espera;
        cout << "\n*Reintento (estaba en espera): ";
    } else {
        indice = generarProceso(simulador);
        cout << "\n*Nuevo proceso listo para entrar: ";
    }
    imprimirProceso(simulador, indice);
    cout << "\n";

    if (colocarEnMemoria(simulador, indice)) {
        simulador.indice_en_espera = -1;                   // ya nadie espera
        simulador.cola_round_robin.push_back(indice);      // entra a la cola del CPU
        printf("Proceso %d asignado\n", simulador.procesos[indice].id);
        imprimirMemoria(simulador);
    } else {
        simulador.indice_en_espera = indice;               // se queda esperando memoria
        simulador.veces_sin_memoria++;
        printf("No hay espacio para el proceso %d: queda en espera\n", simulador.procesos[indice].id);
    }

    // Se acumula la ocupacion de este ciclo ANTES de descargar procesos terminados.
    simulador.suma_ocupacion_kb += calcularMemoriaOcupada(simulador);

    cout << "\n[Cola RR]\n";
    imprimirColaRoundRobin(simulador);
    atenderRoundRobin(simulador);

    simulador.ciclo++;
}

// Suma el tamano de todos los bloques ocupados (en buddy cuenta el bloque entero, no lo pedido).
int calcularMemoriaOcupada(Simulador &simulador) {
    int ocupada = 0;
    for (int i = 0; i < (int)simulador.memoria.size(); i++) {
        if (simulador.memoria[i].id_proceso != 0) {
            ocupada += simulador.memoria[i].tamano;
        }
    }
    return ocupada;
}

// IMPLEMENTACION: IMPRESION

// Formato de bloque:
//   Peor ajuste: [id, tamano, cuanto_restante]                 (hueco: [0, tamano, 0])
//   Buddy:       [id, pedido/bloque @direccion, cuanto]        (hueco: [0, 0/bloque @direccion, 0])
void imprimirBloque(Simulador &simulador, Bloque bloque) {
    bool es_buddy = simulador.configuracion.algoritmo == BUDDY_SYSTEM;
    if (bloque.id_proceso == 0) {
        if (es_buddy) {
            printf("[0, 0/%d @%d, 0]", bloque.tamano, bloque.direccion);
        } else {
            printf("[0, %d, 0]", bloque.tamano);
        }
        return;
    }
    int cuanto = simulador.procesos[bloque.id_proceso - 1].cuanto_restante;  // id - 1 = posicion en el vector
    if (es_buddy) {
        printf("[%d, %d/%d @%d, %d]", bloque.id_proceso, bloque.tamano_pedido, bloque.tamano, bloque.direccion, cuanto);
    } else {
        printf("[%d, %d, %d]", bloque.id_proceso, bloque.tamano, cuanto);
    }
}

// Imprime un proceso con el formato correspondiente al algoritmo.
void imprimirProceso(Simulador &simulador, int indice_proceso) {
    Proceso &proceso = simulador.procesos[indice_proceso];
    if (simulador.configuracion.algoritmo == BUDDY_SYSTEM) {
        printf("[%d, %d/%d, %d]", proceso.id, proceso.tamano_pedido,
               siguientePotenciaDeDos(proceso.tamano_pedido), proceso.cuanto_restante);
    } else {
        printf("[%d, %d, %d]", proceso.id, proceso.tamano_pedido, proceso.cuanto_restante);
    }
}

// Imprime toda la memoria, bloque por bloque, en una sola linea.
void imprimirMemoria(Simulador &simulador) {
    for (int i = 0; i < (int)simulador.memoria.size(); i++) {
        imprimirBloque(simulador, simulador.memoria[i]);
    }
    cout << "\n";
}

// Imprime la cola RR (del frente hacia el final).
void imprimirColaRoundRobin(Simulador &simulador) {
    if (simulador.cola_round_robin.empty()) {
        cout << "(vacia)\n";
        return;
    }
    for (int i = 0; i < (int)simulador.cola_round_robin.size(); i++) {
        imprimirProceso(simulador, simulador.cola_round_robin[i]);
    }
    cout << "\n";
}

void imprimirEncabezadoCiclo(Simulador &simulador) {
    cout << "\n--------------------------------\n";
    printf("** CICLO %d **   (t = %d)\n", simulador.ciclo, simulador.reloj);
    cout << "--------------------------------\n\n";
}

// Resumen final con tabla por proceso y promedios.
void imprimirEstadisticas(Simulador &simulador) {
    int ciclos_ejecutados = simulador.ciclo - 1;   // ciclo siempre apunta al SIGUIENTE
    cout << "\n================ ESTADISTICAS FINALES ================\n";
    cout << "Algoritmo: " << (simulador.configuracion.algoritmo == PEOR_AJUSTE ? "Peor ajuste" : "Buddy System") << "\n";
    printf("Memoria: %d KB   Cuanto del sistema: %d\n", simulador.configuracion.memoria_total_kb, simulador.configuracion.cuanto_sistema);
    printf("Ciclos ejecutados: %d\n", ciclos_ejecutados);
    printf("Tiempo total de CPU: %d\n", simulador.reloj);
    printf("Procesos generados: %d\n", (int)simulador.procesos.size());
    printf("Procesos atendidos (terminados): %d\n", simulador.procesos_terminados);
    printf("Veces que un proceso no cupo en memoria: %d\n", simulador.veces_sin_memoria);

    double suma_retorno = 0, suma_espera = 0;
    cout << "\nProceso  Llega  Rafaga  Termina  Retorno  Espera\n";
    for (int i = 0; i < (int)simulador.procesos.size(); i++) {
        Proceso &proceso = simulador.procesos[i];
        if (proceso.tiempo_fin < 0) {
            continue;   // los que no terminaron no tienen tiempos de retorno/espera
        }
        // Retorno (turnaround) = fin - llegada. Espera = retorno - tiempo que uso el CPU.
        int retorno = proceso.tiempo_fin - proceso.tiempo_llegada;
        int espera = retorno - proceso.rafaga_total;
        suma_retorno += retorno;
        suma_espera += espera;
        printf("P%-7d %-6d %-7d %-8d %-8d %d\n", proceso.id, proceso.tiempo_llegada, proceso.rafaga_total,
               proceso.tiempo_fin, retorno, espera);
    }
    if (simulador.procesos_terminados > 0) {
        printf("\nTiempo medio de atencion (retorno): %.2f\n", suma_retorno / simulador.procesos_terminados);
        printf("Tiempo promedio de espera:          %.2f\n", suma_espera / simulador.procesos_terminados);
    } else {
        cout << "\nNingun proceso termino: no hay tiempos promedio.\n";
    }
    if (ciclos_ejecutados > 0) {
        // % = (suma de KB ocupados / ciclos) / memoria total * 100
        double ocupacion = 100.0 * simulador.suma_ocupacion_kb / ciclos_ejecutados / simulador.configuracion.memoria_total_kb;
        printf("Porcentaje promedio de ocupacion de memoria: %.2f %%\n", ocupacion);
    }
    cout << "=======================================================\n";
}

// IMPLEMENTACION: VELOCIDAD Y TECLADO

// Espera automatica entre ciclos: duerme "retardo_ms" en rebanadas de 50 ms
// para detectar de inmediato si el usuario presiona Q, ESC, P, + o -.
void esperarSiguienteCiclo(Simulador &simulador) {
    cout << flush;   // forzar a que todo lo impreso salga a pantalla antes de esperar
    int esperado = 0;
    while (esperado < simulador.configuracion.retardo_ms && !simulador.salir) {
        dormir(50);
        esperado += 50;
        revisarTeclado(simulador);
    }
}

// Procesa las teclas de control sin bloquear el programa.
void revisarTeclado(Simulador &simulador) {
    while (hayTeclaPresionada()) {
        int tecla = leerTecla();
        if (tecla == 27 || tecla == 'q' || tecla == 'Q') {   // ESC (27) o Q detienen la simulacion
            simulador.salir = true;
            cout << "\n>> Simulacion detenida por el usuario.\n";
        } else if (tecla == '+') {
            // Mas rapido: retardo a la mitad, con piso de 50 ms
            simulador.configuracion.retardo_ms = simulador.configuracion.retardo_ms / 2 < 50 ? 50 : simulador.configuracion.retardo_ms / 2;
            printf("\n>> Velocidad: %d ms por ciclo\n", simulador.configuracion.retardo_ms);
        } else if (tecla == '-') {
            // Mas lento: retardo al doble, con techo de 5000 ms
            simulador.configuracion.retardo_ms = simulador.configuracion.retardo_ms * 2 > 5000 ? 5000 : simulador.configuracion.retardo_ms * 2;
            printf("\n>> Velocidad: %d ms por ciclo\n", simulador.configuracion.retardo_ms);
        } else if (tecla == 'p' || tecla == 'P') {
            cout << "\n>> PAUSA. Presiona P para reanudar o Q / ESC para detener la simulacion\n";
            while (true) {   // bucle de pausa: bloquea leyendo teclas
                int otra = leerTecla();
                if (otra == 'p' || otra == 'P') {
                    cout << ">> Reanudando...\n";
                    break;
                }
                if (otra == 27 || otra == 'q' || otra == 'Q' || otra == -1) {
                    simulador.salir = true;
                    cout << ">> Simulacion detenida por el usuario.\n";
                    break;
                }
            }
        }
    }
}

bool hayTeclaPresionada() {
    return _kbhit() != 0;   // hay una tecla en el buffer?
}

int leerTecla() {
    return _getch();        // lee una tecla sin esperar Enter
}

void dormir(int milisegundos) {
    Sleep(milisegundos);
}