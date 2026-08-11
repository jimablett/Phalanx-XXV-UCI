
/*
 *  The main module:
 *  - Parsing command line parameters
 *  - benchmark
 */

#include "phalanx.h"

uint64_t Wpieces[7] = {0};
uint64_t Bpieces[7] = {0};
uint64_t pawn_attacked_by_w[64] = {0};
uint64_t pawn_attacked_by_b[64] = {0};
uint64_t knight_attacked_by[64] = {0};
uint64_t king_attacked_by[64] = {0};

const char initialpos[] =
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

/* 2005-09-14, José de Paula
 * GCC 3.0 deprecated multi-line strings, and, as of GCC 3.4, they are no longer
 * supported.
 */

char *get_book_file(char *bookdir, char *env_variable, char *path, char *name,
                    int mode) {
  char file[256];
  char *aux;

  if (bookdir) /* Specified in options? */
    path = bookdir;
  else if ((aux = getenv(env_variable))) /* In an environment variable? */
    path = aux;
  else if (!access(name, mode)) /* In the current directory? */
    path = ".";
  else /* Desperacy now, use compile time file. */
    ;

  sprintf(file, "%s/%s", path, name);
  return strdup(file);
}

extern void initdist(void);
extern void initcache(void);
extern void initialize_engine(void);

/* Global configuration for repetition draw avoidance */
int RepetitionAvoidanceThreshold = 300;  /* centipawns */

void initialize_engine(void) {
	
	/* Initialize the random number generator. */
  srand(((unsigned)time(NULL)) + ((unsigned)getpid()));

  setfen(initialpos);
  Counter = 0;
  initbs();
  
  Flag.machine_color = 3;
  Flag.analyze = 0;
  Flag.book = 0;
  Flag.centiseconds = 1000;
  Flag.level = averagetime;
  Flag.ponder = 0;
  Flag.cpu = 0;
  Flag.increment = 0;
  Flag.resign = 0;
  Flag.easy = 0;
  Flag.noise = 50; /* 0.5 s */
  Flag.learn = 1;
  Flag.log = NULL;
  Flag.ponder_option = 1;
  Scoring = 0;
  
  if (Flag.easy) {
    SizeHT = 0;
    Flag.ponder = 0;
    Flag.learn = 0;
  } else if (SizeHT != 0) {
    HT = calloc(SizeHT, sizeof(thashentry));
    if (HT == NULL) {
      puts("cannot alloc hashtable");
      SizeHT = 0;
    }
  }
  
  /* alloc static eval cache */
    initcache();
  /* init distance tables */
    initdist();
}
  
  

int main(int argc, char **argv) {

  int c;
  (void)c;
  char *PbookDir, *SbookDir, *LbookDir, *EcoDir;

  setbuf(stdout, NULL);
  setbuf(stdin, NULL);
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stdin, NULL, _IONBF, 0);


  printf("Phalanx ");
  puts(VERSION);

  if (argc > 1)
    if (strncmp("bcreate\0", argv[1], 8) == 0) {
      return bcreate(argc - 1, argv + 1);
    }

   if (argc > 1)
    if (strncmp("bench\0", argv[1], 5) == 0) {
      Flag.bench = 1;
    }


initialize_engine();

init_skill_system();


#undef debugsee
#ifdef debugsee
  {
    int x;
    int j;
    setfen("q2r2k1/1b3ppp/2prpn2/3P4/2PRP3/4NB2/5PPP/3R2K1 w");
    printboard(NULL);
    for (j = 0; j != 1000000; j++)
      x = see(C6, D5);
    printf(" [%i]\n", see(C6, D5));
    return 0;
  }
