#ifndef PHALANX_INCLUDED
#define PHALANX_INCLUDED


#include <stdint.h>
#include <math.h> 
#include <time.h>


#include "skill_level.h"

#define ENGNAME "Phalanx"
#define VERSION "XXV UCI JA"


#include <stdbool.h>

#include "fathom/src/tbprobe.h"


// Global seldepth tracker (maximum depth reached in current search)
extern int MaxSeldepth;      // Updated during search
extern int CurrentSeldepth;  // Seldepth for current iteration
extern void update_seldepth(int current_ply);

#define TRACK_SELDEPTH() update_seldepth(Ply)


// ====================================================================
// SYZYGY TABLEBASE SUPPORT (FATHOM)
// ====================================================================

#define SYZYGY_PATH_MAX 2048
#define TB_WIN_SCORE   15000 // Score used for TB Win, slightly below Mate
#define TB_LOSS_SCORE -15000 // Score used for TB Loss
#define TB_DRAW_SCORE 0
#define EN_PASSANT 1

/* Move special flags - MUST match the values used in movegen */
#define SPECIAL_NONE      0
#define SPECIAL_CASTLE   16
#define PROMOTION_Q      32
#define PROMOTION_R      33
#define PROMOTION_B      34
#define PROMOTION_N      35


extern int RepetitionAvoidanceThreshold;  /* centipawns - default 300 */

#define DRAW_AVOIDANCE_DEBUG 0  /* Set to 1 for debug output */
extern int SkillLevel;
   extern int CurrentDepthLimit;
   extern int CurrentEvalNoise;
   extern void set_skill_level(int elo);
   extern int apply_skill_eval_noise(int eval);
   extern int should_apply_skill_depth_limit(int current_depth);
   



// Global variables for TB configuration
extern char SyzygyPath[SYZYGY_PATH_MAX];
extern int SyzygyMaxPieces;
extern bool SyzygyReady;
extern int SyzygyLoadedPieces;
extern int square_to_64(int sq120);

extern int64_t TBHits;
extern int TotalPiecesOnBoard;
void apply_time_controls(int wtime, int btime, int winc, int binc,
                                int movestogo, int depth, int movetime, int infinite,
                                int is_ponderhit);
                                                                              

// Engine-specific wrapper functions
void init_syzygy(const char *path);



#if defined(_MSC_VER)
#define R_OK 04
#define W_OK 02
#define _CRT_SECURE_NO_DEPRECATE
#include <errno.h>
#include <io.h>
#include <process.h>
#endif

#if defined(__linux__) && !defined(__ANDROID__)
//int getopt(int argc, char **argv, char *opts);
extern int opterr, optind, optopt;
extern char *optarg;
#endif


#if defined(_WIN32)
#include <errno.h>
#include <io.h>
#include <process.h>
#include <signal.h>
#include <stdio.h>
#include <windows.h>
#else
#include <sys/time.h>
#endif

#ifdef __ANDROID__
#include <arm_neon.h>
#endif


#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* COMPATIBILITY */
#ifndef CLOCKS_PER_SEC
#define CLOCKS_PER_SEC CLK_TCK
#endif

#if defined(_WIN32)
#ifndef __cplusplus
#define inline __inline
#endif
typedef __int64 int64;
#else
typedef long long int64;
#endif

/* TYPES */
typedef unsigned char tsquare;

/* 64-bit Zobrist key */
typedef unsigned long long hashkey_t;

/*** Move structure ***/
typedef struct {
  unsigned char from, to, special;
  unsigned char in1, in2, in2a;
  signed short value;
  short dch, shift;
} tmove;


typedef struct
{
  int psnl;		/* positional evaluation */
  int devi;		/* deviation to be used in lazy eval, 0 = true eval */
  int check;		/* side to move is in check */
} tsearchnode;



/*** Game node with 64-bit hash ***/
typedef struct {
  tmove m;
  hashkey_t hashboard; // 64-bit
  unsigned short rule50;
  unsigned char castling : 8, check : 8;
  short mtrl, xmtrl;
} tgamenode;

