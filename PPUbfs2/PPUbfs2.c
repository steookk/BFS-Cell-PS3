/*PPU bfs versione 1.5. è identico alla versione 1.*/

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <libspe2.h>
#include <pthread.h>
#include <math.h>
#include <sys/time.h>

#include "common2.h"



/*********************GLOBAL VARIABLES**************************/
extern spe_program_handle_t SPUbfs2;

//struct contente cose specifiche del cell, ovvero contexts, threads ecc
//questa struct è utilizzata solo dal PPE, non va alla SPE
typedef struct thread_spu{
  spe_context_ptr_t spe_ctx;
  pthread_t pthread;
  void *argp;
  unsigned int entry;
} thread_spu;

//------------------------------------------------------------------------
//Cell specific
thread_spu threadSpu[SPU_THREADS] __attribute__ ((aligned(128)));    //vettore di struct con cose specifiche del Cell
params parameters[SPU_THREADS] __attribute__ ((aligned(128)));  //vettore di struct con i dati da mandare alla SPEs
//------------------------------------------------------------------------

//------------------------------------------------------------------------
//Graph specific
vertice G[IVI] __attribute__ ((aligned (128)));  //vettore che contiene per ogni vertice la struttura che lo definisce. Ogni SPE riceve una parte di esso.
unsigned int adj[IVI][IEvI_max_aligned16] __attribute__ ((aligned (16))); // vettore che contiene la lista delle adiacenze per ogni vertice. Viene passato alle SPE
//------------------------------------------------------------------------


/*********************HEADERS**************************/
void *ppu_pthread_function(void *);
void generazioneGrafo (); 


