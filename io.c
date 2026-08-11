

#include <signal.h>
#include <stdio.h>

#include "phalanx.h"


extern int uci_command();
extern void uci_send_info(int depth, int score, long time_cs, int64_t nodes, int nps,
                       tmove *pv, int pvlen);

extern long Time;

const char piece[7] = {' ', 'P', 'N', 'B', 'R', 'Q', 'K'};
const char file[10] = {'<', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', '>'};
const char row[12] = {'<', '0', '1', '2', '3', '4',
                      '5', '6', '7', '8', '9', '>'};
			
extern int Flag_ponder_enabled;  // ADD THIS LINE


tmove Pm[8192];
int Pn;

/**
*** Init board and structures
*** To be called after position is set (FEN or xboard's edit)
**/
void initbs(void) {

  int s;

  Counter = 0;
  DrawScore = 0;

  G[0].hashboard = hashboard();

  /* Do full material count */
  G[0].mtrl = G[0].xmtrl = 0;
  for (s = A1; s != H9; s++)
    if (color(B[s]) == Color)
      G[0].mtrl += Values[B[s] >> 4];
    else
      G[0].xmtrl += Values[B[s] >> 4];

  /* busted! S.A.
  G[0].castling = 0;
  if( B[E1]!=WK || B[A1]!=WR ) G[0].castling |= WLONG;
  if( B[E1]!=WK || B[H1]!=WR ) G[0].castling |= WSHORT;
  if( B[E8]!=BK || B[A8]!=BR ) G[0].castling |= BLONG;
  if( B[E8]!=BK || B[H8]!=BR ) G[0].castling |= BSHORT;
  */

  G[0].rule50 = 0;

  /* create the piece list */
  L[L[1].next].prev = 1;
  L[L[2].next].prev = 2;
  L[L[1].next].next = L[L[2].next].next = 0;
  {
    int wlast = L[1].next, blast = L[2].next;
    for (s = A1; s != H9; s++)
      if (B[s] != 0 && B[s] != 3 && B[s] != WK && B[s] != BK) {
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

 Flag.machine_color = enemy(Color);
}


/**
*** Print move in full notation (Ng1-f3) to string 's' or to stdout.
*** SAN would be more complicated since we need a list of all possible
*** moves to find out whether to print 'Nbd2' or 'Nd2'.
**/

void printm( tmove m, char *s )
{

char ss[64];

switch( m.special )
{
  case LONG_CASTLING:  sprintf( ss, "O-O-O  " ); goto endprint;
  case SHORT_CASTLING: sprintf( ss, "O-O  " ); goto endprint;
  case NULL_MOVE: sprintf( ss, "NULLMOV "); goto endprint;
}

if( m.in1 != WP && m.in1 != BP ) sprintf( ss, "%c", piece[m.in1>>4] );
else ss[0]='\0';

sprintf( ss+strlen(ss), "%c%c%c%c%c",
	file[m.from%10], row[m.from/10],
	( m.in2 || m.special ) ? 'x' : '-',
	file[m.to%10], row[m.to/10] );

if( m.in2a != m.in1 ) sprintf( ss+strlen(ss), "%c", piece[m.in2a>>4] );

#ifdef SHOW_FORCED_MOVES
if( m.dch != 100 ) sprintf( ss+strlen(ss), "/%i",m.dch );
#endif

sprintf( ss+strlen(ss), "  " );

endprint:;

if( s == NULL ) printf("%s",ss); else strcpy( s, ss );

}


/**
*** Return pointer to move in m[] if it matches the SAN representation.
*** If there is no such move, return NULL.
**/

tmove * sandex( char *inp, tmove *m, int n )
{
	char bufik[32];
	char *s = bufik;
	char * ss; char * sto;
	int move,to;
	int i;
	unsigned p=0;
	int frow=0, ffile=0;
	int in2a=0;
	int color = color(m->in1);
	int sc=-1; int lc=-1;

	strncpy(bufik,inp,30);

	for( i=0; i!=n; i++ ) if(m[i].special==SHORT_CASTLING) sc=i;
	else if(m[i].special==LONG_CASTLING) lc=i;

	/*** step 0: castlings ***/
	if( lc != -1 )
	if(  strncmp(s,"o-o-o",5)==0
	  || strncmp(s,"O-O-O",5)==0
	  || strncmp(s,"0-0-0",5)==0
	  || ( strncmp(s,"e1c1",4)==0 && color==WHITE )
	  || ( strncmp(s,"e8c8",4)==0 && color==BLACK )
	  ) return m+lc;

	if( sc != -1 && s[3]!='-' )
	if(  strncmp(s,"o-o",3)==0
	  || strncmp(s,"O-O",3)==0
	  || strncmp(s,"0-0",3)==0
	  || ( strncmp(s,"e1g1",4)==0 && color==WHITE )
	  || ( strncmp(s,"e8g8",4)==0 && color==BLACK )
	  ) return m+sc;

	/*** step 1: noncastling move must start with PNBRQKabcdefgh ***/
	/***         if it starts with pnrqk, do the letter upercase ***/
	{
		static char ok[] = "PNBRQKabcdefghpnrqk";
		static char up[] = "pnrqk";
		for( i=0; ok[i]!=*s; i++ ) if( ok[i] == '\0' ) return(NULL);
		for( i=0; up[i]!='\0'; i++ )
			if( up[i] == *s ) { *s = toupper((int)*s); break; }
	}

	/*** step 2: find the destination square ***/
	/* Find the last digit <1-8> in the input string.  It must
	 * be the row of the destination square and must be preceded
	 * by letter <a-h> of the dest. square **********************/
	sto=NULL;
	for( ss=s+1; *ss!='\0'; ss++ ) if( *ss>='1' && *ss<='8' ) sto = ss-1;
	if( sto == NULL )
	{
		if(    ( s[2]=='\0' || s[2]=='\n' )
		    && s[1]>='a' && s[1]<='h'
		    && s[0]>='a' && s[0]<='h' )
		{ /* exception: pawn capture can be typed as 'ef' */
			int mn=-1;
/* printf("trying exception %c %c %c\n", s[0], s[1], s[2] ); */
			for( i=0; i!=n; i++ )
				if( piece(m[i].in1) == PAWN )
					if( m[i].from%10 == s[0]+1-'a' )
						if( m[i].to%10 == s[1]+1-'a' )
						{
							if( mn==-1 )
								mn=i;
							else
								mn=-2;
						}
			if( mn>=0 ) return m+mn;
		}
		return NULL;  /* no digit 1-8 found */
	}

	for( i=1; i!=9; i++ ) if(file[i]==*sto) break;
	if(i==9) return NULL;
	to = i;
	for( i=2; i!=10; i++ ) if(row[i]==sto[1]) break;
	if(i==10) return NULL;
	to += i*10;

	/*** step 3: find the origin square ***/
	/* 3a - try to find full description of origin square (g1f3) */
	ss=sto-1;
	if(ss>=s)
	for( ss=sto-1; ss!=s; ss-- )
	if( *ss>='1' && *ss<='8' && *(ss-1)>='a' && *(ss-1)<='h' )
	{
		for( i=2; i!=10; i++ ) if(row[i]==*ss) break;
		frow = i;
		ss--; for( i=1; i!=9; i++ ) if(file[i]==*ss) break;
		ffile = i;
		for(i=0;i!=n;i++) if(m[i].from==frow*10+ffile)
		{ p=m[i].in1; break; }
		goto step4;
	}
	/* 3b - origin square must be computed from piece */
	{
		for(p=6;p!=1;p--) if( piece[p] == *s ) { s++; break; }
		p = (p<<4) + color;
		if( *s!='x' && s<sto ) /* init frow or ffile */
		{
			if( *s>='a' && *s<='h' ) ffile = *s-'a'+1;
			else
			if( *s>='1' && *s<='8' ) frow = *s-'1'+2;
		}

	}

	step4:;
	/*** step 4: is it a pawn promotion? determine piece ***/
	in2a = p;
	sto += 2; if( *sto == '=' ) sto++;
	*sto = toupper((int)*sto);
	for(i=2;i!=7;i++) if( piece[i]==*sto ) in2a=(i<<4)+color;

	/*** step 5: scan move list ***/
	move = -1;
	for(i=0;i!=n;i++) if( m[i].to==to && m[i].in1==p && m[i].in2a==in2a )
	if( ffile==0 || ffile==m[i].from%10 )
	if( frow==0  ||  frow==m[i].from/10 )
	{ if(move==-1) move=i; else return NULL; }
	if(move==-1) return NULL;

	return m+move;
}




void infoline(int typ, char *s) {
 
  extern long T1;
  long t = ptime();
  
  /* UCI info output */

    if (typ == 1 || typ == 2) {
      int nps = (int)(((float)Nodes) / (((float)max(t - T1, 1)) / 100));
      int pvlen = 0;
      while (PV[0][pvlen].from != 0 && pvlen < MAXPLY)
        pvlen++;

      uci_send_info(A_d, PV[0][0].value, t - T1, Nodes, nps, PV[0], pvlen);
     
	  
	  if (s != NULL)
        s[0] = '\0';
      return;
	  }
 
}



void printboard(char *s) {
  int i;
  char ss[2048];

  sprintf(ss, "  +---+---+---+---+---+---+---+---+\n  ");
  for (i = A8; i >= A1; i++) {
    switch (color(B[i])) {
    case WHITE:
      sprintf(ss + strlen(ss), "| %c ", piece[B[i] >> 4]);
      break;
    case BLACK:
      sprintf(ss + strlen(ss), "| *%c", piece[B[i] >> 4]);
      break;
    default:
      sprintf(ss + strlen(ss), "|   ");
    }
    if (i % 10 == 8) {
      i -= 18;
      sprintf(ss + strlen(ss), "|\n  +---+---+---+---+---+---+---+---+");
      if (i != 10)
        sprintf(ss + strlen(ss), "\n  ");
    }
  }
  if (Color == WHITE)
    sprintf(ss + strlen(ss), "   White to move\n");
  else
    sprintf(ss + strlen(ss), "   Black to move\n");

  if (s == NULL)
    printf("%s", ss);
  else
    strcpy(s, ss);

  /* printf("%08X\n",G[Counter].hashboard); */
}

/**
***  The setfen() function is one of the two ways to set a position.
***  The second one is edit().
**/



int setfen(const char *f) {
  const char *g, *errmsg = "Illegal position";
  int i, j;
  tgamenode p, q;
  const char *castling_start;

  if (Flag.log != NULL) {
    fprintf(Flag.log, "\n\nsetting position\n%s\n\n", f);
  }
  

		

  // Clear board
  memset(B, 3, sizeof(B));
  for (i = FILE_A; i <= FILE_H; i++)
    memset(&B[10 * i + 11], 0, 8);
  memset(G, 0, sizeof(G));
  p = q = G[0];

  TotalPiecesOnBoard = 0;

  // --- Piece placement ---
  for (j = A8; j != H1 + 1; f++) {
    if (*f == '\0' || *f == ' ') {
      puts(errmsg);
      return 1;
    }
    if (isdigit(*f)) {
      if (*f == '0' || *f == '9') {
        puts(errmsg);
        return 1;
      }
      j += (*f - '0');
    } else if (*f == '/') {
      if (j % 10 != 9) {
        puts(errmsg);
        return 1;
      }
      j -= 18;
    } else {
      switch (tolower(*f)) {
        case 'k': B[j] = KING;    break;
        case 'q': B[j] = QUEEN;   break;
        case 'r': B[j] = ROOK;    break;
        case 'b': B[j] = BISHOP;  break;
        case 'n': B[j] = KNIGHT;  break;
        case 'p': B[j] = PAWN;    break;
        default:
          puts(errmsg);
          return 1;
      }
      B[j] |= (tolower(*f) == *f) ? BLACK : WHITE;

      if (piece(B[j]) == KING)
        L[color(B[j])].next = j;

      TotalPiecesOnBoard++;  // Count every piece
      j++;
    }
  }

  // --- Active color ---
  while (*f == ' ') f++;
  if (*f == 'w')      Color = WHITE;
  else if (*f == 'b') Color = BLACK;
  else { puts(errmsg); return 1; }
  f++;

  // --- Castling rights ---
  while (*f == ' ') f++;
  castling_start = f;                   // remember start of castling field
  q.castling = WSHORT | WLONG | BSHORT | BLONG;  // assume none available

  if (*f != '-') {
    while (*f != ' ' && *f != '\0') {
      if (*f == 'K') q.castling &= ~WSHORT;
      if (*f == 'Q') q.castling &= ~WLONG;
      if (*f == 'k') q.castling &= ~BSHORT;
      if (*f == 'q') q.castling &= ~BLONG;
      f++;
    }
  } else {
    f++;  // skip the '-'
  }

  // If a right is NOT mentioned in the FEN, it is unavailable
  if (*castling_start != '-') {
    if (!strchr(castling_start, 'K')) q.castling |= WSHORT;
    if (!strchr(castling_start, 'Q')) q.castling |= WLONG;
    if (!strchr(castling_start, 'k')) q.castling |= BSHORT;
    if (!strchr(castling_start, 'q')) q.castling |= BLONG;
  }

  // --- En passant ---
  while (*f == ' ') f++;
  g = f + 1;
  if (*f != '-' && strchr("abcdefgh", *f) && 
      ((*g == '3' && Color == BLACK) || (*g == '6' && Color == WHITE))) {
    i = 21 + (*f - 'a') + 10 * (*g - '1');
    j = (Color == WHITE) ? -10 : 10;
    if (B[i + j] == (PAWN | (Color ^ (BLACK | WHITE)))) {
      p.m.from = i - j;
      p.m.special = i;
      p.m.to = i + j;
      p.m.in1 = p.m.in2a = B[i + j];
    }
    f = g + 1;
  } else {
    if (*f != '-') f++;  // skip single char if not '-'
  }

  // --- Initialize bitboards and hash ---
  while (*f == ' ') f++;
  initbs();
  q.hashboard = hashboard();

  q.mtrl = p.xmtrl = G[0].mtrl;
  q.xmtrl = p.mtrl = G[0].xmtrl;
  G[0].mtrl = G[0].xmtrl = G[0].hashboard = 0;

  // --- Halfmove clock ---
  i = 0;
  while (isdigit(*f)) i = i * 10 + (*f++ - '0');
  q.rule50 = (i < 50) ? i : 50;
  if (i > 0 && p.m.special) p.rule50 = q.rule50 - 1;

  // --- Fullmove number ---
  while (*f == ' ') f++;
  Counter = 0;
  if (p.m.special) {
    Counter++;
    if (Color == WHITE) Counter++;
  }
  i = 0;
  while (isdigit(*f)) i = i * 10 + (*f++ - '0');
  if (i) {
    i = 2 * i - 1;
    if (Color == BLACK) i++;
    Counter = i;
  }

  // Store in game history
  G[Counter] = q;
  if (Counter > 0 && p.m.special)
    G[Counter - 1] = p;

  return 0;
}



/**
*** Is the current position terminal?
*** returns: 0 ... not terminal
***          1 ... draw, 50 moves or 3rd repetition; ok to continue play
***          2 ... draw, stalemate
***          3 ... checkmate
**/
int terminal(void) {

  tmove m[256];
  int n, c;

 
    // CRITICAL: Prevent invalid access when Counter < 2
    if (Counter < 4) return 0;  // Safe: no repetition possible

    if (repetition(2) || material_draw()) {
        if (Flag.machine_color == (WHITE | BLACK))
            Flag.machine_color = 0;
        return 1;
    }
  

  generate_legal_moves(m, &n, c = checktest(Color));

  if (n != 0)
    return 0;

  if (c)
    return 3;
  else
    return 2;
}


void shell(void) {
    // Safety: Initialize machine_color when the shell starts
    if (Flag.machine_color != WHITE && Flag.machine_color != BLACK) {
        Flag.machine_color = 0;
    }

    while (1) {
        // ============================================================
        // MAIN COMMAND LOOP - Wait for engine's turn or process input
        // ============================================================
        while (1) {
            if (Flag.machine_color == Color && terminal() < 2) break;
            if (!uci_command()) goto exit_shell;
            if (Flag.machine_color == Color && terminal() < 2) break;
        }

        if (Flag.machine_color != Color || terminal() >= 2) {
            Flag.machine_color = 0;
            continue;
        }

        tmove best, ponder_move = {0};
        tmove m[256];
        int n = 0;

        /* ========================================================
         * PONDERING MODE (Flag.ponder == 2) - NO POSITION CHANGE
         * ======================================================== */
        if (Flag.ponder == 2) {
            #ifdef DEBUG
            printf("info string Pondering enabled (waiting for ponderhit)\n");
            #endif
            fflush(stdout);

            /* CRITICAL FIX: Do NOT modify the position during pondering
             * Search in the CURRENT position instead of playing speculative move
             * This prevents eval fluctuations and simplifies ponderhit handling */

            /* Setup infinite search parameters */
            Flag.level = fixeddepth;
            Flag.depth = MAXPLY * 100;
            Flag.centiseconds = 999999;
            Time = 999999;
            Abort = 0;
            NoAbort = 0;
            Flag.machine_color = Color;

            /* Start pondering search in CURRENT position */
            best = root_search();

            /* Check ponder state after search returns */
            if (Flag.ponder == 1) {
                /* PONDERHIT arrived - use pondered results */
                #ifdef DEBUG
                printf("info string PONDERHIT: using pondered results\n");
                #endif
                fflush(stdout);
                
                best = PV[0][0];
                if (best.from == 0) {
                    generate_legal_moves(m, &n, checktest(Color));
                    if (n > 0) best = m[0];
                }
                
                goto handle_ponderhit;
                
            } else if (Flag.ponder == 3) {
                /* STOP received - send bestmove from pondered position */
                #ifdef DEBUG
                printf("info string PONDER: stopped, sending bestmove\n");
                #endif
                fflush(stdout);

                best = PV[0][0];
                if (best.from == 0 || best.from == best.to) {
                    generate_legal_moves(m, &n, checktest(Color));
                    if (n > 0) best = m[0];
                }
                
                if (best.from != 0) {
                    /* Validate best move */
                    tmove legal[256];
                    int ln = 0;
                    generate_legal_moves(legal, &ln, checktest(Color));
                    
                    if (ln > 0) {
                        int found = 0;
                        for (int i = 0; i < ln; i++) {
                            if (legal[i].from == best.from && legal[i].to == best.to) {
                                best = legal[i];
                                found = 1;
                                break;
                            }
                        }
                        if (!found) best = legal[0];
                        
                        /* Send bestmove without ponder (position will change) */
                        uci_send_bestmove(best, (tmove){0}, 0);
                    }
                }
                
                /* Clear state and return to command loop */
                Flag.ponder = 0;
                Flag.machine_color = 0;
                Abort = 0;
                continue;
                
            } else {
                /* Shouldn't reach here */
                #ifdef DEBUG
                printf("info string PONDER: unexpected state %d\n", Flag.ponder);
                #endif
                fflush(stdout);

                Flag.ponder = 0;
                Flag.machine_color = 0;
                Abort = 0;
				uci_send_bestmove(best, (tmove){0}, 0);      // KLUDGE
                continue;
            }
        }

        /* ========================================================
         * PONDER HIT (Flag.ponder == 1) - ALWAYS SEND BESTMOVE
         * ======================================================== */
        if (Flag.ponder == 1) {
handle_ponderhit:
            #ifdef DEBUG
            printf("info string PONDERHIT: Sending bestmove immediately\n");
            #endif
            fflush(stdout);

            // Use pondered results from PV
            best = PV[0][0];
            
            if (best.from == 0 || best.from == best.to) {
                // No valid PV - generate and pick first legal move
                #ifdef DEBUG
                printf("info string PONDERHIT: No valid PV, using first legal move\n");
                #endif
                fflush(stdout);
                
                generate_legal_moves(m, &n, checktest(Color));
                if (n > 0) {
                    best = m[0];
                } else {
                    Flag.machine_color = 0;
                    Flag.ponder = 0;
                    continue;
                }
            }

            /* Validate best move */
            tmove legal[256];
            int ln = 0;
            generate_legal_moves(legal, &ln, checktest(Color));
            if (ln == 0) {
                Flag.machine_color = 0;
                Flag.ponder = 0;
                continue;
            }

            int found = 0;
            int promo = (best.in2a != best.in1) ? (best.in2a >> 4) : -1;
            for (int i = 0; i < ln; i++) {
                int lpromo = (legal[i].in2a != legal[i].in1) ? (legal[i].in2a >> 4) : -1;
                if (legal[i].from == best.from &&
                    legal[i].to == best.to &&
                    lpromo == promo) {
                    best = legal[i];
                    found = 1;
                    break;
                }
            }
            if (!found) best = legal[0];

            /* Make the move */
            if (!Flag.analyze) {
                do_move(&best);
                G[Counter].m = best;
                Counter++;
                initbs();
            }

            /* Calculate ponder move */
            tmove opp[256];
            int on = 0;
            generate_legal_moves(opp, &on, checktest(enemy(Color)));
            
            ponder_move = (tmove){0};
            if (on > 0) {
                tmove cand = (PV[0][1].from != 0) ? PV[0][1] : opp[0];
                ponder_move = opp[0];
                for (int i = 0; i < on; i++) {
                    if (opp[i].from == cand.from && opp[i].to == cand.to) {
                        ponder_move = opp[i];
                        break;
                    }
                }
            }

            Pondermove = ponder_move;
            
            // CRITICAL: Always send bestmove immediately
            uci_send_bestmove(best, ponder_move, 1);

            Flag.ponder = 0;
            Flag.analyze = 0;
            Flag.machine_color = 0;
            Abort = 0;
            PV[0][1].from = 0;
            continue;
        }

        /* ========================================================
         * NORMAL MOVE (no pondering)
         * ======================================================== */
        Abort = 0;
        NoAbort = 0;

        #ifdef DEBUG
        printf("info string NORMAL: Starting root_search()\n");
        #endif
        fflush(stdout);

        best = root_search();

        #ifdef DEBUG
        printf("info string NORMAL: root_search() returned, best.from=%d best.to=%d Abort=%d\n",
               best.from, best.to, Abort);
        #endif
        fflush(stdout);

        /* ---- TIMEOUT OR STOP ---- */
        if (Abort) {
            #ifdef DEBUG
            printf("info string ABORT: Search timed out or stopped\n");
            #endif
            fflush(stdout);
            
            if (PV[0][0].from != 0) {
                tmove m = PV[0][0];
                tmove p = PV[0][1];
                
                // CRITICAL FIX: Calculate ponder move even when aborting
                if (p.from == 0 || p.from == p.to) {
                    #ifdef DEBUG
                    printf("info string ABORT: PV[0][1] empty, calculating ponder move\n");
                    #endif
                    fflush(stdout);
                    
                    // We need to make the best move temporarily to calculate opponent's response
                    tmove temp_best = m;
                    
                    // Validate the move first
                    tmove legal[256];
                    int ln = 0;
                    generate_legal_moves(legal, &ln, checktest(Color));
                    
                    int found = 0;
                    for (int i = 0; i < ln; i++) {
                        if (legal[i].from == temp_best.from && legal[i].to == temp_best.to) {
                            temp_best = legal[i];
                            found = 1;
                            break;
                        }
                    }
                    
                    if (found) {
                        // Make the move temporarily
                        do_move(&temp_best);
                        
                        // Generate opponent's legal moves
                        tmove opp[256];
                        int on = 0;
                        generate_legal_moves(opp, &on, checktest(enemy(Color)));
                        
                        #ifdef DEBUG
                        printf("info string ABORT: Generated %d opponent moves after our move\n", on);
                        #endif
                        fflush(stdout);
                        
                        if (on > 0) {
                            p = opp[0];  // Use first legal move as ponder
                            #ifdef DEBUG
                            printf("info string ABORT: Set ponder to %c%c%c%c\n",
                                   'a' + (p.from % 10) - 1,
                                   '1' + (p.from / 10) - 2,
                                   'a' + (p.to % 10) - 1,
                                   '1' + (p.to / 10) - 2);
                            #endif
                            fflush(stdout);
                        }
                        
                        // Undo the temporary move
                        undo_move(&temp_best);
                    }
                }
                #ifdef DEBUG
                printf("info string ABORT: PRE-BESTMOVE CHECK:\n");
                printf("info string   Flag_ponder_enabled = %d\n", Flag_ponder_enabled);
                printf("info string   PV[0][1].from = %d, to = %d\n", PV[0][1].from, PV[0][1].to);
                printf("info string   Final ponder.from = %d, to = %d\n", p.from, p.to);
                #endif
                fflush(stdout);
                
                uci_send_bestmove(m, p, 0);
            }
            #ifdef DEBUG
            printf("info string Search aborted, ready for next command\n");
            #endif
            fflush(stdout);
            Flag.machine_color = 0;
            continue;  
        }

        if (best.from == 0) {
            Flag.machine_color = 0;
            continue;
        }

        /* Make the move */
        do_move(&best);
        G[Counter].m = best;
        Counter++;
        initbs();

        /* Calculate opponent's ponder move */
        tmove opp[256];
        int on = 0;
        generate_legal_moves(opp, &on, checktest(enemy(Color)));
        
        #ifdef DEBUG
        printf("info string NORMAL: Generated %d legal opponent moves\n", on);
        #endif
        fflush(stdout);
        
        if (on > 0) {
            tmove cand = (PV[0][1].from != 0) ? PV[0][1] : opp[0];
            
            #ifdef DEBUG
            printf("info string NORMAL: PV[0][1] = %c%c%c%c (from=%d to=%d)\n",
                   (PV[0][1].from != 0) ? 'a' + (PV[0][1].from % 10) - 1 : '?',
                   (PV[0][1].from != 0) ? '1' + (PV[0][1].from / 10) - 2 : '?',
                   (PV[0][1].to != 0) ? 'a' + (PV[0][1].to % 10) - 1 : '?',
                   (PV[0][1].to != 0) ? '1' + (PV[0][1].to / 10) - 2 : '?',
                   PV[0][1].from, PV[0][1].to);
            #endif
            fflush(stdout);
            
            ponder_move = opp[0];
            for (int i = 0; i < on; i++) {
                if (opp[i].from == cand.from && opp[i].to == cand.to) {
                    ponder_move = opp[i];
                    #ifdef DEBUG
                    printf("info string NORMAL: Found matching ponder move at index %d\n", i);
                    #endif
                    fflush(stdout);
                    break;
                }
            }
            
            #ifdef DEBUG
            printf("info string NORMAL: Final ponder_move = %c%c%c%c (from=%d to=%d)\n",
                   'a' + (ponder_move.from % 10) - 1,
                   '1' + (ponder_move.from / 10) - 2,
                   'a' + (ponder_move.to % 10) - 1,
                   '1' + (ponder_move.to / 10) - 2,
                   ponder_move.from, ponder_move.to);
            #endif
            fflush(stdout);
        }

        /* CRITICAL: Save ponder move BEFORE sending bestmove */
        Pondermove = ponder_move;

        #ifdef DEBUG
        printf("info string NORMAL: PRE-BESTMOVE CHECK:\n");
        printf("info string   Flag_ponder_enabled = %d\n", Flag_ponder_enabled);
        printf("info string   ponder_move.from = %d, to = %d\n", ponder_move.from, ponder_move.to);
        printf("info string   PV[0][1].from = %d, to = %d\n", PV[0][1].from, PV[0][1].to);
        printf("info string   Pondermove.from = %d, to = %d\n", Pondermove.from, Pondermove.to);
        #endif
        fflush(stdout);

        uci_send_bestmove(best, ponder_move, 1);

        Flag.machine_color = 0;
        continue;
    }

exit_shell:
    return;
}








