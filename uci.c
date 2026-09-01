
#include "phalanx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h> // For isspace

/* UCI Protocol Implementation for Phalanx */

extern long Time;
extern char Inp[8192];
extern tmove Pm[8192];
extern int Pn;
extern tmove PV[MAXPLY][MAXPLY];


static int skill_elo = DEFAULT_SKILL_ELO;  // Initialize to 2700
static const char* desc = "Maximum Strength";  // Initialize to default description


// Define a static global variable to store the last FEN
static char last_fen[512] = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

int uci_mode = 0;

int64_t TBHits = 0;

// NEW: Separate ponder capability from active state
int Flag_ponder_enabled = 0;  // Set by "setoption name Ponder"

// NEW: SYZYGY TB GLOBALS 
char SyzygyPath[SYZYGY_PATH_MAX] = "";
int SyzygyMaxPieces = 4; // Default to 4-man TBs (6 or 7 if preferred)
bool SyzygyReady = false;
int SyzygyLoadedPieces = 0;

bool tb_init_impl(const char *_path);

extern void initcache(void);
extern void initdist(void);
extern void initialize_engine(void);


// Helper function to initialize Fathom

void init_syzygy(const char *path) {
    if (SyzygyReady) {
        // Typically you'd call tb_free() here, but Fathom is safe without it.
        SyzygyReady = false;
        SyzygyLoadedPieces = 0;
    }

    if (path == NULL || *path == '\0' || strcmp(path, "<empty>") == 0) {
        printf("info string Syzygy tablebase support disabled.\n");
        SyzygyLoadedPieces = 0;
        SyzygyPath[0] = '\0';
        fflush(stdout);
        return;
    }

    // Save path
    strncpy(SyzygyPath, path, SYZYGY_PATH_MAX - 1);
    SyzygyPath[SYZYGY_PATH_MAX - 1] = '\0';

    printf("info string Attempting to initialize Syzygy from path: %s\n", SyzygyPath);
    fflush(stdout);

    // Initialize Fathom
    SyzygyLoadedPieces = tb_init(SyzygyPath);
	
    // Fathom writes the largest usable table size into TB_LARGEST
    extern unsigned TB_LARGEST;
    printf("info string TB_LARGEST after init (WDL-only tables inflate this correctly): %u\n", TB_LARGEST);
    fflush(stdout);

    /*
        FIX:

        Fathom returns SyzygyLoadedPieces = 1 if no DTZ tables exist
        — and DTZ starts only at 5-man.

        Therefore:

        - For 3- and 4-man WDL-only sets, SyzygyLoadedPieces == 1
          but TB_LARGEST == 3 or 4

        So we use TB_LARGEST as the real “max piece count” whenever
        only WDL tables exist.
    */

    int effective_loaded = SyzygyLoadedPieces;

    if (SyzygyLoadedPieces <= 1 && TB_LARGEST >= 3) {
        effective_loaded = TB_LARGEST;
    }

    if (effective_loaded >= 3) {
        // WDL-only or WDL+DTZ, both valid
        SyzygyReady = true;
        SyzygyLoadedPieces = effective_loaded;

        if (effective_loaded < 5) {
            printf("info string Syzygy WDL tables detected (DTZ starts at 5-man).\n");
            printf("info string Loaded WDL up to %d-man tables.\n", effective_loaded);
        } else {
            printf("info string Syzygy tablebases initialized. Loaded up to %d-piece DTZ/WDL tables.\n",
                   effective_loaded);
        }
		

        printf("info string Will probe tables with %d pieces or fewer.\n",
               min(SyzygyMaxPieces, SyzygyLoadedPieces));
    }
    else {
        // Nothing found
        printf("info string WARNING: No Syzygy tables detected.\n");
        printf("info string Path was: %s\n", SyzygyPath);
        printf("info string Note: 3- and 4-man tables are WDL-only (no DTZ files).\n");
        SyzygyPath[0] = '\0';
        SyzygyReady = false;
        SyzygyLoadedPieces = 0;
    }

    fflush(stdout);
}



void uci_send_id(void) {
  printf("id name %s %s\n", ENGNAME, VERSION);
  printf("id author Dusan Dobes\n");
  
}


void uci_send_options(void) {
    printf("option name Hash type spin default 64 min 1 max 16384\n");
    fflush(stdout);
    
    printf("option name Move Overhead type spin default 10 min 0 max 5000\n");
    fflush(stdout);

    /* Skill Level – combo type for proper 100-increment in Arena GUI */
    printf("option name SkillLevel type combo default 2700 "
           "var 1000 var 1100 var 1200 var 1300 var 1400 var 1500 "
           "var 1600 var 1700 var 1800 var 1900 var 2000 var 2100 "
           "var 2200 var 2300 var 2400 var 2500 var 2600 var 2700\n");
    fflush(stdout);

    printf("option name OwnBook type check default false\n");
    fflush(stdout);
    
    printf("option name Ponder type check default false\n");
    fflush(stdout);
    
    printf("option name RepetitionThreshold type spin default 300 min 0 max 2000\n");
    fflush(stdout);

    /* CRITICAL: Arena on Linux has issues with "string" type options with no value */
    /* Use a placeholder default path instead of <empty> */
    printf("option name SyzygyPath type string default ./tablebase\n");
    fflush(stdout);
    
    printf("option name SyzygyMaxPieces type spin default 4 min 0 max 7\n");
    fflush(stdout);
    
    printf("option name Learning type check default false\n");
    fflush(stdout);
    
    printf("option name UCI_EngineAbout type string default %s %s by Dusan Dobes\n",
           ENGNAME, VERSION);
    fflush(stdout);
}

void uci_send_uciok(void) {
  printf("uciok\n");
  
}

void uci_send_readyok(void) {
  printf("readyok\n");
  
}


// Helper function to get the move representation for castling
const char* get_castling_move(int special, int move_side) {
    if (move_side == WHITE) {
        if (special == LONG_CASTLING) return "e1c1";
        if (special == SHORT_CASTLING) return "e1g1";
    } else { // BLACK
        if (special == LONG_CASTLING) return "e8c8";
        if (special == SHORT_CASTLING) return "e8g8";
    }
    return NULL; // Should not happen
}



/* -------------------------------------------------------------
   uci_send_bestmove – fixed version
   ------------------------------------------------------------- */

