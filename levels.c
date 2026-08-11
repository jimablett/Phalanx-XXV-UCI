#include "phalanx.h"

long T1, T2;

long Time = 600 * 100;
long Otim = 600 * 100;

/* return time in centiseconds */
long ptime(void) {
  if (Flag.easy)
    return Nodes;
  else if (Flag.cpu)
    return ((double)clock()) * 100 / CLOCKS_PER_SEC;
  else {
#ifdef _WIN32
    return GetTickCount() / 10;
#else
    struct timeval t;
    gettimeofday(&t, NULL);
    return t.tv_sec * 100 + t.tv_usec / 10000;
#endif
  }
}

void l_level(char *l) {
  int moves, seconds, minutes, increment;

  while (*l == ' ')
    l++;
  moves = atoi(l);

  while (isdigit(*l))
    l++;
  while (*l == ' ')
    l++;

  if (*l == '\n' || *l == '\0')
    if (!Flag.easy) {
      printf("fixed time %i seconds\n", moves);
      Flag.level = fixedtime;
      Flag.centiseconds = moves * 100;
      return;
    }

  minutes = atoi(l);

  while (isdigit(*l))
    l++;
  if (*l == ':') {
    l++;
    seconds = atoi(l);
    while (isdigit(*l))
      l++;
  } else
    seconds = 0;

  while (isdigit(*l))
    l++;
  while (*l == ' ')
    l++;
  increment = atoi(l);

  if (moves == 0)
    printf("level: all moves in %i:%02i, increment %i seconds\n", minutes,
           seconds, increment);
  else
    printf("level: %i moves in %i:%02i, increment %i seconds\n", moves, minutes,
           seconds, increment);

  if (Flag.easy) {
    if (moves == 0)
      moves = 80;
    Flag.level = fixedtime;
    Flag.centiseconds = (increment + minutes * 60 / moves) * (150 - Flag.easy);
   
      printf("setting avg time to %i cs\n", Flag.centiseconds);
  } else {
    Flag.level = timecontrol;
    Flag.moves = moves;
    Flag.centiseconds = minutes * 6000 + seconds * 100;
    Flag.increment = increment;
    Time = Flag.centiseconds;
  }
  
}


void l_startsearch(void) {
    int moves;
    T1 = ptime();
    
    if (Flag.analyze) Flag.ponder = 0;

    switch (Flag.level) {

    case timecontrol:
        // *** CRITICAL FIX: If uci_go already calculated a budget, use it directly ***
        // We detect this by checking if Time was set to match Flag.centiseconds
        // (which apply_time_controls does: Time = budget_cs)
        
        if (Flag.centiseconds > 0 && Time == Flag.centiseconds) {
            // Budget was pre-calculated by uci_go/apply_time_controls
            // Just use it directly without recalculating
            
            T2 = Flag.centiseconds * 2LL / 3;  // Set soft limit to 2/3 of hard limit

            #ifdef DEBUG
            printf("info string TIME CALC: Using pre-calculated budget=%dcs inc=%dcs moves=%d Counter=%d\n",
                   Flag.centiseconds, Flag.increment, Flag.moves, Counter);
            printf("info string SOFT LIMIT T2=%ldcs (%.2fs)\n", T2, T2/100.0);
            #endif
            
            if (Flag.centiseconds < 25) Flag.centiseconds = 25;
            goto print_hard_limit; 
        }

        // --- OLD TIME CONTROL LOGIC (for 'level' command only) ---
        
        if (Flag.moves > 0)
            moves = Flag.moves;
        else {
            moves = Counter + (G[Counter].mtrl + G[Counter].xmtrl) / 800;
            if (Counter < 120)
                moves += 60 - Counter / 4;
            else
                moves += 30;
        }

        #ifdef DEBUG
        printf("info string TIME CALC (OLD): Time=%ldcs inc=%dcs moves=%d Counter=%d\n",
               Time, Flag.increment, moves, Counter);
        #endif
        
        /* Increment adjustment - FIXED */
        if (Flag.increment == 0) {
            // BUG FIX: Don't set T2=0 just because there's no increment!
            if (Time <= 0) {
                T2 = 0;
            } else {
                // No increment, but we have time - allocate based on remaining time
                T2 = Time / (moves + 4);
            }
        } else if (Time / Flag.increment >= 1600) {
            T2 = Flag.increment * 60;
        } else if (Time / Flag.increment <= 400) {
            T2 = Flag.increment * 10;
        } else {
            T2 = Flag.increment * (Time / Flag.increment / 8 - 20) / 3;
            T2 += Time / (moves + 4);
        }

        if (Flag.ponder)
            T2 += T2 / 8;

        /* Low-time tweaks */
        if (Flag.increment == 0 && Flag.level == timecontrol) {
            if (Time < 150)
                T2 -= T2 / 2;
            else if (Time < 600)
                T2 -= T2 / 4;
        }
        
        #ifdef DEBUG
        printf("info string SOFT LIMIT T2=%ldcs (%.2fs)\n", T2, T2/100.0);
        #endif
        
        /* Hard limit calculation */
        Flag.centiseconds = T2 * 150LL / 100;
        
        if (Time > 5000) {
            Flag.centiseconds += min(1000, Time / 50);
        }
        
        if (Time > Flag.increment * 10) {
            Flag.centiseconds += Flag.increment / 2;
        }

        /* Cap at available time minus safety margin */
        long max_time = Time - max(50, Flag.increment / 4);
        if (Flag.centiseconds > max_time)
            Flag.centiseconds = max_time;
        
        if (Flag.centiseconds < 25)
            Flag.centiseconds = 25;

print_hard_limit:
        
        #ifdef DEBUG
        printf("info string HARD LIMIT centiseconds=%dcs (%.2fs)\n", 
               Flag.centiseconds, Flag.centiseconds/100.0);
        #endif
        
        break;

    case fixedtime:
    case fixeddepth:
        T2 = 0;
        break;

    case averagetime:
        T2 = Flag.centiseconds / 3;
        break;
    default:
        break;
    }

    if (T2 < 1) T2 = 1;
}


int l_iterate(void) {
    if (!NoAbort && terminal()) {
        Abort = 1;
        return 0;
    }

    if (Flag.ponder == 2) {
        if (uci_input_ready()) return 1;
        return 1;
    }

    if (Flag.level == fixeddepth)
        return (Depth < Flag.depth);

    if (time_is_up()) {
        Abort = 1;
        return 0;
    }

    long t = ptime() - T1;

    if (Flag.level == fixedtime)
        return (t <= Flag.centiseconds);

    switch (EasyMove) {
    case 1:  return (t <= T2 / 3);
    case 2:  return (t <= T2 / 6);
    default:
        if (Turns == 0) return (t <= T2);
        else           return (t <= T2 * (8 + Turns) / 8);
    }
}


