# Advanced Operating Systems (and Systems Security) Final Project - A.Y. 2022/2023

## Student:
- **Ludovico Zarrelli** (ID 0316448)

## Kernel version:
Il modulo seguente per il kernel Linux è stato sviluppato per la versione **5.15.0-67-generic** ed è stato testato sulla versione **4.15.0-54-generic**.    

> **NOTE:** Per semplicità nel repository github è presente anche il modulo per la discovery della system call table e individuazione delle free entries. In questo modo accedendo alla directory root del progetto è possibile eseguire i seguenti comandi per installare il suddetto modulo:

 ```sh
sudo make build_the_usctm 
sudo make install_the_usctm 
```

> **NOTE:** Essendo che la system call table è cambiata durante lo sviluppo del kernel Linux, all'interno della cartella /user_data_management_project/user è necessario apportare le seguenti modifiche:     
>1. Nel Makefile bisogna inserire i numeri delle system call installate. Per le 2 versioni indicate in precedenza è possibile soltanto decommentare e commentare la dichiarazione delle variabili relative alla versione di kernel di interesse.
>2. Fare la stessa cosa anche in user.h, andando a modificare le corrispondenti define.


# Indice:
1. [Introduzione](#introduzione)
2. [Data structures](#strutture-dati)
3. [Headers and defines](#headers-and-defines)
4. [File system](#file-system)
5. [System calls](#implementazione-delle-system-calls)
6. [File_operations](#implementazione-delle-file_operations-supportate-dal-device-driver)
7. [Linearizzabilità e RCU](#linearizzabilità-e-rcu)
8. [User code](#codice-utente)
9. [Compilazione](#compilazione-ed-esecuzione)

## Introduzione:
Il progetto prevede la realizzazione di un linux device driver che implementi block level maintenance di messaggi utente. Un blocco del block-device ha taglia 4 KB e mantiene 6 (2 bytes per i metadati e 4 bytes per memorizzare la posizione all'interno della lista doppiamente collegata dei messaggi validi) bytes di dati utente e 4 KB - 6 bytes per i metadati. Si richiede l'implementazione di 3 system calls non nativamente supportate dal VFS:
- int [put_data](/user_data_management_project/userdatamgmt_sc.c#L4)(char* source, size_t size) 
- int [get_data](/user_data_management_project/userdatamgmt_sc.c#L211) (int offset, char * destination, size_t size)
- int [invalidate_data](/user_data_management_project/userdatamgmt_sc.c#L315) (int offset)

e di 3 file operations che driver deve supportare:
- int [open](/user_data_management_project/userdatamgmt_driver.c#L176) (struct inode*, struct file*)
- int [release](/user_data_management_project/userdatamgmt_driver.c#L154)(struct inode*, struct file*)
- ssize_t [read](/user_data_management_project/userdatamgmt_driver.c#L1) (struct file*, char __user*, size_t, loff_t*)

Il device driver deve essere accessibile come file in un file system che supporti le file_operations precedenti. Di conseguenza, si è deciso di  integrare il progetto con il modulo singlefile-FS (che è l'implementazione di un file system accessibile come fosse un file regolare, attraverso l'utilizzo del loop driver) andando ad associare nell'inode del file (che è l'unico file che il FS è in grado di gestire), nella file_operation lookup, il puntatore alla struct file_operations (istanziata [qui](/user_data_management_project/userdatamgmt_driver.c#L218)) contenente i riferimenti alle funzioni di driver implementate (vedere il seguente [link](/user_data_management_project/file_system/file.c#L47)).

Pe ridurre la dimensione dell' eseguibile ed evitare la ripetizione di #include nei vari codici sorgenti si è deciso di includere:
- /user_data_management_project/userdatamgmt_driver.c
- /user_data_management_project/file_system/userdatamgmt_fs_src.c
- /user_data_management_project/userdatamgmt_sc.c

all' interno del file /user_data_management_project/userdatamgmt.c nel quale viene fatta l'init e la cleanup del modulo (vedere [qui](/user_data_management_project/userdatamgmt.c#L39)).

## Strutture dati:
Per la realizzazione del device driver sono state introdotte le seguenti strutture dati:

- struct [dev_blk](/user_data_management_project/userdatamgmt_driver.h#L20)  : È la rappresentazione (block layout) in memoria della composizione del blocco di un device. È costituto dai seguenti campi:
    - char data [SIZE]: array di SIZE (dimensione del blocco - la dimensione dei metadati) caratteri che contiene il messaggio utente e del padding di zeri 
    - uint16_t metadata: bit mask dei metadati del blocco corrispondente. Il bit più significativo è il validity bit, mentre i 12 bit meno signficativi rappresentano la lunghezza del messaggio utente (2^12 = 4KB, di conseguenza è il minimo numero di bit necessari per rappresentare tutte le possibili lunghezze)
    >**NOTE**: Sono presenti delle define anche per la manipolazione del free bit, secondo bit più significativo. Tuttavia, si è deciso di non distringuere il concetto di invalido con quello di libero (senza dati utente). Si è deciso di lasciarlo per altre versioni di questo modulo in cui la distinzione (dal punto di vista semantico) tra blocco valido ma libero e blocco invalido ha più rilievo.
    - int position: intero che rappresenta la posizione del blocco all'interno della lista (ordinata in base all'ordine di inserimento)doppiamente collegata dei messaggi validi (RCU list)

- struct [message](/user_data_management_project/userdatamgmt_driver.h#L38): rappresenta un elemento nella RCU double-linked list. È costituto dai seguenti campi:
    - struct blk_element *elem: è il puntatore all' elemento dell' array (di metadati di tutti i blocchi) collegato al messaggio considerato.
    - struct message *next: puntatore al prossimo elemento, cioè al seguente messaggio valido
    - struct message *prev: puntatore al precedente elemento, cioè al  messaggio valido che precedente
    - int index: offset all'interno del block device - 2. Non considera il superblocco e l'inode del file
    - int position come sopra

- struct [blk_element](/user_data_management_project/userdatamgmt_driver.h#L30): rappresenta un elemento nell'array dei metadati. È costituto dai seguenti campi:
    - struct message * msg è il puntatore (se esiste) al messaggio corrispondente nella lista RCU 
    - uint16_t metadata, come sopra.

- struct [current_message](/user_data_management_project/userdatamgmt_driver.h#L49): è la struttura dati che mantiene il puntatore al messaggio al quale il lettore è arrivato nella sua sessione di I/0. È costituto dai seguenti campi:
    - int position: come sopra
    - struct message * curr: puntatore al messaggio corrente nella sessione di I/O 
    - loff_t offset: posizione attuale all'interno del file nell'sessione di I/O corrente

- struct [rcu_data](/user_data_management_project/userdatamgmt_driver.h#L70) : Contiene tutte le variabili necessarie per implementare l' approccio RCU:
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
## Headers and defines:
In questa sezione si va ad analizzare in dettaglio il contenuto dei vari headers presenti nel modulo. In totale ci sono 5 headers:
- [userdatamgmt.h](/user_data_management_project/userdatamgmt.h): contiene tutte le defines utilizzate all'interno del modulo, i riferimenti alle strutture dati globali e le macro per operare sulle bitwise mask. Le principali defines sono:
    - MOD_NAME: Stringa che rappresenta il nome del modulo
    - AUDIT: Macro di debugging 
    - BLK_SIZE: dimensione del blocco in bytes
    - POS_SIZE: dimenensione in bytes di un intero
    - MD_SIZE: dimensione in bytes per i metadati
    - NBLOCKS: numero di blocchi gestibili dal software
    - PERIOD: Tempo di attesa per l'house keeper
    - Alcune define per operare sul validity bit e sui 12 bit (2^12 = 4KB) di lunghezza del messaggio.
    - Riferimenti alle strutture dati condivise sh_data (RCU), bdev_md, mount_md e all'intero nblocks che rappresenta il numero di blocchi - 2 presenti sul device.

- [userdatamgmt_fs.h](/user_data_management_project/file_system/userdatamgmt_fs_src.c): contiene le definizione delle strutture dati e le defines utilizzate nel singlefile-FS.

- [userdatamgmt_driver.h](/user_data_management_project/userdatamgmt_driver.c): contiene le definizioni delle strutture dati utilizzate per la realizzazione del Block-level data management service. L'approfondimento sulle data structures utilizzate è presente nella [sezione precedente](/README.md#strutture-dati)

- [utils.h](/user_data_management_project/utils.h): contiene i prototipi delle funzioni "ausiliarie" presenti in utils.c. Sono presenti i seguenti prototipi:
    - void [init](/user_data_management_project/utils.c#L57)(struct rcu_data *): funzione di inizializzazione dei campi della struttura RCU
    - void [insert](/user_data_management_project/utils.c#L92)(struct message **, struct message **, struct message *): inserimento in coda del messaggio in input
    - void [insert_sorted](/user_data_management_project/utils.c#L114)(struct message **, struct message **, struct message *): inserimento ordinato del messaggio in base alla sua posizione
    - void [free_list](/user_data_management_project/utils.c#L171)(struct message *): funzione per liberare la lista dei messaggi validi
    - void [free_array](/user_data_management_project/utils.c#L190)(struct blk_element **): funzione per eseguire la free sull'array di metadati
    - void [delete](/user_data_management_project/utils.c#L214)(struct message **, struct message **, struct message *): funzione per eliminare il messaggio in input dalla lista dei messaggi validi
    - struct message *[lookup_by_pos](/user_data_management_project/utils.c#L287)(struct message *, int): lookup del messaggio in base alla sua posizione
    - struct message *[lookup_by_index](/user_data_management_project/utils.c#L308)(struct message *, int): lookup del messaggio in base al suo indice
    - void [quickSort](/user_data_management_project/utils.c#L376)(struct message *, struct message *): funzione che implementa il quick sort 
    - void [print_list](/user_data_management_project/utils.c#L257)(struct message *): funzione di debugging per stampare la lista dei messaggi validi a partire dalla testa
    - void [print_reverse](/user_data_management_project/utils.c#L272)(struct message *): funzione di debugging per stampare la lista dei messaggi validi a partire dalla coda

- [user.h](/user_data_management_project/user/user.h): contiene le defines utilizzate all'interno del sorgente user.c.



## File system:
Affinchè il device driver sia accessibile come file in un file system è necessario implementare le funzioni di mount e kill del superblocco (kill_sb). 
Come già detto in precedenza, il device ha associato un driver, che è associato all'inode del file "the-file" nella file_operation lookup. In questo modo l'accesso, lettura e close sul file "the-file" vengono "mappate" nelle funzioni del device driver implementate, rispettivamente dev_open, dev_read e dev_close. Attrvarso l'utilizzo del -o loop driver ([vedi](/user_data_management_project/Makefile#L30)), il file "the-file" può essere visto come un dispositivo a blocchi. Inoltre, è presente del software ([vedere](/user_data_management_project/file_system/makefs.c)) user per formattare il device. Ovviamente, viene formattato in modo che sia compliant alla struttura prevista per il device a blocchi.     
Di seguito vengono spiegate in dettaglio mount() e kill_sb():
- struct dentry * [userdatafs_mount](/user_data_management_project/file_system/userdatamgmt_fs_src.c#L175)(struct file_system_type *fs_type, int flags, const char *dev_name, void *data): è la funzione che viene invocata nella fase di mounting del file system. Può essere sintetizzata nei seguenti step:
    1.  Da specifica, per semplicità, è previsto solo un mount point per volta, di conseguenza, all'inizio attravero una **CAS** il thread che fa la mount cerca di portare a 1 l'intero **mounted**. Essendo una operazione RMW locked ci sarà **solo un thread** che in concorrenza potrà portare a 1 quella variabile condivisa, tutti gli altri ritornano al chiamante con il codice d'errore -EBUSY. [Vedere qui](/user_data_management_project/file_system/userdatamgmt_fs_src.c#L198)

    2. Il secondo passo è l'invocazione di mount_bdev per montare il file system memorizzato su un dispositivo a blocchi: 
        - Viene allocata una struttura generica superblocco (del VFS)
        - Viene invocata la funzione di callback che prende in input la struttura super_block generica e la riempie. All'interno della fill_super si **verifica se NBLOCK**, che è il numero di blocchi gestibili **è minore o uguale della dimensione del device**.*  
        [Vedere qui](/user_data_management_project/file_system/userdatamgmt_fs_src.c#L44)

    3. Se la mount_bdev ha avuto **successo**:
        1. Viene inizializzata la **struttura dati condivisa RCU** . [Vedere qui](/user_data_management_project/file_system/userdatamgmt_fs_src.c#L218)
        2. Viene allocato un **array di size nblocks**  (numero di blocchi totali - 2 del device) che conterrà i metadati di ogni blocco. [Vedere qui](/user_data_management_project/file_system/userdatamgmt_fs_src.c#L221)
        3. Per ogni i da 0 a nblock - 1  ([Vedere qui](/user_data_management_project/file_system/userdatamgmt_fs_src.c#L233)):
             1. Si legge dal device attraverso **sb_bread()**, il blocco ad offset i + 2.
             2. Viene **inizializzata** l'entry i-esima dell'array con i metadati letti da device. 
             3. Se il blocco che si sta correntemente leggendo è **valido** allora viene allocato un'istanza di struct message e viene inserita (inserimento ordinato), in base alla **posizione** (indica l'ordine di inserimento) mantenuta sul device, nella lista doppiamente collegata dei messaggi validi . 
             4. Al termine di ogni ciclo viene invocata la brelse() sull'attuale buffer_head pointer. 
             5. Se durante l'esecuzione c'è stato un **errore** allora si esce dal ciclo for, si libera l'array di metadati, la lista doppiamente collegate e si invoca **blkdev_put** (per rilasciare il riferimento al block device).

    4. Infine, si controlla se c'è stato qualche **errore** durante l'esecuzione. In questo caso **mounted viene riportato a 0** poichè la mount non ha avuto successo. In questo modo, se la mount dovesse fallire, è possibile rieseguirla. [Vedere qui](/user_data_management_project/file_system/userdatamgmt_fs_src.c#L302)

>**NOTE**: L'operazione di mount in questo caso ha costo O(n^2): per ogni blocco da leggere è richiesto un inserimento sorted. Per evitare questo problema è presente del [codice](/user_data_management_project/file_system/userdatamgmt_fs_src.c#L289), in utils.c, che realizza il quick sort a partire da una lista di elementi non ordinata. In questo modo il costo è O(n*logn). Il problema in questo caso è che il quick sort è implementato in modo ricorsivo. Di conseguenza, in presenza di un numero elevato di blocchi validi, oppure nel caso degenere di un array già ordinato, lo stack di livello kernel potrebbe esaurirsi. Ovviamente questo è un tradeoff tra prestazioni e utilizzo di memoria. Attualmente la versione con il quick sort è commentata.

- static void [userdatafs_kill_superblock](/user_data_management_project/file_system/userdatamgmt_fs_src.c#L98) (struct super_block *s): è la funzione che viene invocata nella fase di unmounting del file system. È caratterizzata dai seguenti passi:
    1. Come sopra, per prima cosa, un **solo thread porta a 0** il valore dell'intero mounted. In questo modo è possibile fare nuovamente il mounting in futuro. [Vedere qui](/user_data_management_project/file_system/userdatamgmt_fs_src.c#L114)
    2. Si va in wait, secondo un approccio basato su **polling**, fintanto che l'usage counter (il numero di thread che sta correntemente lavorando sul device) è diverso zero.  [Vedere qui](/user_data_management_project/file_system/userdatamgmt_fs_src.c#L124)
    3. Una volta diventato zero, si itera sugli elementi della lista di messaggi validi e si realizza il **filling dei gaps del campo position**, a causa di operazioni di invalidate (I campi position dei messaggi successivi a quello da invalidare non vengono aggiornati), in modo da evitare **overflow** (Il valore di position altrimenti, sarebbe sempre aumentato in successive mount e unmoun del FS). Avendo tutti inserimenti in coda, la posizione rappresenta implicitamente un ordinamento totale dei messaggi.> **NOTE**: Ho scelto di evitare la possibilità di avere un overflow dell'intero position (avrei avuto un'inconsistenza sull'ordinamento dei messaggi) a scapito di un peggioramento delle performance. Se tutti i blocchi sono validi (caso pessimo) ho un costo di 2n.
    4.  Si itera, anche su tutti i blocchi per riportare su device le posizioni (-1 se il blocco non è valido) e i **nuovi metadati per blocchi invalidi**.Memorizzare la posizione è importante affinchè l'ordine d'inserimento dei messaggi **persista anche in sequenze di mount e unmount successivi**. [Vedere qui](/user_data_management_project/file_system/userdatamgmt_fs_src.c#L128)
    4. Infine, si fa la **kill del superblocco e le free dell'array e della lista doppiamente collegata**. [Vedere qui](/user_data_management_project/file_system/userdatamgmt_fs_src.c#L162)



## Implementazione delle system calls:
Andiamo ad analizzare più in dettaglio le system calls sys_put_data, sys_get_data e sys_invalidate_data:
- int [sys_put_data](/user_data_management_project/userdatamgmt_sc.c#L4)(char* source, size_t size): Ha l'obiettivo di andare ad inserire sul device size bytes all'interno del buffer source in un blocco invalido. Si è deciso di "preservare" l'ordine di inserimento andando a scrivere in coda alla lista dei messaggi validi. L'algoritmo prevede i seguenti passi:
    1. Si rende impossibile l'operazione di unmount del FS aggiungendo 1 all'usage counter. [Vedere qui](/user_data_management_project/userdatamgmt_sc.c#L31)
    2. Si verifica che il file system sia stato montato. [Vedere qui](/user_data_management_project/userdatamgmt_sc.c#L34)
    3. Si verifica il valore di size_t: se è maggiore della SIZE, cioè il numero massimo di bytes per un messaggio, allora size è "limitata" a SIZE. Inoltre, se size è minore di zero ritorna -EINVAL. [Vedere qui](/user_data_management_project/userdatamgmt_sc.c#L43)
    4. Al di fuori della critical section, si calcola il nuovo valore per i metadati da assegnare al primo blocco libero (se esiste) e si alloca il buffer di destinazione (lato kernel). [Vedere qui](/user_data_management_project/userdatamgmt_sc.c#L51)
    5. Se l'allocazione è andata a buon fine, si copia il contenuto del buffer user nel buffer di destinazione, attraverso copy_from_user(), e si copiano i metadati nella corretta posizione del buffer. [Vedere qui](/user_data_management_project/userdatamgmt_sc.c#L67). 
    6. Si alloca memoria di livello kernel per la struttura che rappresenterà il nuovo messaggio utente all'interno della lista doppiamente collegata.[Vedere qui](/user_data_management_project/userdatamgmt_sc.c#L73)
    >**NOTE**: Le operazioni precedenti sono eseguite al di fuori della sezione critica poichè non accedono a strutture dati condivise. Questa scelta porta ad una sezione critica minimale. 
    7. Il thread corrente va in attesa del lock in scrittura.[Vedere qui](/user_data_management_project/userdatamgmt_sc.c#L83)
    8. Una volta acquisito il lock, viene attraversato l'array di metadati per individuare il primo blocco ,se esiste, non valido.[Vedere qui](/user_data_management_project/userdatamgmt_sc.c#L90)
    9. Successivamente, viene scritto in modo sincrono o asincrono, attraverso il page-cache write back daemon, nel blocco individuato, il contenuto del buffer di destinazione. [Vedere qui](/user_data_management_project/userdatamgmt_sc.c#L130)
    > **NOTE**: L'aggiornamento del device, nel caso in cui sia sincrono, è un'operazione computazionalmente costosa eseguita in sezione critica. Tuttavia, ciò è necessario per realizzare un'approccio all or nothing. Di fatto, sia la sb_bread() che la scrittura sincrona possono fallire, portando ad una possibile incosistenza con le informazioni mantenute dalle strutture dati del modulo. Ciò, sarebbe potuto avvenire nel caso in cui il flush sul device fosse stato inserito dopo l'aggiornamento delle strutture dati, al di fuori della sezione critica. Inoltre, nel caso in cui la scrittura sul device fosse successiva all'aggiornamento ed in sezione critica, sarebbe stato necessario aggiungere codice per eseguire l'undo, in caso di fallimento.
    10. Se l'operazione ha avuto successo si procede con l'aggiornamento delle strutture dati condivise: 
        1. Si inizializza la struct messagge con l'indice, il puntatore prev e il puntatore all'elemento dell'array contenente i metadati corrispondenti. [Vedere qui](/user_data_management_project/userdatamgmt_sc.c#L158)
        >**NOTE**: Ad ora il messaggio non è ancora visibile ai readers non avendo agganciato il predecessore (ultimo elemento nei messaggi validi, prima dell'inserimento) all'attuale messaggio (ultimo elemento nei messaggi validi dopo l'inserimento) 
        2. Si fa puntare l'elemento dell'array contenente i metadati al nuovo messaggio e si aggiornano i metadati. [Vedere qui](/user_data_management_project/userdatamgmt_sc.c#L163)
        >**NOTE**: Solo gli scrittori accedono all'array dei metadati. I lettori non lo attraverseranno mai. Di conseguenza non ho problemi di consistenza essendo i lettori sincronizzati con un lock.
        3. Infine, si calcola la posizione del nuovo messaggio e si inserisce l'elemento all'interno della lista doppiamente collegata. [Vedere qui](/user_data_management_project/userdatamgmt_sc.c#L166)
         >**NOTE**: Essendo che i readers attraversano la lista dei messaggi validi a partire dalla testa, l' operazione linearizzante per i lettori è l'aggiornamento del puntatore a next del predecessore in modo tale che punti al messaggio appena inserito.
    11. Si rilascia lo spinlock, il buffer di destinazione, si decrementa l'usage counter e si va a risvegliare il thread per andare a valutare nuovamente la condizione. [Vedere qui](/user_data_management_project/userdatamgmt_sc.c#L192)
    >**NOTE**: Si è deciso di non adottare un approccio lock free a causa del fatto che l'operazione atomiche sono due: l'aggiornamento dei metadati per gli scrittori e l'inserimento in coda per i lettori. Di conseguenza, in un approccio lock free avrei avuto due punti di linearizzazione diversi. Sarebbe stato possibile individuare un blocco valido per lo scrittore ma invalido per il lettore, poichè non è presente nella lista, nello stesso istante di tempo. Attraverso l'utilizzo di spinlock, gli scrittori sono bloccati fino al suo rilascio. 


- int [sys_get_data](/user_data_management_project/userdatamgmt_sc.c#L211) (int offset, char * destination, size_t size): Ha l'obiettivo di andare a leggere size bytes del blocco ad indice offset e riportarne il contenuto nel buffer destination lato user.
    1. Si rende impossibile l'operazione di unmount del FS aggiungendo 1 all'usage counter. [Vedere qui](/user_data_management_project/userdatamgmt_sc.c#L235)
    2. Si verifica che il file system sia stato montato. [Vedere qui](/user_data_management_project/userdatamgmt_sc.c#L237)
    3. Si verifica il valore di size_t; se è maggiore della SIZE, cioè il numero massimo di bytes per un messaggio, allora size è "limitata" a SIZE. Inoltre, se size è minore di zero o se offset è minore di 0 o maggiore di nblocks -1 viene ritornato -EINVAL.  [Vedere qui](/user_data_management_project/userdatamgmt_sc.c#L245)
    4. Si aggiunge 1 all'epoca corrente e si individua all'interno della lista doppiamente collegata il messaggio, che corrisponde al blocco con indice in input. [Vedere qui](/user_data_management_project/userdatamgmt_sc.c#L255)
    5. Se il blocco mantiene un messaggio valido allora si va a leggere da device. [Vedere qui](/user_data_management_project/userdatamgmt_sc.c#L267)
    6. All'interno di un ciclo while vengono gestiti eventuali residui nella consegna all'utente. [Vedere qui](/user_data_management_project/userdatamgmt_sc.c#L283)
    7. Infine, viene individuato, a partire dall'epoca corrente, l'indice all'interno dell'array standing. Viene, quindi, aggiunto 1 nell'entry con l'index trovato, si decrementa l'usage counter e si va a risvegliare il thread per andare a valutare nuovamente la condizione.. 
    [Vedere qui](/user_data_management_project/userdatamgmt_sc.c#L299)
> **NOTE**: Si è  deciso di far rilasciare il token (al reader) solo al completamento della lettura da device per evitare il caso seguente: se il rilascio fosse stato inserito prima della lettura del device sarebbe potuto accadere che, in concorrenza, uno scrittore avrebbe potuto invalidare il blocco da leggere e poi sovrascriverne il contenuto con una sys_put_data. Di conseguenza, la lettura da device sarebbe stata inconsistente.

- int [sys_invalidate_data](/user_data_management_project/userdatamgmt_sc.c#L316) (int offset):
    1. Si rende impossibile l'operazione di unmount del FS aggiungendo 1 all'usage counter. [Vedere qui](/user_data_management_project/userdatamgmt_sc.c#L339)
    2. Si verifica che il file system sia stato montato. [Vedere qui](/user_data_management_project/userdatamgmt_sc.c#L342)
    3. Si verifica il valore di offset: se offset è minore di 0 o maggiore di nblocks -1 viene ritornato -EINVAL.  [Vedere qui](/user_data_management_project/userdatamgmt_sc.c#L349)
    4.  Il thread corrente va in attesa del lock in scrittura. [Vedere qui](/user_data_management_project/userdatamgmt_sc.c#L357).
    5. Una volta acquisito il lock, si accede con costo 0(1) ai metadati del blocco ad indice pari all'offset in input. Se il blocco è già stato invalidato allora viene ritornato -ENODATA. [Vedere qui](/user_data_management_project/userdatamgmt_sc.c#L360)
    6. Si procede ad aggiornare la lista dei messaggi validi andando ad eliminare l'elemento da invalidare dalla lista. Ciò, è fatto con costo costante O(1) in seguito all'utilizzo di una lista doppiamente collegata ([Vedere qui](/user_data_management_project/userdatamgmt_sc.c#L378)). 
    La delete è caratterizzata dai seguenti passi: 
        1. Se l'elemento da eliminare è la testa allora viene aggiornato head in modo che punti ad head->next. Ciò è visibile ai lettori immediatamente. Tutti i lettori d'ora in poi vedranno un nuovo elemento che head della lista dei messaggi validi. Viene anche aggiornato il prev del successore del nodo da eliminare. [Vedere qui](/user_data_management_project/utils.c#L230). 
        2. Se l'elemento da eliminare è in mezzo alla lista allora viene sganciato andando ad aggiornare il puntatore a next del predecessore. Come già detto in precedenza, per costruzione quest'operazione è il punto di linearizzazione per i lettori. Infine, viene aggiornato il puntatore prev del successore. [Vedere qui](/user_data_management_project/utils.c#L236).
        3. Se l'elemento è la coda allora viene aggiornato il next del predecessore in modo che punti a NULL (linearizzazione). Infine, viene aggiornata la coda. [Vedere qui](/user_data_management_project/utils.c#L248).
    7. Viene individuata la nuova epoca, viene aggiornato atomicamente il puntatore all'epoca corrente e infine si va in attesa del termine del grace period sull'epoca precedente. [Vedere qui](/user_data_management_project/userdatamgmt_sc.c#L390)
    8. Al termine dell'epoca viene rilasciato lo spinlock, viene liberata la memoria del buffer, si decrementa l'usage counter e si risveglia il thread in attesa quando si verifica la condizione desiderata. [Vedere qui](/user_data_management_project/userdatamgmt_sc.c#L405)


## Implementazione delle file_operations supportate dal device driver: 
Andiamo ad analizzare più in dettaglio le file_operations open, read e close:
- int [open](/user_data_management_project/userdatamgmt_driver.c#L179) (struct inode*, struct file*): 
    1. Innanzitutto, si verifica che il file system sia stato montato. [Vedere qui](/user_data_management_project/userdatamgmt_driver.c#L191)
    2. Se la modalità di apertura del file è in write mode, si ritorna il codice d'errore (Read-only FS). [Vedere qui](/user_data_management_project/userdatamgmt_driver.c#L196)
    3. Si rilascia un gettone sull'usage counter globale. [Vedere qui](/user_data_management_project/userdatamgmt_driver.c#L202)
    4. Infine, si alloca e inizializza la struttura [current_message](/user_data_management_project/userdatamgmt_driver.h#L48) che memorizza la posizione (nella lista doppiamente collegata), il puntatore  del messaggio corrente e l'offset in lettura nell'attuale sessione di I/O.
    Il campo offset è posto a 0, position a -1 e curr a NULL. [Vedere qui](/user_data_management_project/userdatamgmt_driver.c#L203)

- int [release](/user_data_management_project/userdatamgmt_driver.c#L156)(struct inode*, struct file*):
    1. Innanzitutto, si verifica che il file system sia stato montato. [Vedere qui](/user_data_management_project/userdatamgmt_driver.c#L167)
    2. Si libera la struct current_message dell'attuale sessione di I/O. [Vedere qui](/user_data_management_project/userdatamgmt_driver.c#L173)
    3. Si decrementa l'usage counter globale e si risveglia il thread in attesa quando si verifica la condizione desiderata. [Vedere qui](/user_data_management_project/userdatamgmt_driver.c#L174)

- ssize_t [read](/user_data_management_project/userdatamgmt_driver.c#L1) (struct file*, char __user*, size_t, loff_t*):
    1. Innanzitutto, si verifica che il file system sia stato montato. [Vedere qui](/user_data_management_project/userdatamgmt_driver.c#L26)
    2. Si verifica che la open almeno una open è stata invocata. [Vedere qui](/user_data_management_project/userdatamgmt_driver.c#L31)
    3. Si verfica che ci siano messaggi validi e se l'offset è minore della dimensione del file. [Vedere qui](/user_data_management_project/userdatamgmt_driver.c#L36)
    4. Si aggiunge 1 a numero di lettori nell'epoca corrente ([Vedere qui](/user_data_management_project/userdatamgmt_driver.c#L47)).
    5. Si verifica se l'offset sia un multiplo di BLK_SIZE. Se il resto con BLK_SIZE è diverso da 0, significa che ci sono dei bytes del blocco precedenti che non sono stati consegnati al lettore. Di conseguenza, a meno che in concorrenza, tra una lettura e la successiva il blocco sia stato invalidato, si continua a leggere il blocco corrente. [Vedere qui](/user_data_management_project/userdatamgmt_driver.c#L49)
    > **NOTE**: Per controllare se il blocco che si sta ri-leggendo è stato nel frattempo invalidato si eseguono i seguenti controlli:
    >- Si verifica se è possibile ri-leggere il primo blocco o meno. Se the->message->curr è uguale a sh_data.first allora si va a ri-leggere l'elemento in testa.
    >-   Si verifica che il messaggio corrente sia uguale a NULL: controllo di consistenza. In questo caso si termina.
    >-   Si verifica che the_message->curr->prev == NULL. Se fosse uguale a NULL significherebbe che lo scrittore ha rilasciato il buffer associato (Il caso in cui the_message->curr->prev == NULL poichè è l'elemento in testa è risolto con il primo if)
    Se entrambe le condizioni sono vere significa che non si vuole ri-leggere il primo blocco e lo scrittore ha rilasciato il buffer.
    >-    Si verifica che the_message->curr->prev->next == the_message->curr. Se fosse vera la disuguaglianza significherebbe che lo scrittore ha linearizzato l'operazione di invalidazione.

    6. Se è la prima lettura, cioè è successiva alla open allora il messaggio corrente è il primo elemento della lista dei messaggi validi. Altrimenti il prossimo messaggio è quello la cui posizione è successiva all'elemento letto in precedenza. In quest'ultimo caso si fa una ricerca lineare nella lista e si ritorno il blocco la cui posizione è maggiore o uguale di quella in input (individuata nella lettura precedente). [Vedere qui](/user_data_management_project/userdatamgmt_driver.c#L62)
    7. Una volta individuato il blocco da leggere, si calcola l'offset e si accede al device. [Vedere qui](/user_data_management_project/userdatamgmt_driver.c#L84). 
    8. Si va a leggere un numero di bytes pari alla lunghezza del messaggio all'interno del blocco, se len è maggiore di str_len. Altrimenti, si leggono len bytes. Infine, si consegnano all'utente attraverso copy_to_user(). [Vedere qui](/user_data_management_project/userdatamgmt_driver.c#118).
    9. Se copy_to_user () ritorna zero allora ho letto tutti i bytes del messaggio nel blocco. Se ritorna un valore diverso da zero allora nella read successiva si andrà a leggere il blocco successvo. Nel primo caso si andrà ad inviduare nella lettura successiva il messaggio valido (Anche un eventuale inserimento linearizzato di un messaggio attravero sys_put_data), la cui posizione sia maggiore o uguale di quella del blocco letto + 1. Di conseguenza, se il messaggio successivo venisse invalidato prima di iniziare la lettura corrispondente, la funzione [lookup_by_pos](/user_data_management_project/userdatamgmt_driver.c#L72) andrà a trovare l'elemento seguente (se esiste) a quello invalidato, che avrà un valore di posizione almeno uguale a quella corrente + 1. Di conseguenza, verrà comunque letto il blocco corretto dal punto di vista della concorrenza.
    > **NOTE**:** Per come è stata implementata l'invalidate (non vado ad aggiornare le posizione dei blocchi successivi a quello invalidato) non è detto che la posizione attuale + 1 corrisponda alla posizione di un messaggio valido all'interno della lista. Tuttavia, essendo sempre presente un ordinamento totale dei messaggi, la condizione di maggiore o uguale (in lookup_by_pos) risolve questo problema, andando ad individuare correttamente il prossimo messaggio da leggere (sia esso aggiunto o eliminato nel frattempo).  
    [Vedere qui](/user_data_management_project/userdatamgmt_driver.c#L132)
    10. Infine, viene individuato, a partire dall'epoca corrente, l'indice all'interno dell'array standing. Viene, quindi, aggiunto 1 nell'entry con l'index trovato. [Vedere qui](/user_data_management_project/userdatamgmt_driver.c#L145)
> **NOTE:** I controlli iniziali nella dev_read sono stati introdotti poichè é richiesto che venga letto il blocco che non è stato invalidato prima dell'accesso in lettua al blocco corrispondente.

## Linearizzabilità e RCU:
In un approccio RCU i lettori eseguono in concorrenza (sia tra loro sia rispetto agli scrittori), senza l'utilizzo dei lock, mentre gli scrittori (sys_put_data e sys_invalidate_data) per poter operare sulla struttura dati condivisa devono acquisire un lock in scrittura. Gli scrittori linearizzano i cambiamenti, rendendoli visibili ai reader atomicamente. Nel caso dell'invalidazione, per evitare il rilascio di un elemento che può essere acceduto ancora da lettori standing, la free del messaggio corrispondente avviene solo dopo che il grace period è terminato ([Vedere qui](/user_data_management_project/userdatamgmt_sc.c#L431)). Il grace period termina quando tutti i lettori che hanno letto la precedente versione della struttura dati condivisa hanno rilasciato un token nell'entry corrispondente dell'array standing.     

Nella sezione [data structures](/README.md#L74) è presente una descrizione dettagliata delle variabili di sincronizzazione utilizzate. I lettori (in sys_get_data e dev_read) vanno ad aumentare di 1 il contatore epoch, che rappresenta il numero di lettori nell'epoca corrente e al termine vanno a rilasciare un "token" nell'entry dell'array standing. L'indice, in standing, è il bit più significativo di epoch. Gli scrittori, invece, acquisicono il lock in scrittura per poter modificare la struttura condivisa. A differenza dell'inserimento, l'invalidazione va a cambiare l'epoca corrente e va in attesa (in polling) che tutti i lettori dell'epoca corrente (coloro che hanno letto la struttura dati prima del punto di materializzazione dell'invalidazione) abbiamo rilasciato il loro token. Inoltre, nella init all'interno della mount viene creato un kernel thread che va a simulare un'operazione di aggiornamento ed update dell'epoca corrente affinchè il contatore epoch non vada in overflow (se il numero di lettori sull'epoca corrente è maggiore di 2^63).

Nel mio caso la struttura dati condivisa è una lista doppiamente collegata per la quale è mantenuto un puntatore alla testa e alla coda. Inoltre, ogni elemento della lista dei messaggi validi ha un riferimento ai metadati all'interno dell'array e vicerversa. Per i readers è necessario che ci sia un solo punto di materializzazione delle modifiche alla struttura dati condivisa. Gli scrittori hanno invece un lock in scrittura di conseguenza i cambiamenti sono visibili al completamento dell'operazione. Nelle scritture dobbiamo modificare i seguenti campi:
- puntatori prev, next e elem in struct [message](/user_data_management_project/userdatamgmt_driver.h#L36) 
- puntatore a msg e i metadati in struct [blk_element](/user_data_management_project/userdatamgmt_driver.h#L29)         

Come eseguire queste modifiche in modo che per i lettori l'aggiornamento sia un unicum atomico? 
Andiamo ad analizzare prima i lettori:
- ssize_t [read](/user_data_management_project/userdatamgmt_driver.c#L1) (struct file*, char __user*, size_t, loff_t*): In questa operazione il lettore accede alla lista dei messaggi validi andando a scorrerla unidirezionalmente dalla testa alla coda. Di conseguenza, la linearizzazione si ha nel momento in cui viene aggiornato il next dell'elemento in coda nel caso di put_data e il next del predecessore del nodo da eliminare nel caso di invalidate_data. La lettura non accede all'array dei metadati. In questo modo si ha solo un'operazione linearizzante.
- int [sys_get_data](/user_data_management_project/userdatamgmt_sc.c#L211) (int offset, char * destination, size_t size): Nella sys_get_data, come sopra, la struttura dati condivisa con i messaggi validi è acceduta unidirezionalmente a partire dalla testa. Anche in questo caso non si accede all'array dei metadati. Così facendo la linearizzazione può essere raggiunta.

Una volta analizzati i lettori è possibile approfondire maggiormente il comportamento degli scrittori:
- int [sys_put_data](/user_data_management_project/userdatamgmt_sc.c#L4)(char* source, size_t size): Sappiamo che la put_data va ad aggiungere un elemento in coda alla lista doppiamente collegata. Prima di aggiornare il puntatore next del predecessore e quindi rendere l'elemento visbiile, è possibile inizializzare il messaggio da inserire:
    - Si imposta next a NULL poichè sarà l'ultimo elemento della lista.
    - Si aggancia il messaggio al blocco corrispondente nell'array. 
    - Si imposta prev al predecessore, cioè all'attuale coda. 
    - Si aggancia il blocco al nuovo messaggio (solo un altro scrittore accede a questo campo all'interno dell'array e sappiamo che tutti gli scrittori sono sincronizzati).      

    Una volta impostati i precedenti campi è possibile agganciare il nuovo elemento andando a cambiare il next del predecessore. D'ora in poi tutti i lettori che accedono alla struttura vedranno questo elemento. 

- int [sys_invalidate_data](/user_data_management_project/userdatamgmt_sc.c#L301) (int offset): Sappiamo che l'invalidate_data, invece, va a sganciare un elemento all'interno della lista dei messaggi validi.                     
Come rendere quest' operazione atomica per i lettori ?
    - Se l'elemento da eliminare è la testa allora viene aggiornato head in modo che punti ad head->next. Ciò è visibile ai lettori immediatamente. Tutti i lettori d'ora in poi vedranno un nuovo elemento come head della lista dei messaggi validi e andranno a scorrere la lista a partire dal prossimo elemento. Il predecessore è NULL, quindi non viene aggiornato. Infine, il prev del successore del nodo da eliminare viene portato a NULL.
    - Se l'elemento da eliminare è in mezzo alla lista allora viene sganciato andando ad aggiornare il puntatore a next del predecessore. Come già detto in precedenza, per costruzione quest'operazione è il punto di linearizzazione per i lettori. Infine, viene aggiornato il puntatore prev del successore. Attraverso il primo swap di pointers, tutti i lettori d'ora in poi non vedranno l'elemento da eliminare.
    - Se l'elemento da eliminare è la coda allora viene aggiornato il next del predecessore in modo che punti a NULL. Infine, viene aggiornata la coda. Questo può essere fatto poichè la tail non è acceduta dai lettori, ma soltanto dagli scrittori.

    Di conseguenza, in tutti e tre i casi, è stata individuata un'operazione che i lettori vedono atomicamente. Nel loro caso è il punto di linearizzazione. 

> **NOTE**: Il corretto funzionamento RCU è stato testato ampiamente andando ad eseguire tutte e tre le system call sullo stesso block del device. L'interleave è stato analizzato andando a vedere i messaggi contenuti nel buffer del kernel.

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
    - [multi-ops](/user_data_management_project/user/Makefile#L36): Il 66% dei threads creati sono readers, il restante sono writers. In base al loro indice, ogni thread esegue una delle tre system call, per REQS volte, su un blocco. L'offset del blocco è generato randomicamente. 
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
4. All'inteno della directory user è presente codice user per utilizzare il modulo in varie modalità (single thread, multi-threads, operazioni multiple su blocchi diversi e molte operazioni sullo stesso blocco). 

Nel Makefile è possibile specificare quale modalità utilizzare e se generare l'eseguibile nella sua versione single-thread o in quella multi-thread. 
> **NOTE:** In versioni diverse del kernel potrebbe essere necessario modificare i numeri delle system call sia nel Makefile che all'interno di /user_data_management_project/user/user.h