/*********************MAIN**************************/
int main () 
{ 
	unsigned int j, spe = 0;
    	
	struct timeval tv_start, tv_end;
	
	
	//------------------------------------------------------------------------
	//----------------------GENERAZIONE DEI DATI------------------------------
	generazioneGrafo(G, adj);
	//------------------------------------------------------------------------

#ifdef VERBOSE
	
	unsigned int i;
	//---------------mostro il grafo (per ogni vertice le sue adiacenze) partendo dal nodo 0-------------------------
    for (i = 0; i < IVI; i++) 
    {
        printf("\nIl vertice %d e' collegato ai nodi: ", i);
        fflush(stdout);
    
        for (j = 0; j < G[i].adj_length; j++)
        {
        	printf(" %d, ", adj[i][j]);
        	fflush(stdout);
        }
    fflush(stdout);
    }
    

    //--------------informazioni su quanto spazio occupa il grafo---------------------------
    printf ("\n\nLa struttura contenente il grafo è di %d bytes", sizeof(G));
    printf ("\nLa struttura contenente le adiacenze è di %d bytes",sizeof(adj));
    
    
    //--------------mostro la suddivisione dei vertici tra le varie SPEs--------------------
    printf ("\n\nA ogni SPE sono stati assegnati num %d vertici", (int) IViI);
    
    for (i=0; i< SPU_THREADS -1 ; i++)
    	printf ("\nLa spe %d : Dal vertice %d al vertice %d",i ,i*IViI ,(i+1)*IViI -1 ); //per tutte le SPE tranne l'ultima
    printf ("\nLa spe %d : Dal vertice %d al vertice %d", i,i*IViI , (int)IVI-1 );  //l'ultima SPE può avere meno vertici se il numero totale di vertici non è multiplo di 6
    	//in quei rari casi come IVI=20 , dove l'ultima SPE non riceve vertici, il printf fa errore in quanto dice che 
    	//i nodi assegnati vanno dal 20 al 19, cosa non vera ovviamente. Essendo un caso limite, non lo considero.
#endif 
	//------------------------------------------------------------------------
	//------------------------------------------------------------------------

	  
	//------------------------------------------------------------------------
	//-----------------CREAZIONE DELLE SPEs-----------------------------------
	for (spe = 0; spe < SPU_THREADS; spe++) 
	{
		//----------dati da mandare alla SPEs-----------------------------------
	    parameters[spe].id = (unsigned int) spe;
	    parameters[spe].Gi_addr = (unsigned int) &(G[IViI * spe]);
	    parameters[spe].adj_addr = (unsigned int) &adj[IViI * spe][0];
	    parameters[spe].root = (unsigned int) ROOT;
	    
		//------creazione dei contexts--------------------------------------------
	    if((threadSpu[spe].spe_ctx = spe_context_create(SPE_MAP_PS, NULL)) == NULL)
	    {
	      perror ("Failed creating context");
	      exit(1);
	    }
	    
	    
	    //-------carico il programma nel contesto---------
	    if (spe_program_load (threadSpu[spe].spe_ctx, &SPUbfs2))
	    {
	   	      perror ("Failed loading program");
	   	      exit (1);
	   	}
	}
	
	for (spe = 0; spe < SPU_THREADS; spe++) 
	{
	    //------assegno gli indirizzi dei local store ai vari SPE-----------------
	    for(j = 0; j < SPU_THREADS; j++)
	    	 parameters[spe].local_stores[j] = (unsigned int) spe_ls_area_get(threadSpu[j].spe_ctx);
	    
	    //-------passo i dati alle SPE-----------------
	    threadSpu[spe].entry = SPE_DEFAULT_ENTRY;
	    threadSpu[spe].argp = &parameters[spe];   //notare che invece di mandare la mail come fatto nei progetti di prova,
	  										  //l'indirizzo della struct params lo passo direttamente come argomento al mail delle SPEs
	  	
	  	//------creo un thread per ogni context--------------
	    if(pthread_create(&threadSpu[spe].pthread, NULL, &ppu_pthread_function, &threadSpu[spe]))
	  	{
	  	      perror ("Failed creating thread");
	  	      exit (1);
	  	}
	}
	//-----------------------------------------------------------------------------------------------
	
	
	//Faccio partire il timing
	gettimeofday(&tv_start, NULL);
	
	
	  
    //-------------------------------------------------------------
    //-----------------PARTE DI TERMINAZIONE-----------------------
    /* Wait for SPU-thread to complete execution.  */
    for (spe=0; spe<SPU_THREADS; spe++)
    {
        if (pthread_join (threadSpu[spe].pthread, NULL)) 
        {
             perror("Failed pthread_join");
             exit (1);
        }
        
     /* Destroy context */
        if (spe_context_destroy (threadSpu[spe].spe_ctx) != 0) 
        {
  	       perror("Failed destroying context");
             exit (1);
        }
    }
    
    //------------------------------------------------------------------------
  
    //------------------------------------------------------------------------
    //----------------CALCOLO DEL TIMING e info di base per l'utente------------------------------
    gettimeofday(&tv_end, NULL);
    double secs = (tv_end.tv_sec - tv_start.tv_sec) + (double)(tv_end.tv_usec - tv_start.tv_usec)/1E6;
  
    printf ("\nIl grafo è composto da %d vertici ", IVI );
    printf ("\nIl grado minimo è %d, quello massimo è %d ", IEvI_min, IEvI_max);
    printf ("\nIl root è il vertice numero %d", parameters[0].root);
    printf ("\nTempo totale di esecuzione (sec): %5.15f\n",secs);

    //------------------------------------------------------------------------

    return 0;
}