/*** Hash table entry - 24 bytes (aligned) ***/
typedef struct {
  hashkey_t hashboard; // 8 bytes
  short move;          // 2
  char depth;          // 1
  char result;         // 1
  short value;         // 2
  unsigned age;        // 4
  char _pad[2];        // padding to 24 bytes
} thashentry;

/* Positional knowledge */
typedef struct {
  short hung;
  signed char khung, kshield;
  unsigned char p, n, b, r, q;
  unsigned char kp;
  unsigned char r7r;
  signed char prune;
  signed char kstorm, qstorm;
  unsigned char devel;
  unsigned char castling;
  unsigned char bishopcolor, xbishopcolor;
  signed char worsebm;
  unsigned char lpf, rpf;
} tknow;

typedef enum { timecontrol, averagetime, fixedtime, fixeddepth } tlevel;

typedef struct {
  int centiseconds;
  unsigned moves, increment;
  int depth;
  int noise, resign, bench, nps;
  int machine_color, post, book, learn, cpu, ponder, analyze, polling,
      ponder_option;
  tlevel level;
  unsigned char easy;
  FILE *log;
  int move_overhead; // Move overhead in centiseconds
} tflag;

typedef struct {
  unsigned char prev, next;
} tlist;
typedef struct {
  FILE *f;
  unsigned firstkey, lastkey;
  int filesize;
} tsbook;
typedef struct {
  FILE *f;
  int filesize;
} tpbook;
typedef struct {
  unsigned taxi, max, diag, min;
} tdist;

/* DEFINITIONS */
#ifndef VERSION
#define VERSION "unknown"
#endif

#ifndef PBOOK_FILE
#define PBOOK_FILE "pbook.phalanx"
#endif
#ifndef PBOOK_DIR
#define PBOOK_DIR "/usr/local/lib/phalanx"
#endif
#ifndef SBOOK_FILE
#define SBOOK_FILE "sbook.phalanx"
#endif
#ifndef SBOOK_DIR
#define SBOOK_DIR "/usr/local/lib/phalanx"
#endif
#ifndef ECO_FILE
#define ECO_FILE "eco.phalanx"
#endif
#ifndef ECO_DIR
#define ECO_DIR "/usr/local/lib/phalanx"
#endif
#ifndef LEARN_FILE
#define LEARN_FILE "learn.phalanx"
#endif
#ifndef LEARN_DIR
#define LEARN_DIR "/var/local/lib/phalanx"
#endif

#define ENV_PBOOK "PHALANXPBOOKDIR"
#define ENV_SBOOK "PHALANXSBOOKDIR"
#define ENV_LEARN "PHALANXLEARNDIR"
#define ENV_ECO "PHALANXECODIR"

#if !defined(_MSC_VER)
#ifndef max
#define max(x, y) (((x) > (y)) ? (x) : (y))
#endif
#ifndef min
#define min(x, y) (((x) < (y)) ? (x) : (y))
#endif
#endif

#define NOVALUE 32123
#define CHECKMATE 30000
#define MAXPLY 40
#define MAXCOUNTER 8094

#define HASH_COLOR 0x5173C2A8D9E5F1B6ULL
#define no_cut 1
#define alpha_cut 2
#define beta_cut 3

#define SHORT_CASTLING 1
#define LONG_CASTLING 2
#define NULL_MOVE 20

#define WSHORT 1
#define WLONG 2
#define BSHORT 4
#define BLONG 8

#define WHITE 1
#define BLACK 2

#define empty(X) (((X) & 3) == 0)
#define white(X) (((X) & 3) == WHITE)
#define black(X) (((X) & 3) == BLACK)
#define color(X) ((X) & 3)
#define enemy(X) ((X) ^ 3)

#define P_VALUE 100
#define N_VALUE 350
#define B_VALUE 350
#define R_VALUE 550
#define Q_VALUE 1050

