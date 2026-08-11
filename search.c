
/*
 *   Alphabeta negamax search
 */

#include "phalanx.h"

#define SCOUT

#define WINDOW 60

#define SWINDOW 5

#define update_PV(move, ply)                                                   \
  {                                                                            \
    register int __j;                                                          \
    PV[ply][ply] = move;                                                       \
    for (__j = ply + 1; PV[ply + 1][__j].from; __j++)                          \
      PV[ply][__j] = PV[ply + 1][__j];                                         \
    PV[ply][__j].from = 0; /* end of copied line */                            \
  }


int MaxSeldepth = 0;      // Deepest ply reached in any iteration
int CurrentSeldepth = 0;  // Deepest ply reached in current iteration



/*
 * update_seldepth - Call this whenever search goes deeper
 * Should be called at entry points to main search, quiescence, etc.
 */
void update_seldepth(int current_ply) {
    if (current_ply > MaxSeldepth) {
        MaxSeldepth = current_ply;
    }
    if (current_ply > CurrentSeldepth) {
        CurrentSeldepth = current_ply;
    }
}


int uci_input_ready(void) {
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE || h == NULL) return 0;

    DWORD old_console_mode;
    if (GetConsoleMode(h, &old_console_mode)) {
        // Console mode
        DWORD result = WaitForSingleObject(h, 0);
        if (result == WAIT_OBJECT_0) return 1;
        if (result == WAIT_TIMEOUT) return 0;
        return 0;
    } else {
        // Pipe mode
        DWORD avail = 0;
        if (PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL)) {
            if (avail > 0) return 1;
        }

        DWORD old_pipe_state = 0;
        if (GetNamedPipeHandleState(h, &old_pipe_state, NULL, NULL, NULL, NULL, 0)) {
            DWORD new_mode = (old_pipe_state & ~PIPE_WAIT) | PIPE_NOWAIT;
            if (SetNamedPipeHandleState(h, &new_mode, NULL, NULL)) {
                char dummy;
                DWORD read = 0;
                BOOL res = ReadFile(h, &dummy, 1, &read, NULL);
                DWORD err = res ? 0 : GetLastError();

                SetNamedPipeHandleState(h, &old_pipe_state, NULL, NULL);

                if (res && read > 0) return 1;
                if (err == ERROR_MORE_DATA) return 1;
                return 0;
            }
        }
        return 0;
    }
#else
    // FIXED Linux/macOS implementation
    // The issue: stdin may be buffered, and select() only checks if the 
    // file descriptor has data, not if there's data in stdio's buffer.
    
    // First, check if there's already buffered input in stdio
    // This is crucial for proper UCI command handling
    #ifdef __GLIBC__
        // For glibc-based systems (most Linux distributions)
        if (stdin->_IO_read_ptr < stdin->_IO_read_end) {
            return 1;  // Buffered data available
        }
    #elif defined(__APPLE__) || defined(__FreeBSD__)
        // For BSD-based systems (macOS, FreeBSD)
        if (stdin->_r > 0) {
            return 1;  // Buffered data available
        }
    #endif
    
    // If no buffered data, check the actual file descriptor
    struct timeval tv;
    fd_set readfds;
    
    // CRITICAL: Initialize these EVERY time (select modifies them on some systems)
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    
    // Check if stdin has data available
    int result = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);
    
    if (result > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
        return 1;  // Input available
    }
    
    return 0;  // No input available
#endif
}



 
 /* Input polling */
 
void poll_uci_input(void) {
    // Check if input is available without blocking
    if (!uci_input_ready()) {
        return;  // No input, return immediately
    }
    
    // Input is ready - process it
    uci_command();
    
    // If abort was set, return immediately
    if (Abort) {
        return;
    }
}


int sortkey(const void *a, const void *b) {
  return (((tmove *)b)->value - ((tmove *)a)->value);
}



/*
 * search carefuly. we expect fail-low at this node.
 * This is only used when null move fails low by a high amount ->
 * that means we are probably under a major threat and we should
 * extend anything that removes or delays the threat.
 * So, this is a normal PVS search that extends moves
 * that fail high in the zero width search - their re-search
 * is extended.
 */
