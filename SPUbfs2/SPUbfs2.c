
/*SPU bfs versione 1.5. Rispetto alla versione 1 qui vengono implementati i buffer (con double buffering) e le DMA list per i trasferimenti
 * DMA.
 * */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <spu_mfcio.h>
#include <spu_intrinsics.h>

#include "/home/ste/workspace/PPUbfs2/common2.h"



#define INT_SIZE_BITS  				32   //cioè quanto bit ci sono in un intero
#define MARKED_SIZE  				(int)(ceil((float)IViI / INT_SIZE_BITS))  //cioè di quanti elementi è fatto il vettore marked 
#define BUFFER_SIZE_BYTES        512	               //size del buffer in bytes
#define BUFFER_SIZE    			(BUFFER_SIZE_BYTES / (sizeof(vertice)))   //elementi del buffer dove carico i vertici
#define ADJ_MAX_ALLOWED_BYTES      128
#define ADJ_MAX_ALLOWED   			(ADJ_MAX_ALLOWED_BYTES / sizeof(unsigned int))   //elementi del buffer dove carico le adiacenze
#define ADJ_NUM_TRASFERIMENTI       (ADJ_MAX_ALLOWED/IEvI_max_aligned16)



//struct per la sincronizzazione della fase di commit
typedef struct sync {
	unsigned int received; //Usato per la sincronizzazione di all to all e commit. quando è =1 il dato è stato scritto
	unsigned int count; //nel commit ogni SPE ci scrive quanti elementi ha ancora in Qi . Non usato nella alltoall
	unsigned int padding[2];
} sync;


//struct per le DMA LIST
typedef struct dma_list_elem {
	unsigned int all32;     //quantità di dati da trasferire
	unsigned int ea_low ;  //effective address, ovvero l'indirizzo dell'elemento nella Ram
	unsigned int padding[2];
} dma_list;

//creazione delle DMA LIST
dma_list list_alltoall[SPU_THREADS]  __attribute__ ((aligned(16)));
dma_list list_commit[SPU_THREADS]  __attribute__ ((aligned(16)));

dma_list list_fetch[2][BUFFER_SIZE] __attribute__ ((aligned(16)));
dma_list list_gather[2][ADJ_NUM_TRASFERIMENTI] __attribute__((aligned(16)));


//--------------------------------------------------------------------
//Code e buffers
//NB: le code contengono solo numeri che indicano quali sono i vertici da caricare, non contengono i vertici
//Qi e Qnexti non hanno a che fare con i DMA, quindi non serve allinearli
unsigned int Qi[IViI]; //vertici da esplorare nel livello corrente.NB:vengono marcati True nel livello precedente. La dimensione di Qi deve essere 4, 8 o multiplo di 16
unsigned int Qnexti[IViI]; //vertici da esplorare al livello successivo.Ovvero sono appena stati marcati True
unsigned int Qout[SPU_THREADS][adj_max_aligned16] __attribute__ ((aligned(16)));  //code per i vertici che dovranno essere esplorati,sia da questa SPE che dalle altre SPEs
volatile unsigned int Qin[SPU_THREADS][adj_max_aligned16] __attribute__ ((aligned(16))); //code di entrata, qui arrivano i vertici mandati dalle altre SPEs

//vertici e adiacenze caricati per essere visitati
volatile vertice bQi[2][BUFFER_SIZE] __attribute__ ((aligned(16)));  //double buffer dove carico i vertici dalla ram
volatile unsigned int bGi[2][ADJ_MAX_ALLOWED] __attribute__ ((aligned(16)));  //double buffer dove carico le adiacenze dalla ram

unsigned int spu_id;
//-----------------------------------------------------------------------


//------------------------------------------------------------------------
//Cell specific
volatile params parameters __attribute__ ((aligned(128)));  //è la struct passata dal PPU
unsigned int addr __attribute__ ((aligned(128)));
//------------------------------------------------------------------------

#ifdef  PROFILING
			//variabili per cronometrare il tempo
			unsigned int t_start, t_fetch_start, 
						t_allall_start ,t_allall_intermediate,t_allall_start2,
						t_bitmap_start, t_commit_start,t_commit_intermediate,t_commit_start2,t_commit_end, t_end;
		
			//variabili per i tempi finali, a ogni iterazione (cioè a ogni incremento del livello di visita del grafo) vengono incrementati
			unsigned int totaltime=0, inizializzazione=0, fgd=0, alltoall=0, syncroAll=0,bitmap=0,
						 commit=0, syncroComm=0;
						
