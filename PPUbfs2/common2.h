#include <math.h>

#ifndef __COMMON2__
#define __COMMON2__



/*-----------------------------------------------------------------------------------*/
/*----------------------ATTIVAZIONE DI DETERMINATE PARTI DI CODICE-------------------*/
#define PROFILING	   1  //per vedere i tempi delle singole parti di codice delle SPE
//#define VERBOSE 1   //per vedere maggiori dettagli durante l'esecuzione dell'algoritmo
/*-----------------------------------------------------------------------------------*/
/*-----------------------------------------------------------------------------------*/





/*-----------------------------------------------------------------------------------*/
/*----------------------------GRAFO DA CALCOLARE-------------------------------------*/
/*parametri del grafo*/
#define IVI      950*6	  // Numero dei vertici del grafo, ovvero cardinalità di V (IVI sarebbe valore assoluto di V)
#define IViI    (int)(ceil((float)IVI / SPU_THREADS))	// Numero dei vertici assegnati ad ogni SPE, ovvero cardinalità di Vi
						//notare che uso ceil che arrotonda per eccesso: per esempio, se ho IVI=64, IViI diventa = 10,16, che ceil 
						//arrotonda a 11. Poi l'ultima SPE riceverà 6 nodi. Se invece arrotondassi per difetto, otterrei che 5 spe 
						//avrebbero 10 nodi e l'ultima 14 nodi. Quindi, considerando che il programma finisce quando finisce 
						//l'ultima SPE,mentre nel primo caso nessuna ha più di 11 nodi, nel secondo caso solo una ne 
						// ha 14, rallentando l'intero programma. 
						//NB: in certi casi, se per esempio ho 20 nodi, all'ultima SPE non vengono assegnati vertici 
						//perché 20/6 = 3,... arrotondato = 4. Però 4 * 5= 20. Diciamo che è una falla del metodo
						//dell'arrotondamento. sono casi limite quindi lascio così.

#define IEvI_max    5		// Grado massimo dei nodi, cioè l'Arity massima. Questo limite è sempre rispettato
#define IEvI_min    2	       	// Grado minimo dei nodi, cioè l'Arity minima (potrebbe non essere rispettata)

#define ROOT    0				// radice


/*struttura dati che definisce un vertice. Totale di 16 bytes*/
/*Notare che non ho un campo per il valore dato che, dovendo solo visitare l'intero grafo, sarebbe inutile*/
typedef struct nodo {
	unsigned int numero;   //Numero del nodo.
	unsigned int adj_length;
	unsigned int padding[2];
} vertice;

//il seguente per i vettori delle adiacenze. ovviamente questi devono essere di lunghezza pari al massimo di adiacenze consentite. Questa lunghezza però deve essere allineata a 4 in modo 
//che la grandezza del vettore, dato che è fatto di unsigned int, sia allineata a 16
#define IEvI_max_aligned16    (IEvI_max +  (4 - (IEvI_max % 4))) // è il primo numero maggiore di EVimax e allineato a 16 
													    
//il seguente è usato per le code Qout e Qin
//nel peggiore dei casi, ovvero se : - tutti i nodi di un vertice vengono visitati allo stesso livello -ogni nodo ha il massimo di adiacenze -tutte le adiacenze sono nello stesso livello
//allora in questo caso una coda deve essere di adj_max_aligned. ovviamente questo è l'upper bound, e penso sia un caso più unico che raro
#define adj_max_aligned16    ( (IViI*IEvI_max) + (4 - ( (IViI*IEvI_max) % 4)) ) //è il primo numero maggiore di (IViI*IEvI_max) e allineato a 4.Il fatto che sia allineato a 4 fa si che,
																			 //considerando che viene usato con code di unsigned int, quando carico 4 elementi alla volta carico 16bytes.
																			 //quindi, avendo questo numero allineato a 4, la coda viene allineata a 16.
/*-----------------------------------------------------------------------------------*/
/*-----------------------------------------------------------------------------------*/





/*-----------------------------------------------------------------------------------*/
/*-----------------------------STRUTTURE DATI DI BASE PER CELL-----------------------*/

#define SPU_THREADS 6		// Numero degli SPE utilizzati, nella ps3 i disponibili sono 6

/*Struct con i parametri per le SPEs. Totale di 48 bytes*/
typedef struct params_t {
	unsigned int id;
	unsigned int Gi_addr;   //puntatore alla parte di grafo che deve essere analizzato dalla SPE
  	unsigned int adj_addr;   //puntatore al vettore delle adiacenze
	unsigned int root;  //una sola SPE avrà il root tra i suoi vertici
  	unsigned int local_stores[SPU_THREADS];  //per DMA SPE-SPE
	unsigned int padding[8 - SPU_THREADS]; //riempio la struct per arrivare a riempire 48 bytes
} params;

/*-----------------------------------------------------------------------------------*/
/*-----------------------------------------------------------------------------------*/






/*-----------------------------------------------------------------------------------*/
/*----------------------------PROFILING----------------------------------------------*/
//define per il profiling
#define DECR_MAX  0xFFFFFFFF
#define DECR_COUNT   DECR_MAX
#define TIMEBASE 79800000		// Usato per prendere i tempi (viene dal /proc/cpuinfo)
/*-----------------------------------------------------------------------------------*/
/*-----------------------------------------------------------------------------------*/


#endif