#define piece(X) ((X) & 0x7C)
#define PAWN 0x10
#define KNIGHT 0x20
#define BISHOP 0x30
#define ROOK 0x40
#define QUEEN 0x50
#define KING 0x60

#define WP (WHITE | PAWN)
#define WN (WHITE | KNIGHT)
#define WB (WHITE | BISHOP)
#define WR (WHITE | ROOK)
#define WQ (WHITE | QUEEN)
#define WK (WHITE | KING)

#define BP (BLACK | PAWN)
#define BN (BLACK | KNIGHT)
#define BB (BLACK | BISHOP)
#define BR (BLACK | ROOK)
#define BQ (BLACK | QUEEN)
#define BK (BLACK | KING)

#define WPM 0x0001
#define WNM 0x0002
#define WBM 0x0004
#define WRM 0x0008
#define WQM 0x0010
#define WWW 0x0020

#define BPM 0x0100
#define BNM 0x0200
#define BBM 0x0400
#define BRM 0x0800
#define BQM 0x1000
#define BBB 0x2000

/* Files */

#define FILE_A 1
#define FILE_B 2
#define FILE_C 3
#define FILE_D 4
#define FILE_E 5
#define FILE_F 6
#define FILE_G 7
#define FILE_H 8

/* Squares */
#define A1 21
#define B1 22
#define C1 23
#define D1 24
#define E1 25
#define F1 26
#define G1 27
#define H1 28
#define A2 31
#define B2 32
#define C2 33
#define D2 34
#define E2 35
#define F2 36
#define G2 37
#define H2 38
#define A3 41
#define B3 42
#define C3 43
#define D3 44
#define E3 45
#define F3 46
#define G3 47
#define H3 48
#define A4 51
#define B4 52
#define C4 53
#define D4 54
#define E4 55
#define F4 56
#define G4 57
#define H4 58
#define A5 61
#define B5 62
#define C5 63
#define D5 64
#define E5 65
#define F5 66
#define G5 67
#define H5 68
#define A6 71
#define B6 72
#define C6 73
#define D6 74
#define E6 75
#define F6 76
#define G6 77
#define H6 78
#define A7 81
#define B7 82
#define C7 83
#define D7 84
#define E7 85
#define F7 86
#define G7 87
#define H7 88
#define A8 91
#define B8 92
#define C8 93
#define D8 94
#define E8 95
#define F8 96
#define G8 97
#define H8 98
#define H9 99




extern void poll_uci_input(void);
extern int uci_input_ready(void);
extern void send_ponder_info(void);
static int normalmove_score = 0;

static inline void delay_ms(long ms) {
    if (ms <= 0) return;

#ifdef _WIN32
    Sleep(ms);
#else 
    // Linux/POSIX: nanosleep() uses a struct timespec
    struct timespec ts;
    ts.tv_sec = ms / 1000;              // Whole seconds
    ts.tv_nsec = (ms % 1000) * 1000000; // Remaining milliseconds * 1,000,000 = nanoseconds
    nanosleep(&ts, NULL); 
#endif
}



extern long T1,T2;
extern long Time;
extern char *move_to_uci(tmove m);


/* VARIABLES */
static char Inp[8192] = "\0";
extern tflag Flag;
extern tsquare B[120];
extern tlist L[120];
#define WKP (L[1].next)
#define BKP (L[2].next)

extern signed char *Th, *Tv;
extern const char initialpos[];
extern tdist dist[120 * 120];
extern tknow Wknow, Bknow;
extern int Color;
extern int LastIter;
extern int Depth;
extern int Ply;
extern int FollowPV;
extern int Totmat;
extern int Abort, NoAbort;
extern long AllDepth;
extern int64 AllNPS;
extern int DrawScore;
#define DRAW (Ply % 2 ? -DrawScore : DrawScore)

extern tgamenode G[MAXCOUNTER];
extern int Counter;

extern int64 Nodes;
extern int Scoring;
extern int EasyMove;
extern long Otim;
extern void blunder(tmove*,int*);

extern int N_moves[8], RB_dirs[8];
#define K_moves RB_dirs