#endif

			

void fetch_();
void gather_ (int, unsigned int);
void dispatch_ (int , int , unsigned int , unsigned int);


//l'argomento argp è l'indirizzo della struct parameters che l'SPE deve scaricare con DMA
int main(unsigned long long speid, unsigned long long argp)
{			
	
#ifdef PROFILING
	
	//----------------------------------------------------
	/*inizio il profiling*/
	spu_writech (SPU_WrDec, DECR_COUNT);
	spu_writech (SPU_WrEventMask, MFC_DECREMENTER_EVENT);
				
	t_start = spu_readch (SPU_RdDec);
	//----------------------------------------------------- 	
#endif
	
	
	//--------------------------------------------------------------------------------------------------
	//----------------------VARIABILI------------------------------------------------------------
	unsigned int i,j;
	
	unsigned int tag = 1 , chk = 0;
	
	

	//communications guards (sicronizzazione)
	volatile sync flags_alltoall[SPU_THREADS] __attribute__ ((aligned(16))); 
	volatile sync flags_commit[SPU_THREADS] __attribute__ ((aligned(16)));
	sync flag[SPU_THREADS] __attribute__ ((aligned(16)));   //devo fare un vettore di flag invece che una solo perché così necessita la DMA list
	for (i=0; i<SPU_THREADS; i++)
	{
		flag[i].received = 1;
		flag[i].count = 0;
		flag[i].padding[0] = 0;
		flag[i].padding[1] = 0;
	
	}
	
	//variabili usate nel bitmap (cioè quando marco i vettori)
	unsigned int marked[MARKED_SIZE]; //Vettore che mappa in ogni bit se un nodo è stato visitato o meno
	unsigned int mask = 1;   //maschera per scrivere bit a bit nel vettore marked.
	unsigned int id_for_spe, pos, offset, temp;
	
	//variabile che controlla il ciclo do-while
	unsigned int toContinue = 0;  
	//--------------------------------------------------------------------------------------------------
	//----------------------fine variabili------------------------------------------------------------

	
	
	//--------------------------------------------------------------------------------------------------
	//----------------------INIZIALIZZAZIONE------------------------------------------------------------
	//--------------------------------------------------------------------------------------------		
	//prendo la struct parameters con DMA
	mfc_get(&parameters, argp, sizeof(params), tag, 0, 0);
	mfc_write_tag_mask (1 << tag);
	mfc_read_tag_status_all();

    spu_id = parameters.id;
	//--------------------------------------------------------------------------------------------		
		
    
    //--------------------------------------------------------------------------------------------
	//preparo la lista per il DMA list
	for (i=0; i<SPU_THREADS; i++)
	{
		list_alltoall[i].all32 = sizeof(sync);
		list_alltoall[i].ea_low = ((unsigned int)parameters.local_stores[i]) + ((unsigned int)&(flags_alltoall[spu_id]));
		list_alltoall[i].padding[0] = 0;
		list_alltoall[i].padding[1] = 0;
	}
	for (i=0; i<SPU_THREADS; i++)
	{
		list_commit[i].all32 = sizeof(sync);
		list_commit[i].ea_low = ((unsigned int)parameters.local_stores[i]) + ((unsigned int)&(flags_commit[spu_id]));
		list_commit[i].padding[0] = 0;
		list_commit[i].padding[1] = 0;
	}
	//--------------------------------------------------------------------------------------------
	
	
    //livello della visita del grafo, ovviamente si parte dal livello 0
	unsigned int level = 0;
		
	//Il primo elemento di Qi indica il numero di vertici presenti nella coda
	Qi[0] = 0;
	
	//inizializzo a 0 il vettore marked
	for (i=0; i<MARKED_SIZE; i++ )
	{
		marked[i] = 0;
	}

	
	//-----------------------------------------------------------------------------------------------
	//guardo se root appartiene all'intervallo dei vettori assegnati a questa SPE
	if ( parameters.root >= spu_id*IViI  &&  parameters.root < (spu_id+1)*IViI )
	{
		Qi[0] = 1;
		Qi[1] = parameters.root;
		
		//trovo dove devo settare il bit
		pos = (parameters.root % IViI) / INT_SIZE_BITS ; //posizione
		offset = parameters.root % INT_SIZE_BITS;  //offset
				
		//creo la maschera
		mask = mask<<offset;  //ho spostato il bit dalla posizione 0 alla posizione che corrisponde
										     //al numero del vertice
						
		temp = marked[pos] | mask;
		
		marked [pos] = temp ;
		
		mask = mask>>offset;	
		
#ifdef VERBOSE
		//stampo che il root è stato marcato
		printf ("\n\n--------(SPE %d): Il root è il nodo %d ed è stato marcato -----\n",spu_id, parameters.root);
#endif
	}
	//---------------------------------------------------------------------------------------------------
	//----------------------------fine inizializzazione-------------------------------------------
	//--------------------------------------------------------------------------------------------
	
	
#ifdef  PROFILING
	//calcolo il tempo dell'inizializzazione, dato che si fa solo una volta
	t_end = spu_readch(SPU_RdDec);
	inizializzazione = (t_start - t_end) ;
#endif
	
	
	
	do {
		
//PARTE 1,2,3--------------------------FETCH,GATHER,DISPATCH------------------------------------
		//nel fetch viene presa con DMA la struttura che rappresenta il vertice il cui id è in Qi.
#ifdef PROFILING
			t_fetch_start = spu_readch(SPU_RdDec);
#endif
			
		//Vuoto le code Qout per ogni SPE
		for(i = 0; i < SPU_THREADS; i++) 
			Qout[i][0] = 0;
		//anche se così è sufficiente per il funzionamento, sarebbe meglio azzerare le code completamente
		
		//Vuoto Qnext
		Qnexti[0] = 0;
			
		//faccio il fetch solo se ci sono elementi nella coda	
		if (Qi[0] != 0)  
		{
			fetch_ ();   //il fetch chiama il GATHER, e il gather chiama il DISPATCH. poi il controllo torna al main
			
		}
			

		
//PARTE 4-----------------------ALL to ALL--------------------------------------
		//nell'all to all le code di uscita vengono scritte nel Local store delle rispettive SPEs (vengono direttamente sovrascritte le Qin) 
#ifdef PROFILING
			t_allall_start = spu_readch(SPU_RdDec);
#endif	
		
		unsigned int size; 
		//------------------Qout-->Qin---------------------------------------
		//invio i vertici alle altre SPE
		for(i = 0; i < SPU_THREADS; i++) 
		{
			if (i != spu_id) 
			{
				size =  (Qout[i][0]) + (4-((Qout[i][0])%4));  //numero di elementi della Qout da scrivere in Qin allineato a 4, in modo che il dma sia allineato a 16
				size = sizeof(unsigned int) * size;   //questo è allineato a 16 e trasferisce solo il minimo necessario. avrei potuto trasferire l'intera coda che è già allineata a 16
													  //ma è uno spreco di tempo considerando che il più delle volte gli elementi sono molto meno rispetto alla lunghezza della coda
				//più precisamente scrivo nelle code di entrata Qin delle altre SPE
				mfc_put(&(Qout[i][0]), ((unsigned int)parameters.local_stores[i]) + ((unsigned int)&(Qin[spu_id][0])), size , 2, 0, 0);
				mfc_write_tag_mask(1<<2);
				mfc_read_tag_status_all();
			}
		}

		//Copio la coda Qout sulla coda Qin
		for(j = 0; j <= Qout[spu_id][0]; j++) 
			Qin[spu_id][j] = Qout[spu_id][j];
		//-------------------------------------------------------
		

		//------------------Sincronizzazione con communication guards-----------------
		//dopo la sync tutte le SPE avranno le code Qin pronte per l'utilizzo
		
		//DMA SPE-SPE : Invio i flag per assicurarmi che siano stati scritti i dati da tutti i processori
		mfc_putlf(flag, ((unsigned int)parameters.local_stores[0]) + ((unsigned int)&(flags_alltoall[spu_id])) ,
							(unsigned int)list_alltoall ,SPU_THREADS*sizeof(dma_list), 2, 0, 0);
		mfc_write_tag_mask(1<<2);
		mfc_read_tag_status_all();
			
#ifdef PROFILING   
		//la parte che blocca la SPE in attesa delle altre non la considero parte della computazione quindi la tengo fuori dal cronometro
			t_allall_intermediate = spu_readch(SPU_RdDec);
#endif	
			
		//questo è il blocco: si rimane nel while fino a che tutte le altre SPEs non hanno scritto 1 nel flag
		chk = 0;
		while(chk < SPU_THREADS)
		{
			chk = 0;
			//printf ("\nSPE %d sincronizzazione ALL to all", spu_id);
			for(i = 0; i < SPU_THREADS; i++)
				if(flags_alltoall[i].received == 1) 
					chk++;
		}
#ifdef PROFILING   
		//la parte che blocca la SPE in attesa delle altre non la considero parte della computazione quindi la tengo fuori dal cronometro
			t_allall_start2 = spu_readch(SPU_RdDec);
#endif	
		//printf ("\nSPE %d ho finito la all to all", spu_id);
		//Riazzero i flag received
		//NB: è importante farlo qui e non prima di mandare i flag, infatti un altra SPE potrebbe inviare il flag prima
		//che questa SPE lo resetti, con la conseguenza che questa non uscirà mai più del loop di check
		//in teoria,anche azzerando le flag qui potrebbe succedere che un'altra SPE sia così veloce da arrivare 
		//ad inviare la flag della prossima iterazione prima che questa SPE azzeri i received in questa iterazione.
		//Nel mio caso, però, avendo anche un blocco di sincronizzazione nel commit, sono sicuro che tutte
		//le SPE sono arrivate al commit e quindi tutte hanno azzerato il loro received.
		for(j = 0; j < SPU_THREADS; j++) 
			flags_alltoall[j].received = 0;	
		//-----------------------------------------------------------
			
		
		
//PARTE 5-----------------------BITMAP--------------------------------------
		//nel bitmap vengono marcati nel vettore apposito i bit che rappresentano i vertici "toccati" fino ad adesso
#ifdef PROFILING
			t_bitmap_start = spu_readch(SPU_RdDec);
#endif	
			
		for(i = 0; i < SPU_THREADS; i++)   //per ogni coda di entrata..
		{
			for(j = 1; j <= Qin[i][0]; j++)  //..scorro l'id del vertice da marcare
			{
				//trovo dove devo settare il bit
				id_for_spe = (Qin[i][j] % IViI); //questo è il numero del nodo relativo alle SPE. Cioé: se ogni SPE ha 20 nodi, il nodo 20 è il primo nodo della SPE1, quindi è il numero 0
				
				pos = id_for_spe  / INT_SIZE_BITS ; //dato che marked è un vettore,trovo in quale elemento va marcato il bit
				offset = id_for_spe % INT_SIZE_BITS;  //trovo in che posizione dell'elemento va marcato il bit relativo al nodo
				
				//creo la maschera
				mask = mask<<offset;  //ho spostato il bit dalla posizione 0 alla posizione che corrisponde
										     //al numero del vertice
						
				temp = marked[pos] | mask;  //faccio un OR (ovvero cambia solo il bit che voglio io, gli altri rimangono inalterati)
						
				if  (temp == marked[pos] )  //allora vuol dire che già prima era marcato
				{
					mask = mask>>offset;
				    continue;
				}
				else
				{
					marked [pos] = temp ;
		
					int t = ++Qnexti[0];
					Qnexti[t] = Qin[i][j];  
						
					mask = mask>>offset;
					// Qui riempio tutto il Qnexti, invece dovrei riempire solo il buffer bQnext se avessi implementato il double buffer 
				}
					
#ifdef VERBOSE
				printf("\n-----(SPE %d) : Livello %d : Marcato il nodo %d -----", spu_id,level, Qin[i][j]);
				fflush(stdout);
#endif
				
			}
		}

		

			
//PARTE 6-----------------------COMMIT--------------------------------------
		//nel commit vengono aggiunti alla coda Qi i vertici appena marcati (così al prossimo livello verrano caricati e verrano marcati i loro adiacenti)
		//e viene fatta la somma totale di quanti elementi mancano ancora da esplorare, in caso sia zero il ciclo while termina
#ifdef PROFILING
			t_commit_start = spu_readch(SPU_RdDec);
#endif
			
		//DMA double buffering per scrivere bQnexti in Qnexti. Notare che bQnexti scaturisce da bQi = bQi - (v1 v2 v3 ... v max allowed )
			
		//con il double buffering qui dovrebbe finire il while 
			
			
		//pongo Qi uguale a Qnexti (qui sono entrambi nel local store, invece dovrebbero essere entrambi nella ram) 
		for(i = 0; i <= Qnexti[0]; i++) 
				Qi[i] = Qnexti[i];


		//-------------sincronizzazione prima dell'inizio del livello successivo
		//DMA SPE-SPE : Scriviamo in una struttura uguale su tutte le SPU quanti elementi ci sono nella coda Qi
		
		//nodi da esplorare , valore che viene mandato a ogni SPE
		for (i=0; i<SPU_THREADS; i++)
		{
			flag[i].count = Qi[0];
		}
		
		//DMA list
		mfc_putlf(flag, ((unsigned int)parameters.local_stores[0]) + ((unsigned int)&(flags_commit[spu_id])) ,
				(unsigned int)list_commit ,SPU_THREADS*sizeof(dma_list), 3, 0, 0);
		mfc_write_tag_mask(1<<3);
		mfc_read_tag_status_all();

#ifdef PROFILING
		//come nell'all to all anche qui il blocco non lo considero
			t_commit_intermediate = spu_readch(SPU_RdDec);
#endif
			
		//blocco di sincronizzazione (come nell'all to all)
		chk = 0;
		while(chk < SPU_THREADS)
		{
			chk = 0;
			//printf ("\nSPE %d sincronizzazione Commit", spu_id);

			for(i = 0; i < SPU_THREADS; i++)
				if(flags_commit[i].received == 1) 
					chk++;
		}
		
#ifdef PROFILING
			t_commit_start2 = spu_readch(SPU_RdDec);
#endif
		//conto quanti vertici in totale (cioè tra tutte le SPEs) sono rimasti da esplorare al livello successivo
		//se ogni SPE contasse solo i propri, il rischio è che si incontri la situazione in cui la SPE corrente pensa ha Qi=0 e quindi termina, mentre in verità
		//dopo qualche livello potrebbe ricevere nuovi vertici da esplorare provenienti da altre SPE. Morale della favola: tutte le SPE possono finire quando si 
		//sicuri che non esistono più vertici da esplorare
		toContinue = 0;
		for(i = 0; i < SPU_THREADS; i++) 
			toContinue += flags_commit[i].count;
		
		
		//Riazzero i flag
		for(j = 0; j < SPU_THREADS; j++) 
		{
			flags_commit[j].received = 0;	
			flags_commit[j].count = 0;
		}
		
		level++;

			
#ifdef PROFILING  //calcolo dei tempi : per ogni parte del do-while (es: bitmap), faccio la somma del tempo usato in ogni iterazione, cioè--> bitmapfinale=bitmap1+bitmap2+...
			t_commit_end = spu_readch(SPU_RdDec); 
			
			fgd += (t_fetch_start - t_allall_start); //prima parte del fetch (fuori dal ciclo for)

			alltoall += (t_allall_start - t_allall_intermediate) + (t_allall_start2 - t_bitmap_start); //ho l'intermediate per saltare la sincronizzazione
			syncroAll += (t_allall_intermediate - t_allall_start2);
			
			bitmap += (t_bitmap_start - t_commit_start);
			
			commit += (t_commit_start - t_commit_intermediate) + (t_commit_start2 - t_commit_end);  //come nell'alltoall devo saltare la sincronizzazione
			syncroComm += (t_commit_intermediate - t_commit_start2);
#endif

	} while(toContinue != 0);    
	//-------------------------------------fine do-while------------------------------------------------------------
    //--------------------------------------------------------------------------------------------------------------

#ifdef PROFILING
	
	t_end = spu_readch(SPU_RdDec); 

#endif

		
#ifdef  VERBOSE 
	
	//controllo se tutti i vertici sono stati marcati correttamente	
	//per il controllo stampo il vettore marked. poi dal numero si possono vedere quali bit non sono stati marcati (per esempio usando la calcolatrice scientifica del mac)
	for (i=0; i<MARKED_SIZE; i++)
	{
		printf ("\n(SPU %d): Marked[%d] è uguale a: %u ", spu_id, i, marked[i]);
	}
#endif

	
			
#ifdef PROFILING  //trasformazione finale con il timebase e scrittura di tutti i tempi
			
			totaltime = (t_start - t_end) * (1.0E9/TIMEBASE);
			
			
			//fetch è il numero di decrementi che ha avuto la parte di fetch. dividendo fetch per il numero di decrementi al secondo (TIMEBASE)
			//e moltiplicando per il numero di nanosecondi per secondo ottengo per quanti nanosecondi ha lavorato la fase di fetch.
			inizializzazione *= (1.0E9/TIMEBASE);
			fgd *= (1.0E9/TIMEBASE);
			alltoall *= (1.0E9/TIMEBASE);
			syncroAll*=(1.0E9/TIMEBASE);
			bitmap *= (1.0E9/TIMEBASE);
			commit *= (1.0E9/TIMEBASE);
			syncroComm *= (1.0E9/TIMEBASE);
			
			printf ("\n----(SPU %d) : inizializzazione ns: %u",spu_id, inizializzazione);
			printf ("\n----(SPU %d) : FETCH+GATHER+DISPATCH ns: %u",spu_id, fgd);
			printf ("\n----(SPU %d) : ALLTOALL ns: %u",spu_id, alltoall);
			printf ("\n----(SPU %d) : syncro AlltoAll ns: %u",spu_id, syncroAll);
			printf ("\n----(SPU %d) : BITMAP ns: %u",spu_id, bitmap);
			printf ("\n----(SPU %d) : COMMIT ns: %u",spu_id, commit);
			printf ("\n----(SPU %d) : syncro Commit ns: %u",spu_id, syncroComm);
			
			double somma_parti = inizializzazione+fgd+alltoall+syncroAll+bitmap+commit+syncroComm;
			somma_parti /= 1E9; //trasformo in secondi																	
			printf ("\n----(SPU %d) : La somma delle singole parti è sec: %5.15f",spu_id,somma_parti);
			
			double tempototale = totaltime / 1E9;  
			printf ("\n----(SPU %d) : tempo totale sec: %5.15f",spu_id, tempototale);

			fflush(stdout);
			
			//resetto
			spu_writech(SPU_WrEventMask, 0);
			spu_writech(SPU_WrEventAck, MFC_DECREMENTER_EVENT);
#endif
			
	return 0;

}  //fine main


