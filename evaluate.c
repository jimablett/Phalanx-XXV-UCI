/****************************
 * evaluate() function decides if given position is terminal (checkmate,
 * stalemate, ...) , if not, seeks it in the hash table, if not
 * present, calls dynamic (search()) or static (static_eval()) evaluation.
 * This also handles extensions.
 ******************/

#include "phalanx.h"

#define EXTENSION_BASE 80

#define NULL_MOVE_PRUNING

#define RECAPTURE_EXTENSIONS
#define PEE_EXTENSIONS     /* entering kings+pawns endgame extends */
#define CHECK_EXTENSIONS
#define PAWNPUSH_EXTENSIONS

#define minmv (P_VALUE)

/* 12 moves to draw: the static evaluation goes to zero */
#define RULE_50_CLOSE 24

#define EN_PASSANT 1

/**
***   Phalanx uses very simple implementation of static-eval cache.
***   It eats 512 kB and makes it about 10% faster on a 486.
**/
#undef CACHE
#ifdef CACHE
unsigned * C; /* 64k entries of 4 bytes: 256kB */
#endif

// ====================================================================
// SYZYGY TABLEBASE (FATHOM) PROBING LOGIC
// NOTE: External declarations for Fathom API and global vars are
//       assumed to be available via "phalanx.h" or defined locally.
// ====================================================================

// External Syzygy declarations (from phalanx.h)
extern int SyzygyMaxPieces;
extern bool SyzygyReady;
extern int SyzygyLoadedPieces;
extern int square_to_64(int sq120); // Assumed helper from phalanx.h

// Fathom API declarations


#include "fathom/src/tbprobe.h"


/**
 * Converts a 120-based square index (e.g., 21=A1, 98=H8) to a 0-63 index.
 * * @param sq120 The 120-based square index.
 * @return The 0-63 bitboard index.
 */

// Converts a 120-based square index (where rank 1 is row index 20, rank 8 is row index 90)
// to a 0-63 based square index (A1=0, H8=63).
int square_to_64(int sq120) {
    // 1. Extract the 1-based rank (1 to 8) and file (1 to 8)
    
    // Reverse of: sq = file + (rank + 1) * 10 
    
    // Rank (1-8): (sq120 / 10) - 1
    int rank_1_8 = (sq120 / 10) - 1;
    
    // File (1-8): (sq120 % 10)
    int file_1_8 = sq120 % 10;
    
    // 2. Convert to 0-based indices
    
    // The 64-based index typically has A1 = 0 and H8 = 63.
    // For A1=0, the rank must be 0-based (0 to 7) and reversed (8 -> 7, 1 -> 0)
    // and the file must be 0-based (0 to 7).

    // 0-based Rank (0-7, A1=0, A8=7): 8 - rank_1_8
    // Note: The board display code iterates ranks from 8 DOWN to 1,
    // which corresponds to the standard 64-based index where A8 is (7*8)+0=56, and A1 is (0*8)+0=0.
    // Therefore, an 8-based rank maps to a 0-based rank as (rank - 1).
    // The desired 0-based rank (0 for rank 1, 7 for rank 8) is:
    int rank_0_7 = rank_1_8 - 1;
    
    // 0-based File (0-7): file_1_8 - 1
    int file_0_7 = file_1_8 - 1;
    
    // 3. Calculate 64-based index
    // 64-based index = (rank_0_7 * 8) + file_0_7
    return (rank_0_7 * 8) + file_0_7;
}


// tb_probe_score() implementation (Returns centipawn score, 0 if no hit/error)