extern int Values[7];
extern unsigned int P[120];
extern tmove PV[MAXPLY][MAXPLY];
extern tmove Pondermove;

/* Hashing */
extern const unsigned long long H[12][64]; // 64-bit Zobrist
extern thashentry *HT;
extern int *HS;
extern int HP[];
extern size_t SizeHT; // Now size_t
extern unsigned Age;

/* Books */
extern tpbook Pbook;
extern tpbook Learn;
extern tsbook Sbook;
extern FILE *Eco;
extern int Bookout;

/* Signal params */
extern volatile int A_n, A_i, A_d;
extern volatile tmove *A_m;

/* FUNCTIONS */
extern void initbs(void);
extern void myfwrite(void *, int, FILE *);
extern void myfread(void *, int, FILE *);
extern int bcreate(int, char **);
extern void new_game(void);

/* I/O */
extern void printm(tmove, char *);
extern void printmSAN(tmove *, int, int, char *);
extern void printPV(int, int, char *);
extern void infoline(int, char *);
extern void printboard(char *);
extern int setfen(const char *);
extern void shell(void);
extern int command(void);
extern tmove *sandex(char *, tmove *, int);
extern tsearchnode S[MAXPLY];       /* the current node is S[Ply] */

/* Signal */
extern void interrupt(int);

/* Move gen */
extern int attacktest(int, int);
#define checktest(side) attacktest((side) == WHITE ? WKP : BKP, enemy(side))
extern void generate_legal_moves(tmove *, int *, int);
extern void generate_legal_captures(tmove *, int *, int);
extern void generate_legal_checks(tmove *, int *);
extern int see(tsquare *, int, int);

/* Search */
extern int search(tmove *, int, int, int);
extern int csearch(tmove *, int, int, int, int);
extern tmove root_search(void);

/* Eval */
extern int repetition(int);
extern int material_draw(void);
extern int evaluate(int, int);
extern int score_position(void);

/* Endgame */
extern int pawns(void);
extern int e_nb(int);
extern int e_mp(void);
extern int e_rpr(void);

/* Move exec */
extern void do_move(tmove *);
extern void undo_move(tmove *);

/* Hash */
extern hashkey_t hashboard(void);
extern thashentry *seekHT(void);
extern void writeHT(int, int, int);

/* Levels */
extern void l_level(char *);
extern void l_startsearch(void);
extern int l_iterate(void);
extern long ptime(void);
extern long LastTurn;
extern int Turns;

/* Book */
extern int bookmove(tmove *, int);
extern unsigned smove(tmove *);

/* Learn */
extern int rlearn(void);
extern void wlearn(int, int);

/* Killer */
extern void init_killers(void);
extern void write_killer(int, int);
extern void add_killer(tmove *, int, thashentry *);
extern void slash_killers( tmove *, int );

/* UCI */
extern int uci_command();
extern void uci_send_id(void);
extern void uci_send_options(void);
extern void uci_send_uciok(void);
extern void uci_send_readyok(void);
extern void uci_send_bestmove(tmove m, tmove ponder, int real_move);
extern void uci_send_info(int depth, int score, long time_cs, int64_t nodes, int nps,tmove *pv, int pvlen);
extern int uci_parse_move(char *, tmove *, int);
extern void uci_position(char *);
extern void uci_go(char *);
extern void uci_setoption(char *);
extern int uci_mode; 

extern int terminal(void);


// Bitboards: index 0=unused, 1=pawn,2=knight,3=bishop,4=rook,5=queen,6=king
extern uint64_t Wpieces[7], Bpieces[7];

// Precomputed attacker masks (standard A1=bit0, H8=bit63)
extern uint64_t pawn_attacked_by_w[64];
extern uint64_t pawn_attacked_by_b[64];
extern uint64_t knight_attacked_by[64];
extern uint64_t king_attacked_by[64];