void uci_send_bestmove(tmove m, tmove ponder, int real_move) {
	
	int move_side;
    
    #ifdef DEBUG
    printf("info string uci_send_bestmove: ENTRY\n");
    printf("info string   m.from=%d m.to=%d special=%d\n", m.from, m.to, m.special);
    printf("info string   ponder.from=%d ponder.to=%d\n", ponder.from, ponder.to);
    printf("info string   Flag_ponder_enabled=%d\n", Flag_ponder_enabled);
    printf("info string   real_move=%d\n", real_move);
    #endif
    fflush(stdout);

    // --- Format the move string ---
    extern const char file[10], row[12], piece[7];
    char move_str[12] = {0};
    char *p = move_str;

    // Determine which side should be used for formatting
	
	if (Flag.machine_color == WHITE)  {
		move_side = WHITE;  // Default to current side
	}else{
		move_side = BLACK;
	}
	
 
    const char* castling_move = NULL;

    // Handle castling specially using helper function
    if (m.special == SHORT_CASTLING || m.special == LONG_CASTLING) {
        // Complex logic for which side to use for castling formatting
        if ((Flag.book && !Flag.learn) || (CurrentDepthLimit < 9999)) {
            // When using book or skill level, use move_color from piece
            int move_color = color(m.in1);
            castling_move = get_castling_move(m.special, move_color);
        } else {
            // When learning, use current side
            castling_move = get_castling_move(m.special, move_side);
        }
        
        if (castling_move) {
            strcpy(move_str, castling_move);
            #ifdef DEBUG
            printf("info string Formatting castling as %s\n", move_str);
            #endif
        }
        fflush(stdout);
    } else {
        // Normal move
        *p++ = file[m.from % 10];
        *p++ = row[m.from / 10];
        *p++ = file[m.to % 10];
        *p++ = row[m.to / 10];

        // Add promotion piece if present
        if (m.in2a != m.in1) {
            int promoted_piece = (m.in2a >> 4) & 0xF;
            *p++ = tolower(piece[promoted_piece]);
        }
        *p = '\0';
    }

    #ifdef DEBUG
    printf("info string   move_str=%s\n", move_str);
    #endif
    fflush(stdout);

    printf("bestmove %s", move_str);
    
    // Send ponder move if available and enabled
    if (Flag_ponder_enabled && ponder.from != 0 && ponder.from != ponder.to) { 
        char ponder_str[12] = {0};
        char *q = ponder_str;
        
        // For ponder moves, always use opponent's side
        int ponder_side = enemy(Color);
        const char* ponder_castling = NULL;

        if (ponder.special == SHORT_CASTLING || ponder.special == LONG_CASTLING) {
            ponder_castling = get_castling_move(ponder.special, ponder_side);
            if (ponder_castling) {
                strcpy(ponder_str, ponder_castling);
            }
        } else {
            *q++ = file[ponder.from % 10];
            *q++ = row[ponder.from / 10];
            *q++ = file[ponder.to % 10];
            *q++ = row[ponder.to / 10];

            if (ponder.in2a != ponder.in1) {
                int pp = ponder.in2a >> 4;
                if (pp >= QUEEN && pp <= KNIGHT) {
                    *q++ = tolower(piece[pp]);
                }
            }
            *q = '\0';
        }
        
        printf(" ponder %s", ponder_str); 
        printf("\n");
        #ifdef DEBUG
        printf("info string Sent bestmove with ponder: %s\n", ponder_str);
        #endif
        fflush(stdout);
    } else {
        printf("\n");   
        #ifdef DEBUG
        printf("info string Sent bestmove WITHOUT ponder (enabled=%d from=%d to=%d)\n",
               Flag_ponder_enabled, ponder.from, ponder.to);
        #endif
        fflush(stdout);
    }

    // Clear ponder state if real move
    if (real_move) { 
        Pondermove.from = 0;
        Pondermove.to = 0;
        Pondermove.in1 = 0;
        Pondermove.in2a = 0;
        Pondermove.special = 0;
    }

    // Clear PV ponder move
    PV[0][1].from = 0; 
    PV[0][1].to = 0;
    PV[0][1].in1 = 0;
    PV[0][1].in2a = 0;
    PV[0][1].special = 0;
}




/* ------------------------------------------------------------------
 * uci_send_info -- corrected, robust UCI info printer
 * ------------------------------------------------------------------ */
void uci_send_info(int depth, int score, long time_cs, int64_t nodes, int nps,
                   tmove *pv, int pvlen)
{
    extern const char file[10], row[12], piece[7];
	extern int CurrentSeldepth;  // Use the current iteration's seldepth

    if (!Flag.ponder) {
        normalmove_score = score;
    }
    int display_score = Flag.ponder ? normalmove_score : score;

#ifdef DEBUG_MATE
    if (abs(display_score) >= CHECKMATE - MAXPLY) {
        printf("DEBUG_MATE: depth=%d score=%d pvlen=%d first_from=%d first_to=%d\n",
               depth, display_score, pvlen,
               pv && pvlen > 0 ? pv[0].from : -1,
               pv && pvlen > 0 ? pv[0].to   : -1);
        fflush(stdout);
    }
#endif

    /* CRITICAL: Use CurrentSeldepth instead of depth for seldepth field */
    printf("info depth %d seldepth %d score ", depth, CurrentSeldepth);

    /* Correct mate detection using MAXPLY */
    if (display_score >= CHECKMATE - MAXPLY) {
        /* side to move delivers mate */
        int mate_in = (CHECKMATE - display_score + 1) / 2;
        printf("mate %d", mate_in);
    }
    else if (display_score <= -CHECKMATE + MAXPLY) {
        /* side to move is getting mated */
        int mate_in = (CHECKMATE + display_score + 1) / 2;
        printf("mate %d", -mate_in);
    }
    else {
        printf("cp %d", display_score);
    }

    /* Basic info */
    printf(" time %ld nodes %lld", time_cs * 10, (long long)nodes);
    if (nps > 0) printf(" nps %d", nps);
    if (TBHits > 0) printf(" tbhits %lld", (long long)TBHits);

    /* PV printing */
    int is_mate = (display_score >= CHECKMATE - MAXPLY ||
                   display_score <= -CHECKMATE + MAXPLY);

    int have_pv = (pv != NULL && pvlen > 0 &&
                   (pv[0].from != 0 || pv[0].to != 0 || is_mate));

    if (have_pv) {
        printf(" pv");
        int printed = 0;
        int side = Color; /* current side to move */

        for (int i = 0; i < pvlen; i++) {
            if (pv[i].from == 0 && pv[i].to == 0) break;

            printf(" ");
            const tmove *m = &pv[i];

            if (m->special == LONG_CASTLING) {
                printf(side == WHITE ? "e1c1" : "e8c8");
            }
            else if (m->special == SHORT_CASTLING) {
                printf(side == WHITE ? "e1g1" : "e8g8");
            }
            else {
                char from_file = file[m->from % 10];
                char from_rank = row[m->from / 10];
                char to_file   = file[m->to   % 10];
                char to_rank   = row[m->to   / 10];

                printf("%c%c%c%c", from_file, from_rank, to_file, to_rank);

                /* Promotion */
                if (m->in2a != m->in1 && m->in2a != 0) {
                    char prom = tolower(piece[(m->in2a >> 4) & 0xF]);
                    printf("%c", prom);
                }
            }

            printed++;
            side = enemy(side);
        }

        /* Safety net – GUI gets confused if "mate X" has no PV */
        if (is_mate && printed == 0) {
            printf(" <NO_PV_BUT_MATE>");
        }
    }
#ifdef DEBUG_MATE
    else if (is_mate) {
        printf(" pv <MISSING! pvlen=%d>", pvlen);
    }
#endif

    printf("\n");
    fflush(stdout);
}