int tb_probe_score(void) {
    if (!SyzygyReady || SyzygyMaxPieces == 0) {
        return 0;
    }

    uint64_t w_kings = 0, w_queens = 0, w_rooks = 0, w_bishops = 0, w_knights = 0, w_pawns = 0;
    uint64_t b_kings = 0, b_queens = 0, b_rooks = 0, b_bishops = 0, b_knights = 0, b_pawns = 0;
    uint64_t w_all = 0, b_all = 0;

    int total_pieces = 0;        // Includes kings — this is what Syzygy uses

    // Build bitboards and count pieces
    for (int s = A1; s <= H8; s++) {
        if (B[s] == 0 || B[s] == 3) continue;

        int pc = B[s] >> 4;      // 1=pawn ... 6=king
        int c  = color(B[s]);    // WHITE or BLACK
        int sq64 = square_to_64(s);
        uint64_t bit = (1ULL << sq64);

        if (c == WHITE) w_all |= bit;
        else b_all |= bit;

        total_pieces++;          // Count every piece including kings

        if (pc == 6) {  // King
            if (c == WHITE) w_kings |= bit;
            else b_kings |= bit;
        } else {
            // Non-king pieces — populate correct bitboards
            if (pc == 5) { if (c == WHITE) w_queens   |= bit; else b_queens   |= bit; }
            else if (pc == 4) { if (c == WHITE) w_rooks    |= bit; else b_rooks    |= bit; }
            else if (pc == 3) { if (c == WHITE) w_bishops  |= bit; else b_bishops  |= bit; }
            else if (pc == 2) { if (c == WHITE) w_knights  |= bit; else b_knights  |= bit; }
            else if (pc == 1) { if (c == WHITE) w_pawns    |= bit; else b_pawns    |= bit; }
        }
    }

    // Determine maximum number of pieces we are allowed/able to probe
    int max_probe_pieces = SyzygyMaxPieces;
    if (SyzygyLoadedPieces <= 1) {
        extern unsigned TB_LARGEST;
        max_probe_pieces = min(max_probe_pieces, (int)TB_LARGEST);
    } else {
        max_probe_pieces = min(max_probe_pieces, SyzygyLoadedPieces);
    }

    // Syzygy tables are indexed by TOTAL piece count (kings included)
    // Do not probe positions with fewer than 3 or more than max pieces
    if (total_pieces > max_probe_pieces || total_pieces < 3) {
        return 0;
    }

    // Perform the actual WDL probe
    // IMPORTANT: Fathom requires rule50 = 0 and castling = 0 for WDL probes
    unsigned result = tb_probe_wdl(
        w_all, b_all,
        w_kings | b_kings,
        w_queens | b_queens,
        w_rooks | b_rooks,
        w_bishops | b_bishops,
        w_knights | b_knights,
        w_pawns | b_pawns,
        0,                                                  // rule50 must be 0
        0,                                                  // castling must be 0
        (G[Counter].m.special == EN_PASSANT) ? square_to_64(G[Counter].m.to) : 64,
        (Color == WHITE)
    );

    if (result == TB_RESULT_FAILED) {
        return 0;
    }

    // Success — tablebase hit!
    TBHits++;

    int score;
    switch (result) {
        case TB_WIN:           score = TB_WIN_SCORE;   break;  // +15000
        case TB_LOSS:          score = TB_LOSS_SCORE;  break;  // -15000
        case TB_BLESSED_LOSS:
        case TB_CURSED_WIN:
        case TB_DRAW:
        default:               score = TB_DRAW_SCORE;  break;  // 0
    }

    // Adjust score by ply to favor shorter wins/losses
    if (score > TB_WIN_SCORE - 500) {
        score -= Ply;   // Faster win → higher effective score
    } else if (score < TB_LOSS_SCORE + 500) {
        score += Ply;   // Delay inevitable loss
    }

    return score;
}



void initcache(void)
{
#ifdef CACHE
C = malloc(0x10000*sizeof(unsigned)); /* 64k entries of 8 bytes: 512kB */
if( C==NULL ) { puts("cannot alloc static eval cache!"); exit(0); }
#endif
}


tsearchnode S[MAXPLY];