int csearch(tmove *m, /* move list */
            int n,    /* number of moves */
            int Alpha, int Beta, int extend) {

  TRACK_SELDEPTH();
  
  int i;
  int result;

  for (i = 0; i != n; i++) {
    int max;

    {
      int j;
      max = i;
      for (j = i + 1; j != n; j++)
        if (m[j].value > m[max].value)
          max = j;
    }

    do_move(m + max);

    result = -evaluate(-Alpha - 1, -Alpha);

    if (result > Alpha) /* let's look better at this move */
    {
      Depth += extend;
      result = -evaluate(-Beta, -Alpha);
      /*
      undo_move(m+max);
      printboard(NULL); printm( m[max], NULL ); printf("ext=%i",extend);
      if( result <= Alpha ) printf("[+]"); else printf("[-]");
      do_move(m+max);
      getchar();
      */
      Depth -= extend;
    }

    undo_move(m + max);

    FollowPV = 0;

    if (result > Alpha) /* still better? ok, we can trust it. */
    {
      update_PV(m[max], Ply);
      Alpha = result;
      if (Alpha >= Beta)
        return Alpha;
      extend = 0;
    }

    m[max] = m[i];
  }

  return Alpha;
}

int search(
	tmove *m, /* move list */
	int n,    /* number of moves */
	int Alpha, int Beta
)
{

TRACK_SELDEPTH();
  
int i;
int maxi = -1;
int result;


if( Abort && ! NoAbort ) return 0;

/*** internal iterative deepening ***/
#define IID
#ifdef IID
if( Depth > 200 && Beta-Alpha>1 )
{
	int max=0;
	for( i=1; i!=n; i++ ) if( m[i].value > m[max].value ) max=i;
	if( m[max].value != CHECKMATE )
	{
		int depth=Depth;
		Depth -= 200;
		search( m, n, Alpha, Beta );
		Depth = depth;
#undef DEBUGIID
#ifdef DEBUGIID
if(Depth>400)
{
max=0; printboard(NULL);
for(i=0;i!=n;i++) if(m[i].value>m[max].value) max=i;
printm(m[max],NULL); getchar();
}
#endif
	}
}
#endif

for( i=0; i!=n; i++ )
{
	int max;

	{
		int j;
		max = i;
		for( j=i+1; j!=n; j++ ) if( m[j].value > m[max].value ) max=j;
	}

	do_move(m+max);

	S[Ply].check = checktest(Color);

	if( i == 0 || Depth <= 0 )
	result = - evaluate( -Beta, -Alpha );
	else
	{
#define	LMRLMP /* late move reductions and pruning */
#ifdef	LMRLMP
/* Entry condition simplified since XXIV, now we do reduce captures,
 * promotions and check evasions.  We do not reduce if Alpha is
 * a mate-in-n score, reducing may then lead to false mate announcements. */
		
		
		if (SkillLevel >= 1300 &&
    Depth > 0 &&
    Alpha > -(CHECKMATE - MAXPLY) &&
    Alpha <  (CHECKMATE - MAXPLY) &&
    !S[Ply].check && i > 2)
		  
		{
			int olddepth=Depth;
			Depth -= Depth/max(6,(33-i))+i+100;

			if( m[max].value <= 0 ) Depth -= 100;

			if( Depth>0 ) /* reduced search */
			{
				result = -evaluate( -Alpha-1, -Alpha );
				Depth = olddepth;
				if( result <= Alpha ) goto skipsearch;
			}
			else /* prune */
			{
				PV[Ply][Ply].from=0;
				Depth=olddepth;
				result=Alpha;
				goto skipsearch;
			}
			Depth = olddepth;
		}
#endif	/* LMRLMP */

		result = - evaluate( -Alpha-1, -Alpha );
		if( result>Alpha && result<Beta )
		result = - evaluate( -Beta, -Alpha-1 );

		skipsearch:;
	}


    undo_move(m + max);

    FollowPV = 0;

    if( result > Alpha )
	{
/*
{ int i; for(i=Counter-Ply;i!=Counter;i++) printm(G[i].m,NULL);
  printf("[%i,%i] %i",Alpha,Beta,result); getchar(); }
*/
		update_PV( m[max], Ply );
		Alpha = result;
		if(Alpha>=Beta)
		{
			if( i>0 ) slash_killers( m, i );
			m[max].value = (CHECKMATE-5000)+Depth;
			return Alpha;
		}
		maxi = i;
	}
	else if( i==0 && Depth>0 ) update_PV( m[max], Ply );
	/* The line above helps to maintain a nice long PV in those rare
	 * cases, where negascout is not able to confirm the fail-high
	 * and then truncates the PV. */

	{ tmove mo=m[max]; m[max] = m[i]; m[i] = mo; }
}

if( maxi != -1 ) m[maxi].value = (CHECKMATE-5000)+Depth;

return Alpha;

}

	
	
	
int EasyMove;
int bgs; /* best group size */