void fetch_ ()
{
	unsigned int j;
	
	//--------prima parte del double buffering, ovvero riempio il primo buffer
	int idx=0, next_idx=0 ,i;  //funge da contatore e da tag
	i = Qi[0] / BUFFER_SIZE  ; //trovo quante buffer servono. per esempio se i=0 basta un solo buffer

	if ( i == 0)
			i = 1;
	
	j = 1;
	unsigned int num_vertici[2];
	
	//preparo DMA list
	//possono esserci meno elementi del buffer oppure di più, quindi considero entrambi i casi nel controllo del while
	//j scorre Qi, per questo inizia da 1 (il primo elemento della coda indica quanti elementi ci sono nella coda)
	// qui j scorre anche bQi, infatti può essere che bQi è più piccola degli elementi da prendere in Qi, quindi ovviamente 
	//devo fermarmi quando bQi è satura
	while ( j<= Qi[0] && j<= BUFFER_SIZE)
	{
		//calcolo l'indirizzo contenente il vertice da prendere.
		addr = parameters.Gi_addr + (unsigned int) sizeof(vertice) * (unsigned int)(Qi[j]%IViI);
		//lo aggiungo alla lista
		list_fetch[0][j-1].ea_low = addr;
		list_fetch[0][j-1].all32 = sizeof(vertice);
		
		j++;
	}
	
	mfc_getl(bQi[idx], 
			(unsigned int) parameters.Gi_addr + (unsigned int) sizeof(vertice) * (unsigned int)(Qi[1]%IViI), 
			(unsigned int)list_fetch[0], 
			(j-1)*sizeof (dma_list),idx, 0, 0); // primo trasferimento
	
	num_vertici[0] = j-1; 

	
	//---------seconda parte del double buffering, ovvero ciclo in cui vado avanti a riempire i buffers
	while (--i )
	{
		next_idx = idx ^ 1;  //XOR: corrisponde a un IF ma senza fare branch quindi più veloce--> se idx=0, next sarà 1 e viceversa
	
		//preparo DMA list
		num_vertici[next_idx] = 0;
		//adesso per controllare che BUFFERSIZE non sia pieno uso una nuova variabile, in quanto devo azzerarla a ogni nuovo buffer
		while ( j<= Qi[0] && num_vertici[next_idx]<= BUFFER_SIZE)
		{
			//calcolo l'indirizzo contenente il vertice da prendere.
			addr = parameters.Gi_addr + (unsigned int) sizeof(vertice) * (unsigned int)(Qi[j]%IViI);
			//lo aggiungo alla lista
			list_fetch[next_idx][num_vertici[next_idx]].ea_low = addr;
			list_fetch[next_idx][num_vertici[next_idx]].all32 = sizeof(vertice);
			
			j++; num_vertici[next_idx]++;
		}
		
		mfc_getl(bQi[next_idx], 
				(unsigned int) parameters.Gi_addr + (unsigned int) sizeof(vertice) * (unsigned int)(Qi[j-num_vertici[next_idx]]%IViI), 
				(unsigned int)list_fetch[next_idx], 
				num_vertici[next_idx]*sizeof (dma_list),next_idx, 0, 0);
		
		mfc_write_tag_mask (1<<idx);
		mfc_read_tag_status_all();
			
		gather_ (idx, num_vertici[idx]) ;  //a gather passo quale buffer utilizzare e quanti sono gli elementi nel buffer
				
		idx = next_idx ;
	}
	
	//ultima parte: aspetto gli ultimi dati e poi finisce il fetch
	mfc_write_tag_mask (1<<next_idx);
	mfc_read_tag_status_all();  //aspetto l'ultimo trasferimento fatto partire

	gather_ (next_idx, num_vertici[next_idx]);  // calcolo gli ultimi dati
}