/* Saved from the last normal "go" – used on ponderhit */
static int saved_wtime = -1, saved_btime = -1;
static int saved_winc = 0, saved_binc = 0;
static int saved_movestogo = 0;
static int saved_depth = -1;
static int saved_movetime = -1;
static int saved_infinite = 0;
static int saved_is_ponderhit = 0;


static inline void bench();

static inline void bench() {
	
#define NPOS 10 /* number of positions */
  int i;
  long tim;
  int64 allnodes = 0;
  const char *positions[NPOS] = {
      initialpos,
      "2kr3r/pp3Npp/2pbbn2/6B1/2BPp3/6Pq/PPP1Q2P/2KR3R b",
      "r1bq1r1k/2pn2bp/1p1p1np1/pN1Pp3/1PP1Pp2/P2B1P2/1BQN2PP/1R3RK1 b",
      "r4rk1/2p1p1b1/p1n3pp/1p1qP3/P1pP4/B1P2PPN/4Q1K1/R6R b",
      "8/1p3rpp/p2nk3/P3p3/2R5/2N2PP1/1P2K2P/8 w",
      "rnbr2k1/ppq1ppbp/6p1/2p5/3P4/2PBPN2/P4PPP/1RBQ1RK1 w",
      "3r1rk1/1q2b1pp/pn3p2/1pp5/4PB2/5N2/PP1RQPPP/3R2K1 w",
      "8/2p5/5pK1/5R2/4k2P/p4P2/1r6/8 b",
      "3r1rk1/p3qp1p/2bb2p1/2pp4/8/1P2P3/PBQN1PPP/2R2RK1 b",
      "k7/ppp5/8/8/8/8/P1P5/K7 w"};

  Flag.cpu = 1;
  Flag.level = fixedtime;
  Flag.centiseconds = 300;
  Flag.book = Flag.learn = Flag.ponder = Flag.analyze = 0;
  printf("Running benchmark, this will take 30 seconds of CPU time.\n");
  printf("----------");
  tim = ptime();

  for (i = 0; i != NPOS; i++) {
    setfen(positions[i]);
    if (i != 0)
      printf("+");
    
    root_search();
    // if( Abort == 1 ) return 0;
    allnodes += Nodes;
  }
  printf("+\n%10i nodes per second\n", (int)(100 * allnodes / (ptime() - tim)));

  exit(0);
}

// Maps 120sq -> 64bit (A1=21->0, H8=98->63)
static inline int mapsq(int sq) {
  return ((sq / 10) - 2) * 8 + ((sq % 10) - 1);
}

/* Helper: update material and hash after move */
static inline void update_material_and_hash(void) {
  int s;
  G[Counter].hashboard = hashboard();
  G[Counter].mtrl = G[Counter].xmtrl = 0;
  for (s = A1; s != H9; s++) {
    if (B[s] == 0 || B[s] == 3)
      continue;
    int pc = B[s] >> 4;
    int val = Values[pc];
    if (color(B[s]) == Color)
      G[Counter].mtrl += val;
    else
      G[Counter].xmtrl += val;
  }
}

/* Rebuild piece lists */
static inline void rebuild_piece_lists(void) {
  int wlast = 1, blast = 2;
  L[1].next = L[2].next = 0;
  for (int s = A1; s != H9; s++) {
    if (B[s] == 0 || B[s] == 3 || B[s] == WK || B[s] == BK)
      continue;
    if (color(B[s]) == WHITE) {
      L[wlast].next = s;
      L[s].prev = wlast;
      L[s].next = 0;
      wlast = s;
    } else {
      L[blast].next = s;
      L[s].prev = blast;
      L[s].next = 0;
      blast = s;
    }
  }
}




/* Check if time is up (used in ponder and normal search) */

/* --------------------------------------------------------------
 *  Hard-time-limit check – works for normal search *and* pondering
 * -------------------------------------------------------------- */
static inline int time_is_up(void) {
    if (Flag.level != timecontrol && Flag.level != fixedtime)
        return 0;

    long elapsed = ptime() - T1;
    if (elapsed < 0) elapsed = 0;

    return elapsed >= Flag.centiseconds;
}


#endif