/**
*** Sorting root moves and detecting possible `Easy Move'.
**/

int sort_root_moves( tmove *m, int n )
{
	int i;
	int best = -CHECKMATE;
	int secondbest = -CHECKMATE;
	int result;

	bgs=0;

	FollowPV = 0;
	PV[0][0] = m[0];

	EasyMove = 0;

	for( i=0; i!=n; i++ )
	{
		do_move(m+i);

		result = - evaluate( -CHECKMATE, CHECKMATE );

		m[i].value = result/4;
		if( m[i].in2 )
		{ m[i].value += Values[ m[i].in2>>4 ]; bgs++; }
		else if( m[i].special )
		{ m[i].value += 100; bgs++; }
		if( checktest(Color) )
		{ m[i].value += 150; bgs++; }

		undo_move(m+i);

		if( result > best )
		{
			tmove mm=m[0]; m[0]=m[i]; m[i]=mm;
			secondbest = best; best = result;
			update_PV( m[0], 0 );
		}
		else if( result > secondbest ) secondbest = result;
	}

	qsort( m+1, n-1, sizeof(tmove), sortkey );

	if( n == 1 ) EasyMove = 3;
	else
	if( abs(best) > CHECKMATE - 100 ) EasyMove = 2;
	else
	if( best - secondbest > 70 ) EasyMove = 1;

	if( Flag.post && EasyMove!=0)
	{
		printf( "    -> easy move      (%i)  ", EasyMove );
		printm(m[0],NULL); puts("               ");
	}

	return best;
}



long LastTurn;
int Turns;