void gather_ (int num_bufferQi, unsigned int count)
{
	//PARTE 2-----------------------GATHER--------------------------------------
				
	//nel gather viene caricata con DMA la lista delle adiacenze del vertice che era stato caricato nel fetch
	unsigned int j = 0;
	
	
	//--------prima parte del double buffering, ovvero riempio il primo buffer
	int idx=0, next_idx=0 ,i ;  //idx funge da contatore e da tag
	i = count / ADJ_MAX_ALLOWED  ;  //ovvero quanti buffer bG1 mi servono per caricare le adiacenze di tutti i vertici in bQi
	
	if ( i == 0)
		i = 1;
	
	unsigned int vertice_partenza[2];  //siccome dispatch non lavorerà solo sul buffer che riceve (bGi) ma dovrà fare
										//riferimento
									   //anche a bQi, devo mandargli il vertice di partenza di quei vertici che fanno
										//riferimento alla adiacenze in bGi
	unsigned int num_trasferimenti[2];  //ovvero quanti blocchi di adj max vengono trasferiti in una dma list. quindi 
											//se riempio l'intero buffer corrisponde a ADJ_MAX_ALLOWED/IEvI_max_aligned16.
											//questo valore corrisponde quindi a quanti elementi di bQi corrispondono le 
											//adiacenze del buffer
	
	
	
	unsigned int dma_offset = 0;
	//possono esserci meno elementi del buffer oppure di più, quindi considero entrembi i casi nel controllo del while
	//j scorre il buffer bQi,ovvero quello che contiene i vertici ,per tutto il double buffering fino ad arrivare a count che è il numero di
	//vertici presenti nel buffer
	//inoltre qui j si usa anche per tenere sott'occhio il buffer bGi.dato che ad ogni iterazione faccio un trasferimento di EVimax (che quindi
	//mi occupa EVimax elementi nel buffer) devo fare la grandezza del buffer / grandezza del trasferimento per sapere quante iterazioni posso
	//fare per saturarlo
	while ( j< count && j< ADJ_MAX_ALLOWED/IEvI_max_aligned16)
	{
		//calcolo l'indirizzo contenente le adiacenze da prendere.
		dma_offset = (bQi[num_bufferQi][j].numero - spu_id*IViI ) * ( IEvI_max_aligned16 * sizeof(unsigned int) );
		addr = parameters.adj_addr + dma_offset ;
					
		//lo aggiungo alla lista
		list_gather[0][j].ea_low = addr;
		list_gather[0][j].all32 = IEvI_max_aligned16*sizeof(unsigned int);
				
		j++;
	}
			
	mfc_getl(bGi[idx],
			parameters.adj_addr + (bQi[num_bufferQi][0].numero - spu_id*IViI ) * ( IEvI_max_aligned16 * sizeof(unsigned int) ) , 
			(unsigned int)list_gather[0],
			j*sizeof (dma_list),idx, 0, 0); // primo trasferimento

	num_trasferimenti[0] = j;
	vertice_partenza[0] = 0;
			
	//---------seconda parte del double buffering, ovvero ciclo in cui vado avanti a riempire i buffers
	
	while (--i )
	{
		next_idx = idx ^ 1;  
				
		//continuo ad usare j per iterare bQi, invece mi serve una variabile da azzerare ad ogni buffer (ovvero num_trasferimenti) per controllare quando bGi
		//è saturo
		num_trasferimenti[next_idx] = 0;
		while ( j< count && num_trasferimenti[next_idx] < ADJ_MAX_ALLOWED/IEvI_max_aligned16 )
		{
			dma_offset = (bQi[num_bufferQi][j].numero - spu_id*IViI ) * ( IEvI_max_aligned16 * sizeof(unsigned int) );
			addr = parameters.adj_addr + dma_offset ;
		
			//lo aggiungo alla lista
			list_gather[next_idx][num_trasferimenti[next_idx]].ea_low = addr;
			list_gather[next_idx][num_trasferimenti[next_idx]].all32 = IEvI_max_aligned16*sizeof(unsigned int);
			
			j++; num_trasferimenti[next_idx]++;
		}
		
		vertice_partenza[next_idx] = j - num_trasferimenti[next_idx];
		
		mfc_getl(bGi[next_idx],
				parameters.adj_addr + (bQi[num_bufferQi][j-num_trasferimenti[next_idx]].numero - spu_id*IViI ) * ( IEvI_max_aligned16 * sizeof(unsigned int) ) , 
				(unsigned int)list_gather[next_idx],
				num_trasferimenti[next_idx]*sizeof (dma_list),next_idx, 0, 0); 
				
		
		mfc_write_tag_mask (1<<idx);
		mfc_read_tag_status_all();
						
		dispatch_ (num_bufferQi, idx, vertice_partenza[idx], num_trasferimenti[idx]) ; 
						
		idx = next_idx ;
	}
			
	//ultima parte: aspetto gli ultimi dati e poi finisce il fetch
	mfc_write_tag_mask (1<<next_idx);
	mfc_read_tag_status_all();  //aspetto l'ultimo trasferimento fatto partire

	dispatch_ (num_bufferQi, next_idx, vertice_partenza[next_idx], num_trasferimenti[next_idx]); 

}




void dispatch_ (int num_bufferQi, int num_bufferGi, unsigned int vertice_partenza, unsigned int num_trasferimenti )
{
	unsigned int i, j, countj=0; 
	
	//PARTE 3-----------------------DISPATCH--------------------------------------
	//nel dispatch le adiacenze caricate precedentemente vengono distribuite nelle code di uscita
		
	//vertici in bQi corrispondenti alle adiacenze in bGi
	for (i= vertice_partenza; i< vertice_partenza + num_trasferimenti; i++)
	{
		for(j = countj; j < countj+ bQi[num_bufferQi][i].adj_length; j++) 
		{
			unsigned int toSpe = bGi[num_bufferGi][j] / IViI; //Calcola a quale SPU inviare il nodo
			unsigned int t = ++Qout[toSpe][0];  //come per Qi anche per ogni coda di uscita il primo elemento mi indica quanti sono gli elementi
			Qout[toSpe][t] = bGi[num_bufferGi][j]; //carico l'adiacenza nella coda di uscita
			
		}
		countj += IEvI_max_aligned16;

	}
				
}