#endif



  SbookDir = NULL;
  PbookDir = NULL;
  EcoDir = NULL;
  LbookDir = NULL;

  opterr = 0;

 

  if (Flag.bench) {
    bench();
  }

  /*** Opening book init ***/
  /* Try to open book files.  Give a message if not successful. */
  SbookDir = get_book_file(SbookDir, ENV_SBOOK, SBOOK_DIR, SBOOK_FILE, R_OK);
  Sbook.f = fopen(SbookDir, "rb");
  PbookDir = get_book_file(PbookDir, ENV_PBOOK, PBOOK_DIR, PBOOK_FILE, R_OK);
  Pbook.f = fopen(PbookDir, "rb");
  EcoDir = get_book_file(EcoDir, ENV_ECO, ECO_DIR, ECO_FILE, R_OK);
  Eco = fopen(EcoDir, "rb");
  if (Flag.learn) {
  LbookDir = get_book_file(LbookDir, ENV_LEARN, LEARN_DIR, LEARN_FILE, R_OK | W_OK);
  Learn.f = fopen(LbookDir, "r+");
  if (Learn.f == NULL) {
  
         #define LFSZ 65536
        int b[LFSZ];
        char filename[256];
        memset( b, 0, LFSZ*sizeof(int) );
        sprintf(filename,"./%s",LEARN_FILE);
        free( LbookDir );
        LbookDir = strdup( filename );
        Learn.f = fopen(LbookDir,"w+");
        if( fwrite( b, sizeof(int), LFSZ, Learn.f ) == LFSZ )
        printf("Phalanx: created learn file %s\n",LbookDir);

  }
  } else
    Learn.f = NULL;

  if (Pbook.f != NULL) {
    struct stat fs;
    stat(PbookDir, &fs);
    Pbook.filesize = fs.st_size;
    printf("primary book %s, %d bytes\n", PbookDir, Pbook.filesize);
  }

  if (Sbook.f != NULL) {
    struct stat fs;
    unsigned pos;
    stat(SbookDir, &fs);
    Sbook.filesize = fs.st_size;
    myfread(&pos, sizeof(unsigned), Sbook.f);
    Sbook.firstkey = pos;
    fseek(Sbook.f, Sbook.filesize - 6, SEEK_SET);
    myfread(&pos, sizeof(unsigned), Sbook.f);
    Sbook.lastkey = pos;
    printf("secondary book %s, %d bytes\n", SbookDir, Sbook.filesize);
  }

  if (Learn.f != NULL) {
  struct stat fs;
  stat(LbookDir, &fs);
  Learn.filesize = fs.st_size;
  printf("learning file %s, %d bytes\n", LbookDir, Learn.filesize);
} else {
  // File couldn't be opened - disable learning
  printf("info string WARNING: Could not open learning file, learning disabled\n");
  Flag.learn = 0;
}

  if (Eco != NULL) {
    struct stat fs;
    stat(EcoDir, &fs);
    printf("eco file %s, %i bytes\n", EcoDir, (int)fs.st_size);
  }

  if (Sbook.f == NULL || Pbook.f == NULL) {
   
      printf("Phalanx: ");
    printf("Cannot open ");
  }

  if (Sbook.f == NULL)
    if (Pbook.f == NULL)
      printf("both [%s] and [%s]\n", SBOOK_FILE, PBOOK_FILE);
    else
      printf("[%s]\n", SBOOK_FILE);
  else if (Pbook.f == NULL)
    printf("[%s]\n", PBOOK_FILE);

  if (Learn.f == NULL && Flag.learn) {
    Flag.learn = 0;
    puts("Phalanx: cannot open learn file");
  }

  printf("Phalanx ");
  printf(VERSION);
  if (Flag.easy)
    printf(", easy level %i\n", Flag.easy);
  else
    printf(", %i kB hashtable, %i/%i kB P/S opening book",
           (int)(((1 + SizeHT) * sizeof(thashentry) - 1) / 1024),
           (int)(Pbook.filesize / 1024), (int)(Sbook.filesize / 1024));
  printf("\n");

  /************************************/
  /************************************/
  shell();
  /************************************/
  /************************************/

}
