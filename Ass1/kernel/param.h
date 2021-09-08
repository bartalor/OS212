#define NPROC        64  // maximum number of processes
#define NCPU          8  // maximum number of CPUs
#define NOFILE       16  // open files per process
#define NFILE       100  // open files per system
#define NINODE       50  // maximum number of active i-nodes
#define NDEV         10  // maximum major device number
#define ROOTDEV       1  // device number of file system root disk
#define MAXARG       32  // max exec arguments
#define MAXOPBLOCKS  10  // max # of blocks any FS op writes
#define LOGSIZE      (MAXOPBLOCKS*3)  // max data blocks in on-disk log
#define NBUF         (MAXOPBLOCKS*3)  // size of disk block cache
#define FSSIZE       1000  // size of file system in blocks
#define MAXPATH      128   // maximum file path name
#define QUANTUM      5 //* QUANTUM Intialize
#define ALPHA 50
#define DEFAULT_BURSTTIME 100*QUANTUM

// priority decay constanst:
#define TEST_HIGH_PRIORITY 1
#define HIGH_PRIORITY 3
#define NORMAL_PRIORITY 5
#define LOW_PRIORITY 7 
#define TEST_LOW_PRIORITY 25