int uci_parse_move(char *movestr, tmove *m, int n) {
  // CRITICAL: Validate input
  if (movestr == NULL || strlen(movestr) < 4) {
    printf("info string ERROR: Invalid move string (NULL or too short)\n");
    fflush(stdout);
    return -1;
  }

  // Extract coordinates with bounds checking
  if (movestr[0] < 'a' || movestr[0] > 'h' ||
      movestr[1] < '1' || movestr[1] > '8' ||
      movestr[2] < 'a' || movestr[2] > 'h' ||
      movestr[3] < '1' || movestr[3] > '8') {
    printf("info string ERROR: Invalid move coordinates '%s'\n", movestr);
    fflush(stdout);
    return -1;
  }

  int from_file = movestr[0] - 'a' + 1;
  int from_row  = movestr[1] - '1' + 2;
  int to_file   = movestr[2] - 'a' + 1;
  int to_row    = movestr[3] - '1' + 2;
  char promo    = (strlen(movestr) >= 5) ? movestr[4] : 0;

  int from_sq = from_file + from_row * 10;
  int to_sq   = to_file + to_row * 10;

  // Additional validation: check if squares are within board
  if (from_sq < A1 || from_sq > H8 || to_sq < A1 || to_sq > H8) {
    printf("info string ERROR: Move squares out of bounds (%d to %d)\n", from_sq, to_sq);
    fflush(stdout);
    return -1;
  }

  // CRITICAL: Check what piece is actually on the from square
  extern tsquare B[120];
  int piece_on_square = B[from_sq];
  #ifdef DEBUG
  printf("info string DEBUG: Parsing '%s' - from_sq=%d has piece=0x%X (%s)\n", 
         movestr, from_sq, piece_on_square,
         piece_on_square == 0 ? "EMPTY" : 
         (piece(piece_on_square) == KING) ? "KING" :
         (piece(piece_on_square) == QUEEN) ? "QUEEN" :
         (piece(piece_on_square) == ROOK) ? "ROOK" :
         (piece(piece_on_square) == BISHOP) ? "BISHOP" :
         (piece(piece_on_square) == KNIGHT) ? "KNIGHT" :
         (piece(piece_on_square) == PAWN) ? "PAWN" : "UNKNOWN");
  #endif
  fflush(stdout);

  // Castling detection - ONLY treat as castling if King is actually on e1/e8
  // Otherwise it's just a normal piece move (like a Rook moving e8-c8 after castling)
  if (from_sq == E1 && (to_sq == G1 || to_sq == C1)) {
    if (piece(piece_on_square) == KING) {
      // This is a castling move
      #ifdef DEBUG
	  printf("info string DEBUG: Detected WHITE castling: %s\n", movestr);
	  #endif
      fflush(stdout);
      for (int i = 0; i < n; i++) {
        if (to_sq == G1 && m[i].special == SHORT_CASTLING) {
          #ifdef DEBUG
		  printf("info string DEBUG: Matched WHITE SHORT castling\n");
		  #endif
          fflush(stdout);
          return i;
        }
        if (to_sq == C1 && m[i].special == LONG_CASTLING) {
          #ifdef DEBUG
		  printf("info string DEBUG: Matched WHITE LONG castling\n");
		  #endif
          fflush(stdout);
          return i;
        }
      }
      #ifdef DEBUG
	  printf("info string ERROR: Castling move %s not in legal moves\n", movestr);
	  #endif
      fflush(stdout);
      return -1;
    }
    // Not a King move, fall through to normal move matching
    #ifdef DEBUG
	printf("info string DEBUG: %s looks like castling but piece on e1 is not King, treating as normal move\n", movestr);
	#endif
    fflush(stdout);
  }
  
  if (from_sq == E8 && (to_sq == G8 || to_sq == C8)) {
    if (piece(piece_on_square) == KING) {
      // This is a castling move
      #ifdef DEBUG
	  printf("info string DEBUG: Detected BLACK castling: %s\n", movestr);
	  #endif
      fflush(stdout);
      for (int i = 0; i < n; i++) {
        if (to_sq == G8 && m[i].special == SHORT_CASTLING) {
          #ifdef DEBUG
		  printf("info string DEBUG: Matched BLACK SHORT castling\n");
		  #endif
          fflush(stdout);
          return i;
        }
        if (to_sq == C8 && m[i].special == LONG_CASTLING) {
          #ifdef DEBUG
		  printf("info string DEBUG: Matched BLACK LONG castling\n");
		  #endif
          fflush(stdout);
          return i;
        }
      }
      #ifdef DEBUG
	  printf("info string ERROR: Castling move %s not in legal moves\n", movestr);
	  #endif
      fflush(stdout);
      return -1;
    }
    // Not a King move, fall through to normal move matching
    #ifdef DEBUG
	printf("info string DEBUG: %s looks like castling but piece on e8 is not King, treating as normal move\n", movestr);
	#endif
    fflush(stdout);
  }

  // Handle promotions
  int expected_piece_idx = 0;
  if (promo != 0) {
    char p = tolower(promo);
    if (p == 'q') expected_piece_idx = 5;
    else if (p == 'r') expected_piece_idx = 4;
    else if (p == 'b') expected_piece_idx = 3;
    else if (p == 'n') expected_piece_idx = 2;
    else {
      #ifdef DEBUG
	  printf("info string ERROR: Invalid promotion piece '%c'\n", promo);
	  #endif
      fflush(stdout);
      return -1;
    }
  }

  // Find matching move in legal move list
  // Match ONLY if piece types match
  int found_count = 0;
  int found_idx = -1;
  for (int i = 0; i < n; i++) {
    if (m[i].from == from_sq && m[i].to == to_sq) {
      // For promotions, check promotion piece
      if (promo == 0 || (m[i].in2a >> 4) == expected_piece_idx) {
        found_count++;
        found_idx = i;
        #ifdef DEBUG
		printf("info string DEBUG: Found candidate at index %d: piece=0x%X, special=%d\n",
               i, m[i].in1, m[i].special);
		#endif
        fflush(stdout);
      }
    }
  }
  
  if (found_count == 1) {
    #ifdef DEBUG
	printf("info string DEBUG: Matched move '%s' uniquely at index %d\n", movestr, found_idx);
	#endif
    fflush(stdout);
    return found_idx;
  } else if (found_count > 1) {
    #ifdef DEBUG
	printf("info string WARNING: Ambiguous move '%s' - %d matches found, using first\n", 
           movestr, found_count);
	#endif
    fflush(stdout);
    return found_idx;
  }
  
  #ifdef DEBUG
  printf("info string ERROR: No match for move '%s' (from=%d to=%d)\n", 
         movestr, from_sq, to_sq);
  #endif
  fflush(stdout);
  return -1;
}



