#include "phalanx.h"
#include <math.h>

int SkillLevel = DEFAULT_SKILL_ELO;
int CurrentDepthLimit = 9999;
int CurrentEvalNoise = 0;

// Skill profiles mapped to approximate ELO levels
// These provide increasing depth limits and eval noise as ELO decreases
static const tskill_profile skill_profiles[] = {
    // ELO   DepthLimit  EvalNoise  DepthFactor  Description
    { 1000,     200,      80,       0.15f,      "Beginner (1000)" },
    { 1100,     220,      75,       0.17f,      "Beginner+ (1100)" },
    { 1200,     250,      70,       0.20f,      "Novice (1200)" },
    { 1300,     280,      60,       0.23f,      "Novice+ (1300)" },
    { 1400,     320,      50,       0.27f,      "Intermediate (1400)" },
    { 1500,     460,      40,       0.30f,      "Intermediate (1500)" },
    { 1600,     520,      30,       0.35f,      "Intermediate+ (1600)" },
    { 1700,     600,      25,       0.42f,      "Advanced (1700)" },
    { 1800,     700,      20,       0.50f,      "Advanced (1800)" },
    { 1900,     850,      15,       0.62f,      "Advanced+ (1900)" },
    { 2000,     920,      10,       0.77f,      "Expert (2000)" },
    { 2100,    1150,       7,       0.95f,      "Expert+ (2100)" },
    { 2200,    1400,       4,       1.10f,      "Master (2200)" },
    { 2300,    1700,       2,       1.25f,      "Master+ (2300)" },
    { 2400,    2100,       1,       1.40f,      "International Master (2400)" },
    { 2500,    2600,       0,       1.70f,      "Grandmaster (2500)" },
    { 2600,    3500,       0,       1.95f,      "Super-GM (2600)" },
    { 2700,    9999,       0,       1.00f,      "Maximum Strength (2700)" },
};

#define NUM_SKILL_PROFILES (sizeof(skill_profiles) / sizeof(skill_profiles[0]))

// Find the closest skill profile for a given ELO
static const tskill_profile* find_skill_profile(int elo) {
    if (elo >= 2700) return &skill_profiles[NUM_SKILL_PROFILES - 1];
    if (elo <= 1000) return &skill_profiles[0];
    
    // Linear interpolation between profiles
    for (int i = 0; i < NUM_SKILL_PROFILES - 1; i++) {
        if (elo >= skill_profiles[i].elo && elo < skill_profiles[i + 1].elo) {
            // Return the lower ELO profile (conservative approach)
            return &skill_profiles[i];
        }
    }
    return &skill_profiles[NUM_SKILL_PROFILES - 1];
}

void init_skill_system(void) {
    SkillLevel = DEFAULT_SKILL_ELO;
    set_skill_level(DEFAULT_SKILL_ELO);
}

void set_skill_level(int elo) {
    // Clamp ELO to valid range
    if (elo < MIN_SKILL_ELO) elo = MIN_SKILL_ELO;
    if (elo > MAX_SKILL_ELO) elo = MAX_SKILL_ELO;
    
    SkillLevel = elo;
    
    const tskill_profile *profile = find_skill_profile(elo);
    
    CurrentDepthLimit = profile->depth_limit;
    CurrentEvalNoise = profile->eval_noise;
    
    #ifdef DEBUG
    printf("info string Skill level set to %d ELO (%s)\n", elo, profile->description);
    printf("info string Depth limit: %d (100ths), Eval noise: %d cp\n", 
           CurrentDepthLimit, CurrentEvalNoise);
    #endif
    fflush(stdout);
}

int get_skill_depth_limit(void) {
    return CurrentDepthLimit;
}

// Apply skill-based noise to evaluation
// Creates apparent weaker play by adding random perturbations to evaluations
int apply_skill_eval_noise(int eval) {
    if (CurrentEvalNoise == 0) return eval;
    
    // Only apply noise to non-checkmate scores
    if (abs(eval) > CHECKMATE - MAXPLY) return eval;
    
    // Generate pseudo-random noise based on board hash and ply
    // This creates consistency while appearing stochastic
    static int last_ply = -1;
    static int noise_value = 0;
    
    if (Ply != last_ply) {
        last_ply = Ply;
        // Simple pseudo-random: mix hash with ply
        unsigned int seed = G[Counter].hashboard ^ (Ply * 31);
        seed = (seed * 1103515245 + 12345) & 0x7fffffff;
        noise_value = (seed % (2 * CurrentEvalNoise + 1)) - CurrentEvalNoise;
    }
    
    return eval + noise_value;
}

// Check if we should apply depth limit at current search depth
int should_apply_skill_depth_limit(int current_depth) {
    if (CurrentDepthLimit >= 9999) return 0;  // No limit
    return current_depth >= CurrentDepthLimit;
}