tmove root_search(void) {
  extern const char file[10];
  extern const char row[12];
  tmove m[256];
  int n, i;
  int Alpha, Beta;
  int r;
  int Nod[256];
  tmove candidate;
  extern long T1;
  TBHits = 0;
  
  

  Abort = 0;
  NoAbort = 0;  // Allow instant stop

  generate_legal_moves(m, &n, checktest(Color));

  if (Flag.log != NULL) {
    char pb[2048];
    if (Counter > 0) {
      fprintf(Flag.log, "\n");
      fprintf(Flag.log, Flag.ponder == 2 ? "  pondering move " : "  opponent plays ");
      printm(G[Counter - 1].m, pb);
      fprintf(Flag.log, "%s\n", pb);
    }
    printboard(pb);
    fprintf(Flag.log, "%s\n", pb);
  }

  if( Flag.book )
if( Counter < 20 || Bookout < 4 || Flag.analyze )
{
	int b;

	if( Flag.easy && Counter>4
	&& rand()%5000 < Counter*(50+Flag.easy) )
		b = -1;
	else
		b = bookmove( m, n );

	if( b != -1 )
	{
		if( Flag.easy )
		    #ifdef _WIN32
			usleep( (rand()%10000) * (50+2*Flag.easy) );
			#else
			sleep( (rand()%10000) * (50+2*Flag.easy) );
			#endif
		Bookout = 0;
		PV[0][1].from = 0;   /* dont start pondering */
		m[b].value = 0;
		do_move(m+b);
		return m[b];
	} else Bookout ++;
}


  init_killers();

  if (n == 0) {
    NoAbort = 0;
    goto end_search;
  }

  {
    int learndepth = 0;
    int learnalpha = 0;
    Age++;
    Nodes = 0;
    A_n = n;
    A_m = m;
    A_i = 0;
    PV[0][0].from = 0;
    l_startsearch();
    Ply = 0;

  //  LastIter = Alpha = sort_root_moves(m, n);
  Alpha = -CHECKMATE;
   LastIter = Alpha;

  
    LastTurn = ptime();

    /* === EASY MOVE == 3: SINGLE LEGAL MOVE === */
    if (EasyMove == 3 && !Flag.analyze && n > 0) {
      candidate = m[0];
      PV[0][0] = candidate;
      PV[0][1].from = 0;
      PV[0][1].to = 0;
      return candidate;
    }

    FollowPV = 1;
    NoAbort = 0;
    Depth = 290;
    Ply = 0;
    A_d = 2;
	
	
	if (CurrentDepthLimit < 9999) {  // using skill level
	 // Apply skill level depth limit to first iteration
    int skill_limited_depth = min(Depth, get_skill_depth_limit());
    if (skill_limited_depth < Depth) {
        #ifdef DEBUG
        printf("info string Skill level %d: limiting depth from %d to %d\n",
               SkillLevel, Depth, skill_limited_depth);
        #endif
        // The depth limiting will be enforced by should_apply_skill_depth_limit()
        // during the search
    }}
	

    memset(Nod, 0, 256 * sizeof(int));
    Turns = n;

   
  /* ====================================================================
 * MAIN ITERATION LOOP - FULLY UCI PONDER COMPLIANT WITH FAST ABORT
 * ==================================================================== */
while ((l_iterate() && !Abort && Depth < MAXPLY * 100)) {
    // === CRITICAL: CHECK FOR PONDERHIT TRANSITION FIRST ===
    
	 // ===== CRITICAL: Reset seldepth for new iteration =====
    CurrentSeldepth = 0;  // Reset at start of each iteration
	
	
	if (Flag.ponder == 3) {
        #ifdef DEBUG
		printf("info string PONDERHIT detected in search - applying time controls NOW\n");
		#endif
        fflush(stdout);

        // CRITICAL: We were pondering (infinite time), now we have real time limit
        // The T1 and time budget were already set by uci_command()
        // We just need to clear the ponder flag and enable time checking
        
        Flag.ponder = 0;  // Clear ponder state
        NoAbort = 0;      // Enable time checking
        Abort = 0;        // Clear any abort flags
        
        extern long T1, T2;
        long elapsed = ptime() - T1;
        
        #ifdef DEBUG
		printf("info string PONDERHIT: Time already elapsed=%ld cs, budget=%d cs\n",
               elapsed, Flag.centiseconds);
		#endif
		
        fflush(stdout);
        
        // If we've already used most of our time during pondering, abort now
        if (elapsed >= Flag.centiseconds) {
            #ifdef DEBUG
			printf("info string PONDERHIT: Time already expired, aborting search\n");
			#endif
            fflush(stdout);
            Abort = 1;
            break;
        }
    }

    // CRITICAL: Check abort at start of each iteration
    if (Abort) {
        #ifdef DEBUG
		printf("info string Aborting at iteration start\n");
		#endif
        fflush(stdout);
        break;
    }

    // Poll frequently for commands - EVERY 64 nodes instead of 256
    if ((Nodes & 63) == 0) {
        poll_uci_input();
        if (Abort) {
            #ifdef DEBUG
			printf("info string Aborting after input poll\n");
			#endif
            fflush(stdout);
            break;
        }
    }
    
    // CRITICAL: Check time limit (not during pondering mode 2)
    if (Flag.ponder != 2 && !Flag.analyze && time_is_up()) {
        Abort = 1;
        #ifdef DEBUG
		printf("info string Aborting: time_is_up() returned true\n");
		#endif
        fflush(stdout);
        break;
    }
    
    // When reporting info, use CurrentSeldepth:
    if ((Nodes & 4095) == 0) {
        if (Flag.analyze && PV[0][0].from != 0) {
            long t = ptime() - T1;
            int nps = (int)(((float)Nodes) / (((float)max(t, 1)) / 100));
            int pvlen = 0;
            while (PV[0][pvlen].from != 0 && pvlen < MAXPLY)
                pvlen++;
            
            // Pass CurrentSeldepth instead of A_d for seldepth
            uci_send_info(A_d, PV[0][0].value, t, Nodes, nps, PV[0], pvlen);
        }
    }
    
    
    if (Flag.analyze && TBHits >=1) {
	A_d=0;
	printf("\ninfo string TBHit > ");
	}else{
    A_d++;
	}

    Turns = 0;
    Beta = Alpha + WINDOW;
    Alpha = Alpha - WINDOW;

    for (i = 0; i != n; i++) {
        // CRITICAL: Poll at start of each root move for instant response
        poll_uci_input();
        if (Abort) {
            #ifdef DEBUG
			printf("info string Aborting in move loop (move %d/%d)\n", i, n);
			#endif
            fflush(stdout);
            break;
        }
        
        // CRITICAL: Check time every move during ponderhit search
        if (Flag.ponder == 0 && !Flag.analyze && time_is_up()) {
            Abort = 1;
            #ifdef DEBUG
			printf("info string Aborting: time expired during move %d\n", i);
			#endif
            fflush(stdout);
            break;
        }
        
        // Also check every 16 nodes within the move search
        int64 lastnodes = Nodes;
        A_i = i;
        do_move(m + i);

        #ifdef SCOUT
        if (i == 0) {
            r = -evaluate(-Beta, -Alpha);
            if (!Abort) m[i].value = r;
            if (m[i].value <= Alpha && !Abort) {
                infoline(4, NULL);
                r = -evaluate(-Alpha, CHECKMATE);
                if (!Abort) {
                    m[i].value = Alpha = r;
                    update_PV(m[0], 0);
                    infoline(1, NULL);
                }
            }
        } else {
            r = -evaluate(-Alpha - SWINDOW, -Alpha);
            if (!Abort) m[i].value = r;
            if (m[i].value > Alpha) {
                if (!Abort) {
                    Turns++;
                    if (i > bgs) bgs++;
                    update_PV(m[i], 0);
                    LastTurn = ptime();
                    if (EasyMove != 0) {
                        EasyMove = 0;
                        #ifdef DEBUG
						printf("    -> easy move off\n");
						#endif
                    }
                }
                if (m[i].value >= Alpha + SWINDOW) {
                    infoline(2, NULL);
                    r = -evaluate(-Beta, -Alpha);
                    if (!Abort) m[i].value = r;
                }
            }
        }
        #else
        r = -evaluate(-Beta, -Alpha);
        if (!Abort) m[i].value = r;
        if (i == 0 && m[i].value <= Alpha && !Abort) {
            infoline(4, NULL);
            Beta = Alpha;
            Alpha = -CHECKMATE;
            r = -evaluate(-Beta, -Alpha);
            if (!Abort) m[i].value = r;
        }
        #endif

        if (m[i].value >= Beta) {
            #ifndef SCOUT
            LastTurn = ptime();
            update_PV(m[i], 0);
            #endif
            PV[0][0].value = Beta;
            infoline(5, NULL);
            r = -evaluate(-CHECKMATE, -Beta);
            if (!Abort) m[i].value = r;
        }

        undo_move(m + i);

        if (Abort) {
            #ifdef DEBUG
			printf("info string Aborting after move %d\n", i);
			#endif
            fflush(stdout);
            break;
        }

        // CRITICAL: Check time after each move evaluation
        if (Flag.ponder == 0 && !Flag.analyze && time_is_up()) {
            Abort = 1;
            #ifdef DEBUG
			printf("info string Aborting: time check after move %d\n", i);
			#endif
            fflush(stdout);
            break;
        }

        if (m[i].value > Alpha) {
            tmove pom;
            #ifndef SCOUT
            LastTurn = ptime();
            #endif
            update_PV(m[i], 0);
            Alpha = m[i].value;
            infoline(1, NULL);

            pom = m[i];
            for (int j = i; j > 0; j--) m[j] = m[j - 1];
            m[0] = pom;
            Beta = Alpha + WINDOW;
        } else {
            int64 ipom = Nod[i] = Nodes - lastnodes;
            tmove pom = m[i];
            int j;
            for (j = i; j > bgs + 1 && Nod[i] > Nod[j - 1]; j--) {
                m[j] = m[j - 1];
                Nod[j] = Nod[j - 1];
            }
            m[j] = pom;
            Nod[j] = ipom;
        }
    }

    if (Abort) {
        #ifdef DEBUG
		printf("info string Aborting after move loop\n");
		#endif
        fflush(stdout);
        break;
    }

    // CRITICAL: Check time after completing iteration
    if (Flag.ponder == 0 && !Flag.analyze && time_is_up()) {
        Abort = 1;
        #ifdef DEBUG
		printf("info string Aborting: time check after iteration\n");
		#endif
        fflush(stdout);
        break;
    }

    if (!Abort && Depth > 300) {
        learndepth = Depth;
        learnalpha = Alpha;
    }

    infoline(3, NULL);
    Depth += 100;
    FollowPV = 1;
	
	
		// NEW: Enforce skill depth limit (stop ID early)
if (CurrentDepthLimit < 9999 && should_apply_skill_depth_limit(Depth)) {
    infoline(3, NULL);  // Final score line
    break;
}
	
	
    if(!Flag.analyze) {
   if (abs(Alpha) > CHECKMATE - MAXPLY)
        break;
	}
    if (abs(Alpha) > 29000 && EasyMove < 2) {
        EasyMove = 2;
        #ifdef DEBUG
		printf("    -> forced checkmate -> easy move (2)\n");
		#endif
    }

    LastIter = Alpha;
}
  
  
    Depth -= 100;
	
	
	
    AllDepth += Depth;
    {
      extern long T1;
      long t = ptime() - T1;
      if (t != 0)
        AllNPS += 100 * Nodes / t;
    }

    infoline(0, NULL);
    if (Flag.learn && learndepth)
      wlearn(learndepth, learnalpha);
  }
  
  
  // NEW: Post-search delay for low skill (use full time budget)
if (!Abort && !Flag.analyze && CurrentDepthLimit < 9999 &&
    (Flag.level == timecontrol || Flag.level == averagetime || Flag.level == fixedtime)) {
    
    long elapsed_ms = ptime() - T1;
    long budget_ms = Flag.centiseconds;  // Assumes centiseconds = ms (consistent with UCI)
    
    long target_usage_ms = budget_ms * 90LL / 100;
    long delay_needed = target_usage_ms - elapsed_ms;
    
    if (delay_needed > 30) {  // Min 30ms delay
        delay_needed = (delay_needed * 95LL) / 100;  // Conservative 95%
        delay_needed = max(delay_needed, 50LL);     // Min 50ms "think"
		delay_needed*=8;
        
        // Optional: Output "thinking" info
        #ifdef DEBUG
        printf("info string Skill delay: %ld ms (used %ld/%ld ms)\n", delay_needed, elapsed_ms, budget_ms);
        #endif
        fflush(stdout);
        
        delay_ms(delay_needed);
    }
}
  
  

end_search:


  /* ==================================================================
   * FINAL BESTMOVE VALIDATION + CASTLING FIX + PV CLEAR
   * ================================================================== */
  
  // CRITICAL FIX: Ensure we're generating moves for the CURRENT position
  // The search may have modified global state, so regenerate legal moves
  // for the side that is ACTUALLY to move right now.
  
  if (EasyMove == 3 && !Flag.analyze && n > 0) {
    candidate = m[0];
  } else if (PV[0][0].from != 0) {
    candidate = PV[0][0];
  } else if (n > 0) {
    candidate = m[0];
  } else {
    Flag.machine_color = 0;
    return (tmove){0};
  }

  // CRITICAL FIX: Regenerate legal moves for CURRENT board state
  // This prevents using stale moves from before the search
  tmove legal[256];
  int ln = 0;
  
  // Make sure we're checking for the correct side
  int current_check = checktest(Color);
  generate_legal_moves(legal, &ln, current_check);
  #ifdef DEBUG
  printf("info string Final validation: Color=%s, Counter=%d, legal_moves=%d\n",
         Color == WHITE ? "WHITE" : "BLACK", Counter, ln);
  printf("info string Candidate move: from=%d to=%d special=%d\n",
         candidate.from, candidate.to, candidate.special);
  #endif
  fflush(stdout);
  
  if (ln == 0) {
    #ifdef DEBUG
	printf("info string ERROR: No legal moves in final validation!\n");
	#endif
    fflush(stdout);
    Flag.machine_color = 0;
    return (tmove){0};
  }


  // Clear PV ponder moves
  PV[0][0] = candidate;
  PV[0][1].from = 0;
  PV[0][1].to = 0;

  return candidate;
}
