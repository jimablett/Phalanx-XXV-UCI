#ifndef SKILL_LEVEL_H
#define SKILL_LEVEL_H

// Skill level configuration
#define MIN_SKILL_ELO 1000
#define MAX_SKILL_ELO 2700  // Engine's maximum strength
#define DEFAULT_SKILL_ELO 2700

// Skill levels (approximate)
typedef struct {
    int elo;              // ELO rating
    int depth_limit;      // Max search depth (in 100ths)
    int eval_noise;       // Noise to add to eval (-100 to 100 cp)
    float depth_factor;   // Multiplier for search depth (0.1 to 1.0)
    const char *description;
} tskill_profile;

extern int SkillLevel;              // Current skill level ELO (1000-2700)
extern int CurrentDepthLimit;       // Applied depth limit for this skill level
extern int CurrentEvalNoise;        // Applied eval noise for this skill level

// Function declarations
void init_skill_system(void);
void set_skill_level(int elo);
int get_skill_depth_limit(void);
int apply_skill_eval_noise(int eval);
int should_apply_skill_depth_limit(int current_depth);

#endif // SKILL_LEVEL_H