/*questa funzione genera grafi random. Viene sempre utilizzata dato che all'applicazione non si possono dare come input grafi già preparati*/
void generazioneGrafo () 
{
	unsigned int i,j,k,h;
		
	unsigned int numadj;   //vettore del numero di adiacenze: per ogni vertice viene indicato il numero di adiacenze

	//metto numeri casuali nel vettore delle adiacenze
	//il ciclo esterno sono i vertici
	for (i=0; i<IVI; i++)
	{
		G[i].numero = i;  //ogni vertice ha come id il suo numero
		//il ciclo interno sono le adiacenze (casuali) che per ogni vertice sono di numero casuale compreso tra il minimo
		//e il massimo dell'arità
		numadj = (unsigned int) (rand() % (IEvI_max-IEvI_min+1)) + IEvI_min ;
		G[i].adj_length = numadj; //ogni vertice tiene traccia di quante adiacenze ha

		for (j=0; j <  numadj; j++)
			adj[i][j] = (unsigned int) rand() % (IVI) ;  // adiacenza che ovviamente è un vertice (quindi tra 0 e IVI-1)
	}	

	  
	/*adesso devo aggiustare le adiacenze, infatti vengono considerate solo le adiacenze
	 * che sono indicate dal nodo richiesto. (es: le adiacenze di 0). Alla adiacenze di 0 dovrei aggiungere quei 
	 * vertici di cui 0 è adiacenza. (es: se il vertice 6 ha come adiacenza lo 0, allora anche 6 deve fare parte 
	 * delle adiacenze di 0 anche se a random non c'era).*/
	for (i=0; i<IVI; i++)  //prendo un vertice alla volta.. (es: prendo 0)
	{
		for (j=0; j<G[i].adj_length; j++)  //.. e per ogni vertice percorro tutte le adiacenze (cioè tutte le adiacenze di 0)
		{
			k = adj[i][j];   //prendo un adiacenza (per es: 6)
			for (h=0; h<G[k].adj_length; h++)  //vado dentro il vertice adiacenza (es: se l'adiacenza era 6 prendo il vertice 6)
			{								  //e scorro le sue adiacenze (h scorre le adiacenze)
				if ( adj[k][h] == i )  //cerco se il vertice k ha una adiacenza h che corrisponda a i (cioè a 0)
					break;      //se ho trovato l'adiacenza sono già a posto
				else             //se l'adiacenza non corrisponde ripeto il ciclo fino a che..
				{
					if ( h == G[k].adj_length-1 ) //..siccome anche l'ultimo h non aveva il valore cercato (es:0), allora devo..
					{
						if ( G[k].adj_length == IEvI_max )  //..o rimuovere questa adiacenza da i se k ha già il massimo di adiacenze
						{				
							G[i].adj_length--;
							adj[i][j] = adj[i][G[i].adj_length]; //sposto l'ultima adiacenza al posto di quella da rimuovere
							//adj[i][G[i].adj_length] = -1;  //siccome le coda si è accorciata marchio l'ultimo elemento  con -1. non posso perché uso gli unsigned int
							j --;  //faccio ripetere il ciclo con la stessa j dato che adesso in questa posizione è cambiata l'adiacenza
						}
						
						else // ..oppure aggiungere l'adiacenza i a k se c'è spazio per nuove adiacenze
						{
							adj[k][G[k].adj_length] = i;  //aggiungo l'adiacenza i alla fine della coda
							G[k].adj_length++;  //incremento il numero delle adiacenze di k
						}
					}
				}
					
			}
				
		}
	}
	
	/*adesso ripasso tutto il grafo in cerca di quei vertici che sono rimasti senza adiacenze (dato che succede sempre che qualcuno rimanga senza)
	*/
	for (i=0; i<IVI; i++)
	{
		if ( G[i].adj_length == 0 )
		{
			//prima rimetto delle adiacenze casuali..
			numadj = (unsigned int) (rand() % (IEvI_max-IEvI_min+1)) + IEvI_min ;
			G[i].adj_length = numadj;

			for (j=0; j <  numadj; j++)
				adj[i][j] = (unsigned int) rand() % (IVI) ; 
			
			//.. e adesso aggiungo i ai vertici suoi adiacenti (k)
			for (j=0; j<G[i].adj_length; j++)
					{
						k = adj[i][j]; 
						
						if ( G[k].adj_length < IEvI_max)  //nel caso la coda delle adiacenze di k non sia piena
						{
							adj[k][G[k].adj_length] = i;  //aggiungo l'adiacenza i alla fine della coda
							G[k].adj_length++;  //incremento il numero delle adiacenze di k
						}
						else    //invece se è piena lo sostituisco con un altro numero casuale
						{
							adj[i][j] = (unsigned int) rand() % (IVI) ;
							j--;  //faccio in modo che l'iterazione venga ripetuta in modo da rifare l'operazione per questo nuovo num casuale
						}
						
					}	
						
		}
		
	}
	
		/*altro da eventualmente aggiungere 
		 * - certe adiacenze sono il vertice stesso (0 ha come adiacenza 0). Lo lascio così
		 * - tra la adiacenze ci sono due vertici uguali (0 ha come adicenze 2 e 2). Lo lascio così
		 * - i vertici possono avere meno adiacenze del numero minimo. Lo lascio così
		 * */
}


void *ppu_pthread_function(void *arg) 
{
    thread_spu *datap = (thread_spu *) arg;
	  
    if (spe_context_run(datap -> spe_ctx, &datap -> entry, 0, datap -> argp, NULL, NULL) )
    {
	      perror ("Failed running context");
	      exit (1);
    }
 
    pthread_exit(NULL);
}

