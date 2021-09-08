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

//my definitions:

//special handlers
//#define SIG_DFL  0 /* default signal handling */
//#define SIG_IGN  1 /* ignore signal */

#define SIG_DFL (void (*) ()) 0 /* default signal handling */
#define SIG_IGN (void (*) ()) 1 /* ignore signal */

//sig nums
#define SIGKILL 9
#define SIGSTOP 17
#define SIGCONT 19

//useful defines
#define NUM_OF_SIGNALS 32
#define MIN_SIG  0
#define MAX_SIG  31

#define NTHREAD 8

#define NBSEM 10

#define MAX_STACK_SIZE 4000

#define FREE 0

#define LOCKED 1

#define DEBUG 0