static inline int approx_eval(void)
{
S[Ply].psnl = - S[Ply-1].psnl;

S[Ply].devi = S[Ply-1].devi*2/3 + abs(S[Ply].psnl)/8;
if( G[Counter-1].m.in2 ) S[Ply].devi += 60; else S[Ply].devi += 40;

/* pawn promotions should mostly trigger full static eval in the child node,
 * and avoid the lazy one, otherwise the engine delays promotions due to
 * the high bonus for the pawn on the 7th row. */
if(
	( G[Counter-1].m.in1 == WP && G[Counter-1].m.to >= A7 )
	||
	( G[Counter-1].m.in1 == BP && G[Counter-1].m.to <= H2 )
  )
	S[Ply].devi += 3*P_VALUE;

return G[Counter].mtrl - G[Counter].xmtrl + S[Ply].psnl;
}



int static_eval(void)
{

int positional;
int r50 = 100 - G[Counter].rule50;
int material = G[Counter].mtrl-G[Counter].xmtrl;

#ifdef CACHE
if( ( ( C[ 0x0000FFFF & G[Counter].hashboard ] ^ G[Counter].hashboard )
    & 0xFFFF0000 ) == 0 )
{
unsigned * cc = C + ( 0x0000FFFF & G[Counter].hashboard );
Wknow.prune = (((*cc)&0x00004FFF)!=0);
Bknow.prune = (((*cc)&0x00008FFF)!=0);
return ( (*cc) & 0x00003FFF ) ;
}
#endif

positional = score_position();

if( r50 < RULE_50_CLOSE )
{ positional = positional * r50 / RULE_50_CLOSE;
  material = material * r50 / RULE_50_CLOSE;
}

#undef RAISE
#ifdef RAISE
/* Inbalanced material: take positional bonus seriously */
if( abs(material) > 200 )
{
	int m = abs(material) - 200;
	if( m > 1000 ) m=1000;
#ifdef SCORING
	if( Scoring )
		printf(
		" (   ) inbalanced material, positional raised: %i -> %i\n",
		positional, positional + positional*m/500 );
#endif
	positional =        positional + positional*m/500;
}
#endif

#ifdef CACHE
{
unsigned * cc = C + ( 0x0000FFFF & G[Counter].hashboard );
*cc = ( 0xFFFF0000 & G[Counter].hashboard ) | ( material+positional );
if( Wknow.prune ) *cc |= 0x00004000; else *cc &= (0xFFFFFFFF-0x00004000);
if( Bknow.prune ) *cc |= 0x00008000; else *cc &= (0xFFFFFFFF-0x00008000);
}
#endif

S[Ply].psnl = positional;
S[Ply].devi = 0;

#undef debug
#ifdef debug
if(abs(positional)>600)
{ Scoring = 1; score_position(); Scoring = 0;
  printboard(); printf("[%i,%i,%i]",material,positional,score_position());
  getchar();
}
#endif

return material+positional;


 int final_result = material + positional;
    
    // Apply skill-based evaluation noise for weaker play
    final_result = apply_skill_eval_noise(final_result);
    
    return final_result;


}



inline int repetition( int n )
{
	int i;
	int r=0;
	unsigned board = G[Counter].hashboard;
	for( i=Counter-2; i>=0; i-=2 )
	{
		if( G[i].hashboard == board ) if( ++r == n ) return 1;
		if( G[i].rule50 <= 1 ) break;
	}
	return 0;
}



int material_draw( void )
{
	int i, n=2;

	for( i=L[WKP].next; i!=0; i=L[i].next )
	switch( B[i] )
	{	case WQ: case WR: case WP: return 0;
		default: n--; if( n==0 ) return 0;
	}

	for( i=L[BKP].next; i!=0; i=L[i].next )
	switch( B[i] )
	{	case BQ: case BR: case BP: return 0;
		default: n--; if( n==0 ) return 0;
	}

	return 1;
}


// GLOBAL VARIABLE
int TotalPiecesOnBoard = 0;

/*****************************************************************/

