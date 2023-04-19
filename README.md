# Advanced Operating Systems (and Systems Security) Final Project - A.Y. 2022/2023

## Student:
- Ludovico Zarrelli (ID 0316448)

## Kernel version:
Il modulo seguente per il kernel Linux è stato sviluppato sulla versione 5.15.0-67-generic ed è stato testato sulla versione 4.15.0-54-generic.    
Per semplicità nel repository github è presente anche il modulo per la discovery della system call table e individuazione delle free entries. In questo modo accedendo alla directory root del progetto è possibile eseguire i seguenti comandi per installare il suddetto modulo:

```sh
sudo make build_the_usctm 
sudo make install_the_usctm 
```
Essendo che la composizione della system call table è cambiata nelle precedenti versioni del kernel all'interno della cartella /user_data_management_project/user è necessario apportare le seguenti modifiche:     

1. Nel Makefile bisogna inserire i nuovi system call numbers. Per le 2 versioni precedenti è possibile soltanto decommentare e commentare la dichiarazione delle variabili relative alla versione di kernel di interesse.
2. Fare la stessa cosa anche in user.h, andando a modificare le corrispondenti define.


# Indice
1. [Introduzione](#introduzione)
2. [Data structures](#strutture-dati)
3. [System calls](#implementazione-delle-systemcalls)
4. [File_operations](#implementazione-delle-file_operations-supportate-dal-device-driver)
5. [Linearizzabilità e RCU](#linearizzabilità-e-RCU)
6. [Concorrenza](#concorrenza)
7. [User code](#codice-utente)
8. [Compilazione](#compilazione-ed-esecuzione)

## Introduzione 
Il progetto prevede la realizzazione di un linux device driver che implementi block level maintenance di messaggi utente. Un blocco del block-device ha taglia 4 KB e mantiene 6 (2 bytes per i metadati e 4 bytes per memorizzare la posizione all'interno della lista doppiamente collegata dei messaggi validi) bytes di dati utente e 4 KB - 6 bytes per i metadati. Si richiede l'implementazione di 3 system calls non nativamente supportate dal VFS:
- int  [put_data](/user_data_management_project/userdatamgmt_sc.c#L3)(char* source, size_t size) 
- int [get_data](/user_data_management_project/userdatamgmt_sc.c#L200) (int offset, char * destination, size_t size)
- int [invalidate_data](/user_data_management_project/userdatamgmt_sc.c#L297) (int offset)

e di 3 file operations che driver deve supportare:
- int [open](/user_data_management_project/userdatamgmt_driver.c#L1) (struct inode*, struct file*)
- int [release](/user_data_management_project/userdatamgmt_driver.c#L153)(struct inode*, struct file*)
- ssize_t [read](/user_data_management_project/userdatamgmt_driver.c#L175) (struct file*, char __user*, size_t, loff_t*)

Il device driver deve essere accessibile come file in un file system che supporti le file_operations precedenti. Di conseguenza, si è deciso di  integrare il progetto con il modulo singlefile-FS (che è l'implementazione di un file system accessibile come fosse un file regolare, attraverso l'utilizzo del loop driver) andando ad associare nell'inode del file (che è l'unico file che il FS è in grado di gestire) il puntatore alla struct file_operations (istanziata [qui](/user_data_management_project/userdatamgmt_driver.c#L212)) contenente i riferimenti alle funzioni di driver implementate (vedere il seguente [link](/user_data_management_project/file_system/file.c#L47)).

Pe ridurre la dimensione dell' eseguibile ed evitare la ripetizione di #include nei vari codici sorgenti si è deciso di includere i sorgenti:
- /user_data_management_project/userdatamgmt_driver.c
- /user_data_management_project/file_system/userdatamgmt_fs_src.c
- /user_data_management_project/userdatamgmt_sc.c

all' interno del file /user_data_management_project/userdatamgmt.c nel quale viene fatta l'init e la cleanup del modulo (vedere [qui](/user_data_management_project/userdatamgmt.c#L39)).

## Strutture dati
## Implementazione delle system calls
## Implementazione delle file_operations supportate dal device driver 
## Linearizzabilità e RCU
## Concorrenza
## Codice Utente
## Compilazione ed esecuzione