void uci_position(char *cmd) {
    if (Flag.analyze || Flag.machine_color != 0) {
        #ifdef DEBUG
		printf("info string Stopping ongoing search for new position\n");
		#endif
        fflush(stdout);
        
        Flag.analyze = 0;
        Abort = 1;
        NoAbort = 0;
        
        // Wait for search to actually stop
        while (Flag.machine_color != 0) {
            poll_uci_input();
            if (!Abort) break;
            #ifdef _WIN32
            usleep(1000);
            #else
            sleep(1000);
            #endif
        }

        extern int64 Nodes;
        int64 last_nodes = Nodes;
        int stable_count = 0;
        for (int wait = 0; wait < 100; wait++) {
            for (volatile int i = 0; i < 1000000; i++);
            if (Nodes == last_nodes) {
                if (++stable_count >= 3) break;
            } else {
                stable_count = 0;
                last_nodes = Nodes;
            }
        }
        #ifdef DEBUG
		printf("info string Search stopped (Nodes=%lld), loading position\n", (long long)Nodes);
		#endif
        fflush(stdout);
    }

    // Clear PV
    for (int i = 0; i < MAXPLY; i++)
        for (int j = 0; j < MAXPLY; j++)
            PV[i][j].from = 0;

    // Clear hash table before applying moves to avoid stale data
    // from previous search corrupting move generation
    if (HT && SizeHT > 0) {
        memset(HT, 0, SizeHT * sizeof(thashentry));
    }

    // Use much larger buffer for move list parsing
    char *moves_list[1024];  // Increased from 512
    int num_moves = 0;
    char fen[512] = "";
    int parsing_moves = 0;

    // Parse the command more carefully
    char *p = cmd;
    
    // Skip "position"
    while (*p && !isspace(*p)) p++;
    while (*p && isspace(*p)) p++;
    
    if (!*p) return;
    
    // Check for startpos or fen
    if (strncmp(p, "startpos", 8) == 0) {
        setfen(initialpos);
        p += 8;
        while (*p && isspace(*p)) p++;
    } else if (strncmp(p, "fen", 3) == 0) {
        p += 3;
        while (*p && isspace(*p)) p++;
        
        // Parse FEN (up to "moves" keyword)
        char *fen_start = p;
        while (*p && strncmp(p, "moves", 5) != 0) {
            p++;
        }
        
        // Extract FEN string
        int fen_len = p - fen_start;
        if (fen_len > 0 && fen_len < sizeof(fen) - 1) {
            strncpy(fen, fen_start, fen_len);
            fen[fen_len] = '\0';
            
            // Trim trailing whitespace
            char *end = fen + fen_len - 1;
            while (end >= fen && isspace(*end)) {
                *end = '\0';
                end--;
            }
            
            strncpy(last_fen, fen, sizeof(last_fen) - 1);
            last_fen[sizeof(last_fen) - 1] = '\0';
            setfen(fen);
        }
    } else {
        return;
    }
    
    // Check for moves keyword
    if (strncmp(p, "moves", 5) == 0) {
        p += 5;
        while (*p && isspace(*p)) p++;
        parsing_moves = 1;
    }
    
    // Parse move list
    if (parsing_moves && *p) {
        while (*p && num_moves < 1024) {
            // Skip whitespace
            while (*p && isspace(*p)) p++;
            if (!*p) break;
            
            // Extract move (letters and digits only)
            char move_buf[16];
            int move_len = 0;
            
            while (*p && !isspace(*p) && move_len < 15) {
                move_buf[move_len++] = *p++;
            }
            move_buf[move_len] = '\0';
            
            if (move_len >= 4) {  // Valid move must be at least 4 chars
                moves_list[num_moves] = strdup(move_buf);
                num_moves++;
            } else {
                #ifdef DEBUG
				printf("info string WARNING: Skipping invalid move '%s'\n", move_buf);
				#endif
                fflush(stdout);
            }
        }
    }
    #ifdef DEBUG
    printf("info string Parsed %d moves from position command\n", num_moves);
	#endif
    fflush(stdout);

    // Apply moves one by one with validation
    int moves_applied = 0;
    for (int i = 0; i < num_moves; i++) {
        tmove legal_moves[512];
        int n_moves;
        generate_legal_moves(legal_moves, &n_moves, checktest(Color));
        
        // Validate move string length before parsing
        if (strlen(moves_list[i]) < 4) {
            #ifdef DEBUG
			printf("info string ERROR: Move %d '%s' is too short (min 4 chars)\n", 
                   i + 1, moves_list[i]);
			#endif
            fflush(stdout);
            free(moves_list[i]);
            continue;
        }
        
        int idx = uci_parse_move(moves_list[i], legal_moves, n_moves);
        
        if (idx >= 0) {
            do_move(&legal_moves[idx]);
            moves_applied++;
            
            // Validate board state every 50 moves in long games
            if (moves_applied > 0 && moves_applied % 50 == 0) {
                #ifdef DEBUG
				printf("info string Position checkpoint: %d moves applied, Counter=%d\n",
                       moves_applied, Counter);
				#endif
                fflush(stdout);
            }
        } else {
            #ifdef DEBUG
			printf("info string ERROR: illegal move '%s' (move %d/%d)\n", 
                   moves_list[i], i + 1, num_moves);
            printf("info string Current position: Color=%s, Counter=%d, legal_moves=%d\n",
                   Color == WHITE ? "WHITE" : "BLACK", Counter, n_moves);
			#endif
            fflush(stdout);
            
            // Dump the board state to diagnose issues
           #ifdef DEBUG           
		   printf("info string BOARD STATE DEBUG:\n");
            for (int rank = 8; rank >= 1; rank--) {
                printf("info string Rank %d: ", rank);
                for (int file = 1; file <= 8; file++) {
                    int sq = file + (rank + 1) * 10;
                    int pc = B[sq];
                    if (pc == 0) {
                        printf(". ");
                    } else {
                        char piece_char = ' ';
                        switch(piece(pc)) {
                            case PAWN: piece_char = 'P'; break;
                            case KNIGHT: piece_char = 'N'; break;
                            case BISHOP: piece_char = 'B'; break;
                            case ROOK: piece_char = 'R'; break;
                            case QUEEN: piece_char = 'Q'; break;
                            case KING: piece_char = 'K'; break;
                        }
                        printf("%c%c ", color(pc) == WHITE ? 'W' : 'B', piece_char);
                    }
                }
                printf("\n");
                fflush(stdout);
            }
            
            // Print first few legal moves for debugging
            if (n_moves > 0) {	
                printf("info string Legal moves in current position: ");			
                for (int j = 0; j < min(10, n_moves); j++) {
                    printf("%s ", move_to_uci(legal_moves[j]));
                }
                if (n_moves > 10) printf("... (%d more)", n_moves - 10);
                printf("\n");
                fflush(stdout);
            }
            
            // Don't continue if we can't parse moves - position is desynced
			printf("info string FATAL: Stopping position loading due to illegal move\n");
            fflush(stdout);
			#endif
            break;
        }
        free(moves_list[i]);
    }

    if (Counter == 0) {
        Counter = 1;
        G[1] = G[0];
        G[1].rule50 = 0;
        G[1].hashboard = G[0].hashboard;
        hashboard();
    }

    Flag.machine_color = 0;
    #ifdef DEBUG
	printf("info string POSITION loaded: %d moves applied (of %d), side=%s, Counter=%d\n",
           moves_applied, num_moves, Color == WHITE ? "WHITE" : "BLACK", Counter);
	#endif
    fflush(stdout);
    
    // Verify board integrity
	#ifdef DEBUG
    int piece_count = 0;
    for (int s = A1; s != H9; s++) {
        if (B[s] != 0 && B[s] != 3) piece_count++;
    }
	printf("info string Board verification: %d pieces\n", piece_count);
	#endif
    fflush(stdout);
}