int evaluate( int Alpha, int Beta )
{
    
	TRACK_SELDEPTH();
	static int timeslice = 2000;
    static int polslice = 4000;
    int result;
    tmove m[256]; int n; /* moves and number of moves */
    thashentry *t;
    int check = 0;
    int lastiter;
    int totmat = Totmat = G[Counter].mtrl + G[Counter].xmtrl;
    (void)totmat;

    if (Ply % 2) lastiter = -LastIter; else lastiter = LastIter;
    Nodes++;

    if ( Flag.level == fixedtime || Flag.level == timecontrol )
        if ( ( Nodes % timeslice ) == 0 && !Flag.analyze )
        {
            extern long T1;
            int t = Flag.centiseconds - ptime() + T1;
            if ( t < 0 ) { if( Flag.ponder >= 2 ) Flag.ponder = 3; else Abort = 2; }
            else if ( t != Flag.centiseconds )
                timeslice = Nodes * t / ( Flag.centiseconds - t ) * 2 / 3;
            if (timeslice > 5*Flag.centiseconds) timeslice = 5*Flag.centiseconds;
            if (timeslice < 50) timeslice = 50;
        }

    {
        static int64 lnodes = 0;
        if ( lnodes + polslice < Nodes || Nodes == 1 )
        {
            static long lptime = 0;
            long nptime = ptime();
            if ( nptime == lptime ) nptime++;
            if ( nptime - lptime < 100 ) polslice = polslice*11/10;
            else { polslice = polslice*10/11; }
            lptime = nptime;
            lnodes = Nodes;
        }
    }

    if ( Ply >= MAXPLY-2 )
    { PV[Ply][Ply].from = 0; return G[Counter].mtrl - G[Counter].xmtrl; }

    if ( G[Counter].rule50 >= 100 ) /* 50 moves draw */
    { PV[Ply][Ply].from = 0; return DRAW; }

    /* insufficient material draw */
    if ( G[Counter].mtrl < 400 && G[Counter].xmtrl < 400 )
        if ( material_draw() )
        { PV[Ply][Ply].from = 0; return DRAW; }

    if ( G[Counter].rule50 >= 3 )
        if ( repetition(1) ) /* third rep. draw */
        {
            int j;
            int ext = 0;

            /* NEW: Check if engine should avoid this draw */
            int should_avoid_draw = 0;

            if ( RepetitionAvoidanceThreshold > 0 )
            {
                /* Calculate current position evaluation */
                int material = G[Counter].mtrl - G[Counter].xmtrl;
                int positional = score_position();
                int total_eval = material + positional;

                /* Determine if current side is ahead by threshold amount */
                if ( Color == WHITE && total_eval >= RepetitionAvoidanceThreshold )
                {
                    should_avoid_draw = 1;
                    #if DRAW_AVOIDANCE_DEBUG
                    printf("info string WHITE ahead by %d cp (thresh=%d) - REFUSING draw\n",
                           total_eval, RepetitionAvoidanceThreshold);
                    #endif
                }
                else if ( Color == BLACK && total_eval <= -RepetitionAvoidanceThreshold )
                {
                    should_avoid_draw = 1;
                    #if DRAW_AVOIDANCE_DEBUG
                    printf("info string BLACK ahead by %d cp (thresh=%d) - REFUSING draw\n",
                           -total_eval, RepetitionAvoidanceThreshold);
                    #endif
                }

                fflush(stdout);
            }

            /* If ahead by threshold, don't accept the draw - continue searching */
            if ( should_avoid_draw )
            {
                /* Skip the draw return and continue with normal evaluation */
                goto continue_evaluation;
            }

            /* ORIGINAL CODE: Standard 3-fold repetition draw handling */
            {
                PV[Ply][Ply].from = 0;
                for ( j = Counter - 1; j > Counter - Ply; j -= 2 )
                {
                    ext += G[j-1].m.dch - G[j].m.dch;
                }
                if ( ext > 0 )
                {
                    if ( ext > 500 ) ext = 500;
                    ext += Ply*20;
                }
                else if ( ext < 0 )
                {
                    if ( ext < -500 ) ext = -500;
                    ext -= Ply*20;
                }
                /* Speculative drawscore... */
                return DRAW + ext/(15 + max(-5, Depth/10));
            }
        }

continue_evaluation:;

    /********************************************************************
     * Now it is time to look into the hashtable.
     ********************************************************************/
    if ( SizeHT == 0 || Depth < 0 ) t = NULL;
    else if ( (t = seekHT()) != NULL )
        if ( Age == t->age || Beta - Alpha == 1 )
            if ( t->depth >= Depth || ( Depth < 300 && abs(t->value) >= CHECKMATE - MAXPLY ) )
            {
                int val = t->value;
                /* Adjust stored mate scores to the current Ply */
                if ( val >= CHECKMATE - MAXPLY ) val -= Ply;
                else if ( val <= -CHECKMATE + MAXPLY ) val += Ply;

                switch ( t->result )
                {
                    case no_cut:
                        PV[Ply][Ply].from = 0;
                        return val;
                    case alpha_cut:
                        if ( val <= Alpha ) { PV[Ply][Ply].from = 0; return Alpha; }
                        break;
                    case beta_cut:
                        if ( val >= Beta ) { PV[Ply][Ply].from = 0; return Beta; }
                        break;
                }
            }

    /***** End of hashtable stuff *****/

    /* Skill depth limit: cutoff deep searches for weak play */
    if ( CurrentDepthLimit < 9999 && should_apply_skill_depth_limit(Ply * 100) ) {
        int ev = static_eval();
        return apply_skill_eval_noise(ev);
    }

    // ====== SKILL LEVEL DEPTH LIMITING ======
    if ( should_apply_skill_depth_limit(Depth) ) {
        int ev;
        if ( Ply < 2 )
            S[Ply].check = check = checktest(Color);
        else
            check = S[Ply].check;

        if ( check ) {
            ev = approx_eval();
        } else {
            ev = static_eval();
        }

        ev = apply_skill_eval_noise(ev);

        PV[Ply][Ply].from = 0;
        return ev;
    }
    // ==========================================

#undef nodef
#ifdef nodef
    {
        static int64 int good = 0, all = 0;
        if ( t != NULL && t->move != 0 ) good++;
        all++;
        if ( all%100000 == 0 && all != 0 )
            printf("hit percentage = %lld.%02lld%%\n", good*100/all, good*10000/all%100);
    }
#endif

    if ( Ply < 2 ) /* called from rootsearch() */
        S[Ply].check = check = checktest(Color);
    else /* called from search() and the checktest() has been run */
        check = S[Ply].check;

    if ( Depth > 0 || check )
    {
        if ( Depth <= 0 ) result = approx_eval();
        else result = static_eval();

#ifdef FORWARD_PRUNING
        if ( !check
            && Depth < 200
            && ( Color == WHITE
                 ? ( Wknow.hung < 14 && Wknow.khung < 6 )
                 : ( Bknow.hung < 14 && Bknow.khung < 6 )
               )
        ) {
            int r = result - (P_VALUE + Depth) / 10;
            if ( r >= Beta ) { PV[Ply][Ply].from = 0; return Beta; }
            if ( r > Alpha ) Alpha = r;
        }
#endif /* FORWARD_PRUNING */

        generate_legal_moves(m, &n, check);

        /** Return, if there is no legal move - checkmate or stalemate **/
        if ( n == 0 )
        {
            PV[Ply][Ply].from = 0;
#ifdef DEBUG_TERMINAL
            printf("DEBUG TERMINAL: Ply=%d, check=%d, n=0\n", Ply, check);
            printf(" Color=%s, Counter=%d\n", Color==WHITE?"WHITE":"BLACK", Counter);
            printf(" Returning: %s\n", check ? "CHECKMATE" : "STALEMATE");
            if ( check ) {
                printf(" Mate-encoding example: CHECKMATE - Ply = %d - %d = %d\n",
                       CHECKMATE, Ply, CHECKMATE - Ply);
            }
            fflush(stdout);
#endif
            if ( check ) return Ply - CHECKMATE; /* Checkmate (negative value convention handled elsewhere) */
            else return DRAW; /* Stalemate */
        }

        /* SYZYGY TABLEBASE PROBE - Only after confirming position is legal */
        if ( TotalPiecesOnBoard <= SyzygyMaxPieces && SyzygyReady ) {
            int tb_score = tb_probe_score();
            if ( tb_score != 0 ) {
                /* Return the TB score if a hit is found (tb_probe_score handles ply adjustments / DTZ) */
                return tb_score;
            }
        }

#ifdef NULL_MOVE_PRUNING
        if (
            Depth > 100
            && ( result >= Beta || Depth > 350 )
            && ! FollowPV
            && ! check
            && n > 4
            && ( (Color == WHITE) ? (Wknow.q||Wknow.r||Wknow.b||Wknow.n>1)
                                  : (Bknow.q||Bknow.r||Bknow.b||Bknow.n>1) )
            && G[Counter-1].m.special != NULL_MOVE /* prev. node not nm */
            && ( t == NULL || t->depth <= Depth-350
                 || t->result == beta_cut || t->value >= Beta )
        )
        {
            int value;
            int olddepth = Depth;
            G[Counter].m.in1 = 0; /* disable en passant */
            G[Counter].m.special = NULL_MOVE;
            G[Counter].m.to = 2;
            Counter ++; Ply ++;
            G[Counter].castling = G[Counter-1].castling;
            G[Counter].rule50 = G[Counter-1].rule50 + 1;
            G[Counter].hashboard = HASH_COLOR ^ G[Counter-1].hashboard;
            G[Counter].mtrl = G[Counter-1].xmtrl;
            G[Counter].xmtrl = G[Counter-1].mtrl;
            Color = enemy(Color);
            S[Ply].check = 0;
            /* reduction depends on number of moves generated */
            Depth -= 200 + n*5;
            if ( Depth < 0 ) Depth = 0; else Depth -= Depth/4;
            value = -evaluate(-Beta, -Beta+1);
            Color = enemy(Color);
            Counter --; Ply --;
            Depth = olddepth;
            Totmat = totmat;
            if ( value >= Beta ) { result = Beta; goto end; }
        }
#endif

#ifdef PROBCUT
#define MRGN 190
        if ( Depth > 350 && Beta - Alpha == 1 && !check )
        {
            int i;
            int r = Alpha;
            int olddepth = Depth;
            Depth -= 350;
            for ( i = 0; i != n; i++ )
                if ( m[i].in2 && result + Values[m[i].in2 >> 4] >= Beta + MRGN )
                {
                    do_move(m+i); S[Ply].check = checktest(Color);
                    r = - evaluate( -Beta - MRGN, -Beta - MRGN + 1 );
                    undo_move(m+i);
                    if ( r >= Beta + MRGN ) break;
                }
            Depth = olddepth;
            if ( r >= Beta + MRGN ) { result = Beta; goto end; }
        }
#endif /* PROBCUT */

#ifdef CHECK_EXTENSIONS
        if ( check && Depth > 0 )
        {
            int newdch, i;
            newdch =
               EXTENSION_BASE - 20
             - 3000/(Depth+100) /* 30..0 */
             - 200/(n*(n+1)); /* 1~100,2~33,3~16,..0 */
            if ( result <= lastiter-50 || result <= -250 ) /* losing anyway */
                newdch = (newdch + 100) / 2; /* smaller extension, 50% */
            for ( i = 0; i != n; i++ )
                if ( m[i].in2 ) m[i].dch = (newdch + 100) / 2; /* capture */
                else m[i].dch = newdch;
        }
#endif

#ifdef PAWNPUSH_EXTENSIONS
        if ( Totmat < 4000 )
            if ( result < lastiter + 50 && result < 250 ) /* winning anyway: don't extend */
            {
                int i;
                for ( i = 0; i != n; i++ )
                    if ( piece(m[i].in2a) == PAWN
                         && m[i].special == 0
                         && ( ( Color == WHITE && m[i].to >= A6 )
                            || ( Color == BLACK && m[i].to <= H3 ) ) )
                    {
                        int j;
                        int support = 0;
                        int newdch = EXTENSION_BASE;
                        if ( Color == WHITE )
                        {
                            for ( j = m[i].to + 10; j <= H7; j += 10 )
                                if ( B[j-1] == BP || B[j] == BP || B[j+1] == BP )
                                    goto no_push_extension;
                            for ( j = m[i].to; j <= H8; j += 10 )
                            {
                                if ( B[j] ) support--;
                                if ( see(B, m[i].from, j) < 0 ) support--;
                                else support++;
                            }
                        }
                        else
                        {
                            for ( j = m[i].to - 10; j >= A2; j -= 10 )
                                if ( B[j-1] == WP || B[j] == WP || B[j+1] == WP )
                                    goto no_push_extension;
                            for ( j = m[i].to; j >= A1; j -= 10 )
                            {
                                if ( B[j] ) support--;
                                if ( see(B, m[i].from, j) < 0 ) support--;
                                else support++;
                            }
                        }
                        newdch += Depth/20 + Totmat/100 - 30*support;
                        switch ( m[i].to / 10 )
                        { case 8: case 3:
                          if ( support > 0 ) newdch -= 20*support + 10; break; }
                        if ( newdch < m[i].dch ) { m[i].dch = newdch; }
                    no_push_extension:;
                    }
            }
#endif

#ifdef RECAPTURE_EXTENSIONS
        if ( Depth > 0 && G[Counter-1].m.in2 )
            if ( result < lastiter + 50 && result < 250 ) /* winning anyway: don't */
            {
                int i, t = G[Counter-1].m.to;
                int v1 = Values[ G[Counter-1].m.in1 >> 4 ];
                int v2 = Values[ G[Counter-1].m.in2 >> 4 ];
                int newdch;
                if ( Depth <= 200 )
                    newdch = EXTENSION_BASE - 30;
                else
                    newdch = EXTENSION_BASE - 20;
                if ( min(v1, v2) > 400 || G[Counter].mtrl < Q_VALUE )
                    newdch -= 70;
                else if ( min(v1, v2) > 200 || G[Counter].mtrl < (Q_VALUE + N_VALUE) )
                    newdch -= 45;
                for ( i = 0; i != n; i++ )
                    if ( m[i].to == t )
                        if ( m[i].dch > newdch )
                            m[i].dch = newdch;
            }
#endif

#ifdef PEE_EXTENSIONS
        if ( Depth > 200
         && G[Counter].mtrl <= (8*P_VALUE)
         && G[Counter].xmtrl <= (8*P_VALUE + Q_VALUE)
         && G[Counter-1].m.in2 > KNIGHT )
        {
            int i;
            int target = 0;
            int cdch;
            for ( i = L[L[Color].next].next; i != 0; i = L[i].next )
                if ( B[i] >= KNIGHT ) goto nopee;
            for ( i = L[L[enemy(Color)].next].next; i != 0; i = L[i].next )
                if ( B[i] >= KNIGHT )
                { if ( target ) goto nopee; else target = i; }
            if ( !target ) goto nopee;
            if ( G[Counter-1].m.in2 >= QUEEN ) cdch = min(Depth,450);
            else cdch = min(Depth*2/3,350);
            for ( i = 0; i != n; i++ )
                if ( m[i].to == target ) { m[i].dch -= cdch; }
        nopee:;
        }
#endif

#ifdef ETTC
        if ( Depth > 200 && SizeHT != 0 )
        {
            extern void do_hash(tmove *), undo_hash(tmove *);
            int i;
            for ( i = 0; i != n; i++ )
            {
                thashentry *th;
                do_hash(m+i);
                if ( (th = seekHT()) != NULL )
                    if ( th->depth >= Depth )
                        if ( th->result != beta_cut )
                        {
                            int val = -th->value;
                            /* use MAXPLY for mate-range detection */
                            if ( val >= CHECKMATE - MAXPLY ) val -= Ply;
                            else if ( val <= -CHECKMATE + MAXPLY ) val += Ply;
                            if ( val > Alpha )
                            {
                                if ( val >= Beta )
                                {
                                    undo_hash(m+i);
                                    G[Counter].m = m[i];
                                    PV[Ply][Ply] = m[i];
                                    PV[Ply][Ply+1].from = 0;
                                    result = Beta; goto end;
                                }
                                Alpha = val;
                            }
                        }
                undo_hash(m+i);
            }
        }
#endif
    }
    else
    { /*** Quiescence search ***/
        result = approx_eval();
        if ( ( result < Beta + S[Ply].devi && result > Alpha - S[Ply].devi )
             || Totmat <= (B_VALUE + P_VALUE) || Ply < 2 )
            result = static_eval();
        if ( result >= Beta ) return result;
        if ( result <= Alpha - (Q_VALUE + minmv) && Depth <= -100 ) { return Alpha; }
#ifdef QCAPSONLY
        generate_legal_captures(m, &n, Alpha - result - minmv);
#else
        if ( G[Counter].mtrl - G[Counter].xmtrl < 400 && Depth > -100 )
            generate_legal_checks(m, &n);
        else
            generate_legal_captures(m, &n, Alpha - result - minmv);
#endif
    }

    /*** Compute heuristic values of moves ***/
    add_killer( m, n, t );
    PV[Ply][Ply].from = 0;

    if ( Flag.nps )
    {
        if ( Flag.easy )
        { if ( n > 10 && Flag.easy <= 100 ) blunder(m, &n); }
        if ( polslice > Flag.easy + 200 ) polslice = Flag.easy + 200;
        if ( Nodes % (1 + Flag.nps / 500) == 0 )
        {
            int nps_actual = Nodes * 100 / max(ptime() - T1, 1);
    #ifdef _WIN32
            if ( nps_actual > Flag.nps && !Abort ) usleep(100000);
    #else
            if ( nps_actual > Flag.nps && !Abort ) sleep(100000);
    #endif
        }
    }

    /*** Full-width search ***/
    if ( Depth > 0 || check )
    {
        result = search(m, n, Alpha, Beta);
    }
    else /*** Quiescence ***/
    {
        if ( result > Alpha )
            result = search(m, n, result, Beta);
        else
            result = search(m, n, Alpha, Beta);
    }

    /* ===================================================================== */
    /* CRITICAL FIX: Restore PV when we fail high (beta cutoff)              */
    /* This ensures mate PVs are not lost on cutoff nodes.                   */
    /* ===================================================================== */
    if ( result >= Beta && Depth > 0 && G[Counter].m.from != 0 ) {
        PV[Ply][Ply] = G[Counter].m; /* move that caused cutoff */
        memcpy(&PV[Ply][Ply + 1], PV[Ply + 1], sizeof(tmove) * (MAXPLY - Ply - 1));
        PV[Ply][MAXPLY - 1].from = 0; /* terminate line */
    }

end:
    if ( result >= Beta && Depth > 0 )
        write_killer( G[Counter].m.from, G[Counter].m.to );

    if ( SizeHT != 0 && Depth >= 0 && Abort == 0 ) writeHT( result, Alpha, Beta );

    /* Learning file: apply ply-adjustment if it looks like a mate */
    if ( Depth > 300 && Flag.learn )
        if ( Depth > 400 || Ply < 3 )
        {
            int lresult = rlearn();
            if ( lresult != NOVALUE )
            {
                PV[Ply][Ply].from = 0;
                if ( lresult >= CHECKMATE - MAXPLY ) lresult -= Ply;
                if ( lresult <= -CHECKMATE + MAXPLY ) lresult += Ply;
                result = lresult;
            }
        }

    return result;
}
