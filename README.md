# Advanced Operating Systems (and Systems Security) Final Project - A.Y. 2022/2023

## Student:
- **Ludovico Zarrelli** (ID 0316448)

## Kernel version:
Il modulo seguente per il kernel Linux è stato sviluppato sulla versione **5.15.0-67-generic** ed è stato testato sulla versione **4.15.0-54-generic**.    

> **NOTE:** Per semplicità nel repository github è presente anche il modulo per la discovery della system call table e individuazione delle free entries. In questo modo accedendo alla directory root del progetto è possibile eseguire i seguenti comandi per installare il suddetto modulo:

 ```sh
sudo make build_the_usctm 
sudo make install_the_usctm 
```
> **NOTE:** Essendo che la composizione della system call table è cambiata nelle precedenti versioni del kernel all'interno della cartella /user_data_management_project/user è necessario apportare le seguenti modifiche:     
>1. Nel Makefile bisogna inserire i nuovi system call numbers. Per le 2 versioni precedenti è possibile soltanto decommentare e commentare la dichiarazione delle variabili relative alla versione di kernel di interesse.
>2. Fare la stessa cosa anche in user.h, andando a modificare le corrispondenti define.


# Indice:
1. [Introduzione](#introduzione)
2. [Data structures](#strutture-dati)
3. [System calls](#implementazione-delle-system-calls)
4. [File_operations](#implementazione-delle-file_operations-supportate-dal-device-driver)
5. [Linearizzabilità e RCU](#linearizzabilità-e-RCU)
6. [User code](#codice-utente)
7. [Compilazione](#compilazione-ed-esecuzione)

## Introduzione:
Il progetto prevede la realizzazione di un linux device driver che implementi block level maintenance di messaggi utente. Un blocco del block-device ha taglia 4 KB e mantiene 6 (2 bytes per i metadati e 4 bytes per memorizzare la posizione all'interno della lista doppiamente collegata dei messaggi validi) bytes di dati utente e 4 KB - 6 bytes per i metadati. Si richiede l'implementazione di 3 system calls non nativamente supportate dal VFS:
- int  [put_data](/user_data_management_project/userdatamgmt_sc.c#L3)(char* source, size_t size) 
- int [get_data](/user_data_management_project/userdatamgmt_sc.c#L200) (int offset, char * destination, size_t size)
- int [invalidate_data](/user_data_management_project/userdatamgmt_sc.c#L297) (int offset)

e di 3 file operations che driver deve supportare:
- int [open](/user_data_management_project/userdatamgmt_driver.c#L175) (struct inode*, struct file*)
- int [release](/user_data_management_project/userdatamgmt_driver.c#L153)(struct inode*, struct file*)
- ssize_t [read](/user_data_management_project/userdatamgmt_driver.c#L1) (struct file*, char __user*, size_t, loff_t*)

Il device driver deve essere accessibile come file in un file system che supporti le file_operations precedenti. Di conseguenza, si è deciso di  integrare il progetto con il modulo singlefile-FS (che è l'implementazione di un file system accessibile come fosse un file regolare, attraverso l'utilizzo del loop driver) andando ad associare nell'inode del file (che è l'unico file che il FS è in grado di gestire) il puntatore alla struct file_operations (istanziata [qui](/user_data_management_project/userdatamgmt_driver.c#L212)) contenente i riferimenti alle funzioni di driver implementate (vedere il seguente [link](/user_data_management_project/file_system/file.c#L47)).

Pe ridurre la dimensione dell' eseguibile ed evitare la ripetizione di #include nei vari codici sorgenti si è deciso di includere i sorgenti:
- /user_data_management_project/userdatamgmt_driver.c
- /user_data_management_project/file_system/userdatamgmt_fs_src.c
- /user_data_management_project/userdatamgmt_sc.c

all' interno del file /user_data_management_project/userdatamgmt.c nel quale viene fatta l'init e la cleanup del modulo (vedere [qui](/user_data_management_project/userdatamgmt.c#L39)).

## Strutture dati:
Per la realizzazione del device driver sono state introdotte le seguenti strutture dati:

- struct [dev_blk](/user_data_management_project/userdatamgmt_driver.h#L20)  : È la rappresentazione (block layout) in memoria della composizione del blocco di un device. È costituto dai seguenti campi:
    - char data [SIZE]: array di SIZE (dimensione del blocco - la dimensione dei metadati) caratteri che contiene il messaggio utente e del padding di zeri 
    - uint16_t metadata: bit mask dei metadati del blocco corrispondente. Il bit più significativo è il validity bit, mentre i 12 bit meno signficativi rappresentano la lunghezza del messaggio utente (2^12 = 4KB, di conseguenza è il minimo numero di bit necessari per rappresentare tutte le possibili lunghezze)
    - int position: intero che rappresenta la posizione del blocco all'interno della lista (ordinata in base all'ordine di inserimento)doppiamente collegata dei messaggi validi (RCU list)

- struct [message](/user_data_management_project/userdatamgmt_driver.h#L38): rappresenta un elemento nella RCU double-linked list. È costituto dai seguenti campi:
    - struct blk_element *elem: array di SIZE (dimensione del blocco - la dimensione dei metadati) caratteri che contiene il messaggio utente e del padding di zeri 
    - struct message *next: puntatore al prossimo elemento, cioè al seguente messaggio valido
    - struct message *prev: puntatore al precedente elemento, cioè al  messaggio valido che lo ha preceduto
    - int index: offset all'interno del block device - 2. Non considera il superblocco e l'inode del file
    - int position come sopra

- struct [blk_element](/user_data_management_project/userdatamgmt_driver.h#L30): rappresenta un elemento nell'array dei metadati.  È costituto dai seguenti campi:
    - struct message * msg è il puntatore (se esiste) al messaggio corrispondente nella lista RCU 
    - uint16_t metadata come sopra.

- struct [current_message](/user_data_management_project/userdatamgmt_driver.h#L49): è la struttura dati che mantiene il puntatore al messaggio al quale il lettore è arrivato nella sua sessione di I/0. È costituto dai seguenti campi:
    - int position: come sopra
    - struct message * curr: puntatore al messaggio corrente nella sessione di I/O 
    - loff_t offset: posizione attuale all'interno del file

- struct [rcu_data](/user_data_management_project/userdatamgmt_driver.h#L70) : Contiene tutte le variabili necessarie per implementare l' approccio RCU.
    - struct message *first: puntatore al primo elemento della lista dei messaggi validi                             
    - struct message *last: puntatore all' ultimo elemento della lista dei messaggi validi
    - unsigned long standing[EPOCHS]: array di due unsigned long. I lettori vanno a rilasciare un "token" nell' elemento dell'array, il cui indice  corrisponde all'epoca di loro interesse (epoca di inizio lettura)
    - unsigned long epoch: lettori dell'epoca corrente. Il bit più significativo è l'indice dell'epoca corrente
    - int next_epoch_index: indice della prossima epoca                                
    - spinlock_t write_lock: puntatore allo spinlock in scrittura


- struct [bdev_metadata](/user_data_management_project/userdatamgmt_driver.h#L56): contiene le informazioni riguardo allo stato corrente del block device.
    - unsigned int count: è un contatore che rappresenta il numero di threads che stanno correntemente utilizzando il device driver. È incrementata atomicamente ed è utilizzata per evitare che un thread in concorrenza faccia l'unmount e conseguente kill del superblocco.
    - struct block_device *bdev: puntatore alla struttura block_device. Viene memorizzata per repererire il puntatore alla struttura generica super_block
    - const char *path: path name del device (image)

- struct [mount_metadata](/user_data_management_project/userdatamgmt_driver.h#L63): contiene le informazioni sul mounting del singlefile-FS.
    - int mounted: è una variabile che è settata atomicamente a 1 se il FS è montato e 0 altrimenti
    - char *mount_point: punto di ancoraggio a partire da /

## File system:

## Implementazione delle system calls:

## Implementazione delle file_operations supportate dal device driver: 

## Linearizzabilità e RCU:

## Codice Utente:
All' interno del progetto sono presenti due sorgenti user level:
 - [makefs.c](/user_data_management_project/file_system/makefs.c): è il file sorgente che formatta il device per il suo utilizzo. Il device è formattato nel seguente modo:
    1. Scrittura del superblocco
    2. Scrittura del file inode
    3. Scrittura di un numero di blocchi pari alla dimensione del device. All'interno del codice sono presenti 20 stringhe hardcoded che vengono scritte nei primi blocchi. I rimanenti (se esistono) sono tutti invalidi, ma possono essere utilizzati in futuro attraverso le system call implementate.
- [user.c](/user_data_management_project/user/user.c): è codice di prova, sviluppato per testare le system calls e la concorrenza tra scrittori e lettori, anche sullo stesso blocco. All'interno del Makefile sono previste le seguenti modalità d'esecuzione:
    - [puts](/user_data_management_project/user/Makefile#L21): Nel caso single-thread inserisce una stringa hardcoded nel primo blocco libero nel device. Nel caso multi-thread ogni threads inserisce la stessa stringa in blocchi liberi (se esistono)
    - [get_data](/user_data_management_project/user/Makefile#L25): Viene acquisito i dati del blocco (se valido) con l'indice (offset nel blocco - 2) passato come argomento
    - [invalidate_data](/user_data_management_project/user/Makefile#L29): Viene invalidato (da uno dei thread creati) il blocco (se non è già invalido) con l'indice (offset nel blocco - 2) passato come argomento
    - [gets](/user_data_management_project/user/Makefile#L29): Vengono acquisiti tutti i messaggi utenti in concorrenza dai threads creati. Ogni thread è responsabile dei blocchi individuati dal loro indice + NTHREADS *i.
    - [invalidations](/user_data_management_project/user/Makefile#L32): Vengono invalidati (da uno dei thread creati) tutti i blocchi (se non è già invalido) passati come argomento
    - [multi-ops](/user_data_management_project/user/Makefile#L36): Il 66% dei threads creati sono readers, il restante sono writers. In base al loro indice, ogni thread esegue una delle tre system call sul blocco passato in input di sua competenza. 
    - [same-block-ops](/user_data_management_project/user/Makefile#L40): Il 66% dei threads creati sono readers, il restante sono writers. Per un numero di volte pari REQS, ogni thread (in base al suo id) invoca una system call sullo stesso blocco. Ho usato questa modalià per valutare l'interleave di writers e readers e il corretto funzionamento dell'approccio RCU.

## Compilazione ed esecuzione:
La fase di compilazione è caratterizzata dai seguenti passi:

1. Posizionarsi nella root directory del progetto ed eseguire i seguenti comandi del Makefile più esterno.

2. Compilazione e insmod del modulo the_usctm:
    ```sh
        sudo make build_the_usctm 
        sudo make install_the_usctm 
    ```

3. Compilazione, insmod del modulo progetto_soa, creazione del singlefile-FS e mounting di quest'ultimo nella directory con path relativo /mount:
    ```sh
        sudo make build_progetto_soa 
        sudo make install_progetto_soa //Internamente fa insmod, create-fs e mount-fs presenti nel makefile interno a /user_data_management_project
    ```
4. All'inteno della directory user è presente codice user per utilizzare il modulo in varie modalità (single thread, multi-threads, operazioni multiple su blocchi diversi e molte operazioni sullo stesso blocco). Nel Makefile è possibile specificare quale modalità utilizzare e anche se generare l'eseguibile nella sua versione single-thread o in quella multi-thread. 
> **NOTE:** In versioni diverse del kernel potrebbe essere necessario modificare i system call numbers sia nel Makefile che all'interno di /user_data_management_project/user/user.h