char *move_to_uci(tmove m) {
  static char buf[8];
  char *promo = "";
  if (m.special == SHORT_CASTLING) return (Color == WHITE) ? "e1g1" : "e8g8";
  if (m.special == LONG_CASTLING) return (Color == WHITE) ? "e1c1" : "e8c8";
  if (m.in2a != m.in1 && (m.in2a >> 4)) {
    int p = m.in2a >> 4;
    promo = (p == QUEEN) ? "q" : (p == ROOK) ? "r" : (p == BISHOP) ? "b" : "n";
  }
  sprintf(buf, "%c%c%c%c%s",
          'a' + (m.from % 10) - 1,
          '1' + (m.from / 10) - 2,
          'a' + (m.to % 10) - 1,
          '1' + (m.to / 10) - 2,
          promo);
  return buf;
}


void uci_go(char *cmd) {
    Abort = 0;
    NoAbort = 0;
    G[Counter].rule50 = 0;

    // Check if we detected a ponderhit in position command
    int auto_ponderhit = (Flag.ponder == 1);

    if (auto_ponderhit) {
        #ifdef DEBUG
        printf("info string uci_go(): Detected auto-ponderhit state from position command\n");
        #endif
        fflush(stdout);
    }

    int wtime = -1, btime = -1;
    int winc = 0, binc = 0;
    int movestogo = 0;
    int depth = 0;
    int movetime = 0;
    int infinite = 0;
    int ponder = 0;
	if (CurrentDepthLimit < 9999) Flag.learn = 0;  // disable learning if not playing full strength (using skill level)

    char cmd_copy[1024];
    strncpy(cmd_copy, cmd, sizeof(cmd_copy) - 1);
    cmd_copy[sizeof(cmd_copy) - 1] = '\0';

    char *token = strtok(cmd_copy, " ");
    while ((token = strtok(NULL, " "))) {
        if (strcmp(token, "wtime") == 0) { token = strtok(NULL, " "); if (token) wtime = atoi(token); }
        else if (strcmp(token, "btime") == 0) { token = strtok(NULL, " "); if (token) btime = atoi(token); }
        else if (strcmp(token, "winc") == 0) { token = strtok(NULL, " "); if (token) winc = atoi(token); }
        else if (strcmp(token, "binc") == 0) { token = strtok(NULL, " "); if (token) binc = atoi(token); }
        else if (strcmp(token, "movestogo") == 0) { token = strtok(NULL, " "); if (token) movestogo = atoi(token); }
        else if (strcmp(token, "depth") == 0) { token = strtok(NULL, " "); if (token) depth = atoi(token); }
        else if (strcmp(token, "movetime") == 0) { token = strtok(NULL, " "); if (token) movetime = atoi(token); }
        else if (strcmp(token, "infinite") == 0) { infinite = 1; Flag.analyze = 1; Flag.level = fixeddepth; Flag.depth = MAXPLY * 100; Flag.centiseconds = 9999999;}
        else if (strcmp(token, "ponder") == 0) ponder = 1;
    }

    // Save time controls for potential ponderhit later
    if (!Flag.ponder || auto_ponderhit) {
        saved_wtime = wtime;
        saved_btime = btime;
        saved_winc = winc;
        saved_binc = binc;
        saved_movestogo = movestogo;
        saved_depth = depth;
        saved_movetime = movetime;
        saved_infinite = infinite;
		saved_is_ponderhit = auto_ponderhit;
    }

    Flag.analyze = infinite;

    // =========================================================================
    // AUTO-PONDERHIT: Use pondered results and switch to real time controls
    // =========================================================================
    if (auto_ponderhit && !ponder) {
        #ifdef DEBUG
        printf("info string *** AUTO-PONDERHIT *** Using pondered results!\n");
        #endif
        fflush(stdout);

        // Apply real time controls (with safe move overhead handling)
		apply_time_controls(wtime, btime, winc, binc, movestogo, depth, movetime, infinite, 0);

        Flag.machine_color = Color;
        extern long T1;
        T1 = ptime();
        return; // shell() will use existing PV/Pondermove
    }

    // =========================================================================
    // PONDER: Start pondering
    // =========================================================================
    if (ponder && Flag_ponder_enabled) {
        #ifdef DEBUG
        printf("info string Starting PONDER session\n");
        #endif
        Flag.ponder = 2;
        Flag.level = fixeddepth;
        Flag.depth = MAXPLY * 100;
        Flag.centiseconds = 999999;
        Time = 999999;
        Flag.machine_color = Color;
        return;
    }

    // =========================================================================
    // NORMAL SEARCH: Apply time controls safely
    // =========================================================================
    #ifdef DEBUG
    printf("info string Starting NORMAL search\n");
    #endif

    Flag.ponder = 0;
    Pondermove.from = 0;

    apply_time_controls(wtime, btime, winc, binc, movestogo, depth, movetime, infinite, 0);

    Flag.machine_color = Color;
    extern long T1;
    T1 = ptime();
	
	if (CurrentDepthLimit < 9999 ) {  //  CurrentDepthLimit < 9999  - means using a skill level 
	 printf("info string Skill level set to %d ELO [%s]\n", skill_elo, desc);
     printf("info string Depth limit: %d/100 ply, Eval noise: +/-%d cp\n",
           CurrentDepthLimit, CurrentEvalNoise);
	 }
}

// ============================================================================
// Safe time control calculator 
// ============================================================================

void apply_time_controls(int wtime, int btime, int winc, int binc,
                                int movestogo, int depth, int movetime, int infinite,
                                int is_ponderhit)  // NEW PARAMETER
{
    	
	// Apply time controls for the real search, using saved values from 'go' command
    wtime = saved_wtime, btime = saved_btime;
    winc = saved_winc, binc = saved_binc;
    movestogo = saved_movestogo;
    depth = saved_depth;
    movetime = saved_movetime;
    infinite = saved_infinite;
	is_ponderhit = saved_is_ponderhit;
    
	long budget_cs = 1000;

    if (depth > 0) {
        Flag.level = fixeddepth;
        Flag.depth = depth * 100;
    }
    else if (movetime > 0) {
        Flag.level = fixedtime;
        budget_cs = movetime / 10;
        
        // Only apply overhead for initial searches, not ponderhit
        if (!is_ponderhit && budget_cs > Flag.move_overhead + 50) {
            budget_cs -= Flag.move_overhead;
        } else if (!is_ponderhit) {
            budget_cs = max(50, budget_cs / 2);
        }
    }
    else if (infinite) {
        Flag.level = fixeddepth;
        Flag.depth = MAXPLY * 100;
        budget_cs = 999999;
    }
    else {
        // Standard time control
        if (wtime < 0 && btime < 0) { wtime = btime = 300000; }
        int mytime_ms = (Color == WHITE) ? wtime : btime;
        int myinc_ms  = (Color == WHITE) ? winc  : binc;
        if (mytime_ms < 0) mytime_ms = 300000;

        long total_cs = mytime_ms / 10;
        Flag.increment = myinc_ms / 10;
        Flag.level = timecontrol;
        Flag.moves = (movestogo > 0) ? movestogo : 40;

        long base = total_cs / Flag.moves;
        budget_cs = base + (base / 2) + Flag.increment;

        if (budget_cs > total_cs / 4) budget_cs = total_cs / 4;
        if (budget_cs <= 0) budget_cs = 100;

        // Only apply overhead once, on initial search
        if (!is_ponderhit) {
            long min_budget = 100;
            if (budget_cs > Flag.move_overhead + min_budget) {
                budget_cs -= Flag.move_overhead;
            } else {
                budget_cs = max(min_budget, budget_cs * 3 / 4);
            }
        }
        // For ponderhit, use budget as-is (GUI already accounted for overhead)
    }

    if (budget_cs < 50) budget_cs = 50;

    Flag.centiseconds = budget_cs;
    Time = budget_cs;

    #ifdef DEBUG
    printf("info string FINAL BUDGET: %ld cs (%.2f seconds) [ponderhit=%d]\n", 
           budget_cs, budget_cs / 100.0, is_ponderhit);
    #endif
    fflush(stdout);
}


void uci_setoption(char *cmd) {
  char *name_start, *value_start;
  char option_name[128] = "";
  char option_value[512] = "";  // INCREASED from 128 to handle long paths

  name_start = strstr(cmd, "name");
  if (name_start == NULL)
    return;
  name_start += 4;
  while (*name_start == ' ')
    name_start++;

  value_start = strstr(name_start, "value");
  if (value_start != NULL) {
    int len = value_start - name_start;
    while (len > 0 && name_start[len - 1] == ' ')
      len--;
    strncpy(option_name, name_start, len);
    option_name[len] = '\0';

    value_start += 5;
    while (*value_start == ' ')
      value_start++;
    strncpy(option_value, value_start, sizeof(option_value) - 1);  // FIX: Use strncpy
    option_value[sizeof(option_value) - 1] = '\0';

    char *newline = strchr(option_value, '\n');
    if (newline)
      *newline = '\0';
  } else {
    strncpy(option_name, name_start, sizeof(option_name) - 1);  // FIX: Use strncpy
    option_name[sizeof(option_name) - 1] = '\0';
    char *newline = strchr(option_name, '\n');
    if (newline)
      *newline = '\0';
  }

  #ifdef DEBUG
  printf("info string uci_setoption: name='%s' value='%s'\n", option_name, option_value);
  fflush(stdout);
  #endif

if (strcmp(option_name, "SkillLevel") == 0) {
    int new_elo = atoi(option_value);
    
    #ifdef DEBUG
    printf("info string DEBUG: SkillLevel setoption received: old=%d, new=%d\n", 
           skill_elo, new_elo);
    #endif
    fflush(stdout);
    
    // ARENA RESET BUG WORKAROUND:
    // Arena doesn't properly send reset commands for combo options.
    // However, we can detect when the user clicks "reset" by checking if
    // we receive the SAME value twice in a row (which suggests Arena re-applied it)
    // For now, always apply the value regardless:
    
    skill_elo = new_elo;
    set_skill_level(new_elo);
    
    // Update description
    desc = (new_elo == 2700) ? "Maximum Strength" :
           (new_elo >= 2400) ? "International Master" :
           (new_elo >= 2200) ? "FIDE Master" :
           (new_elo >= 2000) ? "Expert" :
           (new_elo >= 1600) ? "Advanced" :
           (new_elo >= 1200) ? "Intermediate" : "Beginner";

    printf("info string Skill level set to %d ELO [%s]\n", skill_elo, desc);
    printf("info string Depth limit: %d/100 ply, Eval noise: +/-%d cp\n",
           CurrentDepthLimit, CurrentEvalNoise);
    fflush(stdout);
    
    // Force appropriate settings for this skill level
    if (CurrentDepthLimit < 9999) {
        Flag.learn = 0;
        Flag_ponder_enabled = 0;
        SyzygyMaxPieces = 0;
        Flag.book = 0;
        
        printf("info string Learning automatically disabled due to SkillLevel setting.\n");
        printf("info string Ponder automatically disabled due to SkillLevel setting.\n");
        printf("info string Syzygy probing disabled due to SkillLevel setting\n");
        printf("info string OwnBook disabled due to SkillLevel setting\n");
    } else {
        printf("info string Maximum strength (2700 ELO) - features available\n");
    }
    fflush(stdout);
    return;


  // =========================================================================
  // Hash Table Size (MB)
  // =========================================================================
  } else if (strcmp(option_name, "Hash") == 0) {
    long long mb = atoll(option_value);
    if (mb < 1)
      mb = 1;
    if (mb > 16384)
      mb = 16384;

    size_t entries = (size_t)(mb * 1024LL * 1024LL / sizeof(thashentry));
    if (entries == 0)
      entries = 1;

    if (HT)
      free(HT);
    HT = calloc(entries, sizeof(thashentry));
    if (!HT) {
      SizeHT = 0;
      printf("info string Hash allocation failed\n");
    } else {
      SizeHT = entries;
      printf("info string Hash: %lld MB -> %zu entries (%.1f MB)\n", mb, SizeHT,
             (double)SizeHT * sizeof(thashentry) / (1024 * 1024));
    }
    fflush(stdout);

  // =========================================================================
  // Move Overhead (ms)
  // =========================================================================
  } else if (strcmp(option_name, "Move Overhead") == 0) {
    int overhead_ms = atoi(option_value);
    if (overhead_ms >= 0) {
      // Convert UCI value (ms) to engine's internal unit (cs)
      Flag.move_overhead = overhead_ms / 10;	
      printf("info string Move Overhead set to %d ms (%d cs)\n", overhead_ms, Flag.move_overhead);
    }
    fflush(stdout);

  // =========================================================================
  // Own Book
  // =========================================================================
  } else if (strcmp(option_name, "OwnBook") == 0) {
    if (strcmp(option_value, "true") == 0) {
      Flag.book = 1;
      printf("info string OwnBook enabled\n");
    } else if (strcmp(option_value, "false") == 0) {
      Flag.book = 0;
      printf("info string OwnBook disabled\n");
    }
    fflush(stdout);
	
	if (CurrentDepthLimit < 9999) {
		Flag.book = 0;           // Force opening book off
		printf("info string OwnBook disabled due to SkillLevel setting\n"); 
    }

  // =========================================================================
  // Ponder
  // =========================================================================
  } else if (strcmp(option_name, "Ponder") == 0) {
    Flag_ponder_enabled = (strcmp(option_value, "true") == 0);
    printf("info string Ponder option set to %s\n", Flag_ponder_enabled ? "true" : "false");
    fflush(stdout);
	
	// FIX: If SkillLevel is set (non-max strength), force Learning/Ponder off.
	if (CurrentDepthLimit < 9999) {
		Flag_ponder_enabled = 0; // Force ponder OFF
        printf("info string Ponder automatically disabled due to SkillLevel setting.\n");
    }
  
	
  // =========================================================================
  // Learning
  // =========================================================================
  
  } else if (strcmp(option_name, "Learning") == 0) {
    if (strcmp(option_value, "true") == 0) {
        
        // CRITICAL FIX: If a non-max SkillLevel is active, REJECT the attempt to enable Learning.
        if (CurrentDepthLimit < 9999) {
            Flag.learn = 0; // Force off (reject the 'true' command)           
            printf("info string ERROR: Learning cannot be enabled when SkillLevel is active.\n");      
        }
        // If skill level is max (default 2700 ELO) AND the file is open, allow it.
        else if (Learn.f != NULL) {
            Flag.learn = 1;
            printf("info string Learning enabled\n");
        } else {
            Flag.learn = 0;
            printf("info string WARNING: Cannot enable learning - file not opened\n");
        }
    } else if (strcmp(option_value, "false") == 0) {
        Flag.learn = 0;
        printf("info string Learning disabled\n");
    }
    fflush(stdout);
	
	// Original check remaining for final enforcement
	if (CurrentDepthLimit < 9999) Flag.learn = 0; 

	 
  // =========================================================================
  // Repetition Threshold (centipawns)
  // =========================================================================
  } else if (strcmp(option_name, "RepetitionThreshold") == 0) {
    int threshold = atoi(option_value);
    if (threshold >= 0 && threshold <= 2000) {
      RepetitionAvoidanceThreshold = threshold;
      printf("info string RepetitionThreshold set to %d cp\n", threshold);
    } else {
      printf("info string ERROR: RepetitionThreshold out of range [0..2000]\n");
    }
    fflush(stdout);

  // =========================================================================
  // Syzygy Tablebase Path
  // =========================================================================
   } else if (strcmp(option_name, "SyzygyPath") == 0) {
    #ifdef DEBUG
    printf("info string SyzygyPath option received: '%s'\n", option_value);
    fflush(stdout);
    #endif
    
    // Trim the path value
    char trimmed_path[512];
    strncpy(trimmed_path, option_value, sizeof(trimmed_path) - 1);
    trimmed_path[sizeof(trimmed_path) - 1] = '\0';
    
    // Remove trailing whitespace/newlines
    int path_len = strlen(trimmed_path);
    while (path_len > 0 && (isspace(trimmed_path[path_len - 1]) || trimmed_path[path_len - 1] == '\n' || trimmed_path[path_len - 1] == '\r')) {
      trimmed_path[--path_len] = '\0';
    }
    
    printf("info string Syzygy path option parsed as: '%s'\n", trimmed_path);
    fflush(stdout);
    
    // Call init_syzygy with trimmed path
    init_syzygy(trimmed_path);

  // =========================================================================
  // Syzygy Max Pieces
  // =========================================================================
  } else if (strcmp(option_name, "SyzygyMaxPieces") == 0) {
    int new_max = atoi(option_value);
    // Clamp value between 0 and 7 (0 means disabled)
    SyzygyMaxPieces = (new_max < 0) ? 0 : ((new_max > 7) ? 7 : new_max);
    
    #ifdef DEBUG
    printf("info string SyzygyMaxPieces set to %d\n", SyzygyMaxPieces);
    fflush(stdout);
    #endif
    
    // If a path is already set, re-initialize to apply the new limit immediately.
    if (SyzygyPath[0] != '\0') {
      init_syzygy(SyzygyPath); 
    } else {
      printf("info string SyzygyMaxPieces set to %d. Tables are not yet loaded.\n", SyzygyMaxPieces);
    }
    fflush(stdout);
	
	if (CurrentDepthLimit < 9999) {
		SyzygyMaxPieces = 0;
		printf("info string Syzygy probing disabled due to SkillLevel setting\n"); 
    }
  }
}


void send_bestmove_pv(void) {	
	// Send bestmove with current PV
            tmove legal[512];
            int n = 0;
            generate_legal_moves(legal, &n, checktest(Color));
            
            if (n > 0 && PV[0][0].from != 0) {
                tmove best = PV[0][0];
                tmove ponder = PV[0][1];
                
                // Validate best move
                int found = 0;
                for (int i = 0; i < n; i++) {
                    if (legal[i].from == best.from && legal[i].to == best.to) {
                        best = legal[i];
                        found = 1;
                        break;
                    }
                }
                if (!found) best = legal[0];
                
                // Calculate ponder if missing
                if (ponder.from == 0 || ponder.from == ponder.to) {
                    do_move(&best);
                    tmove opp[512];
                    int on = 0;
                    generate_legal_moves(opp, &on, checktest(enemy(Color)));
                    if (on > 0) ponder = opp[0];
                    undo_move(&best);
                }           
                uci_send_bestmove(best, ponder, 0);			
				}
				 }


int uci_command() {
    char *p;
    char *eol = NULL;
    int handled = 0;

    // 1. Read input if the buffer is empty
    if (Inp[0] == '\0') {
        if (fgets(Inp, 8192, stdin) == NULL) {
            strcpy(Inp, "quit\n");
        }
    }

    // 2. Trim leading whitespace
    p = Inp;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }

    // If empty after trimming, reset and return
    if (*p == '\0') {
        Inp[0] = '\0';
        return 0;
    }

    // 3. Find end of current command line
    eol = p;
    while (*eol != '\0' && *eol != '\n' && *eol != '\r') {
        eol++;
    }

    // Temporarily null-terminate the command
    char original_char = *eol;
    if (*eol != '\0') {
        *eol = '\0';
    }

    // Trim trailing whitespace
    char *end = eol;
    if (end > p) end--;
    while (end >= p && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }

    #ifdef DEBUG
    printf("info string Processing: \"%s\"\n", p);
	#endif
    fflush(stdout);

    // ========================================================================
    // 4. PROCESS COMMANDS
    // ========================================================================
    
    if (strncmp(p, "uci", 3) == 0 && (p[3] == '\0' || isspace((unsigned char)p[3]))) {
        uci_mode = 1;
        
		uci_send_id();
        uci_send_options();
        uci_send_uciok();
        handled = 1;
        
    } else if (!uci_mode) {
        // Not in UCI mode, ignore
        
    } else if (strncmp(p, "isready", 7) == 0 && (p[7] == '\0' || isspace((unsigned char)p[7]))) {
			
		uci_send_readyok();
        handled = 1;
        
   } else if (strncmp(p, "ucinewgame", 10) == 0 && (p[10] == '\0' || isspace((unsigned char)p[10]))) {
    setfen(initialpos);
    if (HT && SizeHT > 0)
        memset(HT, 0, SizeHT * sizeof(thashentry));
    Age = 0;
    Flag.machine_color = 0;
    
    handled = 1;
        
    } else if (strncmp(p, "position", 8) == 0) {
        uci_position(p);
        handled = 1;
        
    } else if (strncmp(p, "go", 2) == 0 && (p[2] == ' ' || p[2] == '\0')) {
        uci_go(p);
        handled = 1;
        
   } else if (strncmp(p, "stop", 4) == 0 && (p[4] == '\0' || isspace((unsigned char)p[4]))) {
        // ====================================================================
        // STOP COMMAND - Handle all modes properly
        // ====================================================================
		
		#ifdef DEBUG
		printf("info string STOP command received (ponder=%d, analyze=%d, machine_color=%d)\n", 
               Flag.ponder, Flag.analyze, Flag.machine_color);
		#endif
        fflush(stdout);
        
      // Check if we're actually searching
if (Flag.machine_color == 0 && Flag.ponder == 0 && Flag.analyze == 0 && PV[0][0].from == 0) {
    #ifdef DEBUG
	printf("info string STOP ignored - not searching and no move to send\n");
	#endif
    fflush(stdout);
    handled = 1;
    goto cleanup_buffer;
}
        // Set abort flag IMMEDIATELY
        Abort = 1;
        NoAbort = 0;
        
        // Give search a moment to see the Abort flag
        for (volatile int i = 0; i < 100000; i++);
        
        PV[0][1].from = 0;
        
        if (Flag.ponder == 2) {
            // Pondering mode - stop without bestmove (UCI spec)
            Flag.ponder = 0;
            Pondermove.from = 0;
            Flag.machine_color = 0;
			
			 // CRITICAL: Clear hash table to force search exit
            if (HT && SizeHT > 0)
                memset(HT, 0, SizeHT * sizeof(thashentry));
                   
            #ifdef DEBUG
			printf("info string Pondering stopped (no bestmove per UCI spec)\n");
			#endif
            fflush(stdout);
            
        } else if (Flag.analyze) {
            // Analysis mode - must send bestmove
            Flag.analyze = 0;
            Flag.machine_color = 0;
			      
            // Wait for search to see Abort
            for (volatile int i = 0; i < 1000000; i++);           
       
			#ifdef DEBUG
            printf("info string Analysis stopped, bestmove sent\n");
			#endif
            fflush(stdout);
					
			send_bestmove_pv();
		    initialize_engine();
							      
        } else {
            // Normal search - need to send bestmove!
            Flag.machine_color = 0;
            
            // Wait briefly for search to stabilize
            for (volatile int i = 0; i < 500000; i++);
            
			    send_bestmove_pv();
				
			    #ifdef DEBUG
				printf("info string Normal search stopped, bestmove sent\n");
				#endif
                fflush(stdout);
		}				
          
               
cleanup_buffer:
        handled = 1;
        
   } else if (strncmp(p, "ponderhit", 9) == 0 && (p[9] == '\0' || isspace((unsigned char)p[9]))) {
    // ====================================================================
    // PONDERHIT COMMAND - Properly transition from ponder to real search
    // ====================================================================
    
	#ifdef DEBUG
    printf("info string PONDERHIT command received (ponder=%d)\n", Flag.ponder);
    #endif
    fflush(stdout);
    
    // Check if we are in the active pondering state (Flag.ponder == 2)
    if (Flag.ponder != 2) {
        #ifdef DEBUG
        printf("info string PONDERHIT ignored - not in pondering mode (ponder=%d)\n", Flag.ponder);
        #endif
        fflush(stdout);
        handled = 1;
        goto cleanup_buffer;
    }
    
    // Stop pondering and force the *pondering* search to return immediately.
    Abort = 1;
    NoAbort = 0;
    
    // Signal that this is a ponderhit. 
    // State 3 means "Ponderhit received, search must transition to real time controls."
    Flag.ponder = 3; 
	
	// Apply time controls for the real search, using saved values from 'go' command
    int wtime = saved_wtime, btime = saved_btime;
	(void)wtime;
	(void)btime;
    int winc = saved_winc, binc = saved_binc;
	(void)winc;
	(void)binc;
    int movestogo = saved_movestogo;
	(void)movestogo;
    int depth = saved_depth;
	(void)depth;
    int movetime = saved_movetime;
	(void)movetime;
    int infinite = saved_infinite;
	(void)infinite;
    
    
    #ifdef DEBUG
    printf("info string PONDERHIT: Applying time controls (wtime=%d btime=%d)\n", 
           wtime, btime);
    #endif
    fflush(stdout);
    
    // ========================================================================
    // Set up Flag fields BEFORE calling l_startsearch()
    // ========================================================================
    
   if (depth > 0) {
    Flag.level = fixeddepth;
    Flag.depth = depth * 100;
} else if (movetime > 0) {
    Flag.level = fixedtime;
    Flag.centiseconds = movetime / 10;
    Time = movetime / 10;
} else if (infinite) {
    Flag.level = fixeddepth;
    Flag.depth = MAXPLY * 100;
} else {
    // Call the safer apply_time_controls with is_ponderhit=1
    apply_time_controls(saved_wtime, saved_btime, saved_winc, saved_binc,
                       saved_movestogo, saved_depth, saved_movetime, 
                       saved_infinite, 1);  // ← is_ponderhit=1
}
       
        #ifdef DEBUG
        printf("info string PONDERHIT: Time control: total=%ld cs, moves=%d, inc=%ld cs, overhead=%d cs â†' budget=%ld cs\n",
               total_time_cs, Flag.moves, Flag.increment, Flag.move_overhead, calculated_time_limit);
        #endif
    
    // ========================================================================
    // Reset T1 and call l_startsearch() to finalize all timers
    // ========================================================================
    extern long T1, T2;
    T1 = ptime();  // Record the START of the real search
    
    #ifdef DEBUG
    printf("info string PONDERHIT: Calling l_startsearch() to finalize timers\n");
    printf("info string PONDERHIT: Flag.level=%d, Flag.centiseconds=%ld, Flag.increment=%ld\n",
           Flag.level, Flag.centiseconds, Flag.increment);
    #endif
    fflush(stdout);
    
    // This MUST be called to:
    // 1. Set T2 as a DURATION (soft limit) based on Flag.level
    // 2. Apply increment bonuses and low-time tweaks
    // 3. Compute hard limit (Flag.centiseconds) from soft limit
    l_startsearch();
    
    #ifdef DEBUG
    printf("info string PONDERHIT: After l_startsearch() - T1=%ld, T2=%ld, Flag.centiseconds=%ld\n",
           T1, T2, Flag.centiseconds);
    printf("info string PONDERHIT: Exiting pondering. Real search will use T2=%ld as soft limit duration.\n", T2);
    #endif
    fflush(stdout);
    
    handled = 1;
  
  
    } else if (strncmp(p, "setoption", 9) == 0) {
        uci_setoption(p);
        handled = 1;
        
    } else if (strncmp(p, "bench", 5) == 0) {
        bench();
        handled = 1;
        
    } else if (strncmp(p, "quit", 4) == 0 && (p[4] == '\0' || isspace((unsigned char)p[4]))) {
        exit(0);
    }

    // ========================================================================
    // 5. CLEANUP - Shift buffer if there are more commands
    // ========================================================================
    if (original_char != 0) {
        *eol = original_char;  // Restore character
        
        char *next_command = eol;
        // Skip line endings
        if (*next_command == '\r' && *(next_command + 1) == '\n') {
            next_command += 2;
        } else if (*next_command == '\n' || *next_command == '\r') {
            next_command += 1;
        }

        // Move remaining content to start of buffer
        size_t remaining_len = strlen(next_command);
        memmove(Inp, next_command, remaining_len + 1);

        // If there's more content, return 1 to process it immediately
        if (Inp[0] != '\0') {
            return 1;
        }
    }
    
    // Clear buffer if done
    Inp[0] = '\0';
    return handled;
}


