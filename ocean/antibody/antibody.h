#ifndef ADIOS_H
#define ADIOS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "generated/antibody_generated.h"

#define ADIOS_NUM_AMINO_ACIDS 20
#define ADIOS_PAD_INDEX 20
#define ADIOS_ANTIBODY_LEN 11
#define ADIOS_ANTIGEN_LEN 97
#define ADIOS_MAX_INTERACTIONS 23
#define ADIOS_FULL_POSE_COUNT ADIOS_PRIMARY_SITES_ANTIBODY_ROWS
#define ADIOS_MED_POSE_COUNT ADIOS_MED_BIND_INDS_COUNT
#define ADIOS_TOP_POSE_COUNT ADIOS_TOP_BIND_INDS_COUNT

#define ADIOS_THREEFRY_C240 0x1BD11BDAu
#define ADIOS_THREEFRY_ROT_0 13u
#define ADIOS_THREEFRY_ROT_1 15u
#define ADIOS_THREEFRY_ROT_2 26u
#define ADIOS_THREEFRY_ROT_3 6u
#define ADIOS_THREEFRY_ROT_4 17u
#define ADIOS_THREEFRY_ROT_5 29u
#define ADIOS_THREEFRY_ROT_6 16u
#define ADIOS_THREEFRY_ROT_7 24u

typedef struct {
    uint32_t k0;
    uint32_t k1;
} AdiosKey;

typedef struct {
    float antigen_mut_rate;
    int antigen_pop_size;
    float antigen_selection_temperature;
} AdiosShapeParams;

typedef struct {
    const int32_t* indices;
    int count;
} AdiosPoseSet;

typedef struct {
    float min_value;
    int32_t argmin;
    float* binding_full_list;
    int count;
} AdiosBindingResult;

typedef struct {
    float binding_values_target;
    float binding_values_antitarget;
    int32_t poses_target;
    int32_t poses_antitarget;
} AdiosFitnessInfo;

typedef struct {
    int horizon;
    int seq_len;
    float* ag_performances;
    float binding_penalty;
    int32_t ab_t_m_pose_index;
    float* best_fitness;
    AdiosFitnessInfo* best_member_extra_info;
    int32_t* best_members;
} AdiosShapeRunResult;

typedef struct {
    int8_t* primary_sites_antibody;
    int8_t* secondary_sites_antibody;
    int8_t* secondary_sites_antigen;
    int32_t* med_bind_inds;

    int max_population_size;
    int max_sequence_length;
    int max_generations;

    int32_t* population_a;
    int32_t* population_b;
    float* fitness_values;
    float* selection_probs;
    AdiosFitnessInfo* fitness_info;
    float* binding_full_list;

    float* ag_performances;
    float* best_fitness_history;
    AdiosFitnessInfo* best_member_extra_history;
    int32_t* best_members_history;

    int32_t viral_target[ADIOS_ANTIBODY_LEN];
    int32_t antigen_array[ADIOS_ANTIGEN_LEN];
    int32_t antibody_antitarget_array[ADIOS_ANTIGEN_LEN];
    int32_t different_antibody_antitarget_array[ADIOS_ANTIGEN_LEN];

    float ag_target_binding;
    float ag_target_clip_min;

    AdiosPoseSet full_pose_set;
    AdiosPoseSet top_pose_set;
    AdiosPoseSet med_pose_set;
} Adios;

static const AdiosShapeParams ADIOS_DEFAULT_SHAPE_PARAMS = {
    .antigen_mut_rate = 1.0f,
    .antigen_pop_size = 15,
    .antigen_selection_temperature = 0.05f,
};

static inline uint32_t adios_rotl32(uint32_t value, uint32_t shift) {
    return (value << shift) | (value >> (32u - shift));
}

// JAX-compatible threefry2x32 block function.
static inline void adios_threefry2x32(
        AdiosKey key,
        uint32_t count0,
        uint32_t count1,
        uint32_t* out0,
        uint32_t* out1) {
    uint32_t ks0 = key.k0;
    uint32_t ks1 = key.k1;
    uint32_t ks2 = ADIOS_THREEFRY_C240 ^ ks0 ^ ks1;

    uint32_t x0 = count0 + ks0;
    uint32_t x1 = count1 + ks1;

    x0 += x1;
    x1 = adios_rotl32(x1, ADIOS_THREEFRY_ROT_0) ^ x0;
    x0 += x1;
    x1 = adios_rotl32(x1, ADIOS_THREEFRY_ROT_1) ^ x0;
    x0 += x1;
    x1 = adios_rotl32(x1, ADIOS_THREEFRY_ROT_2) ^ x0;
    x0 += x1;
    x1 = adios_rotl32(x1, ADIOS_THREEFRY_ROT_3) ^ x0;

    x0 += ks1;
    x1 += ks2 + 1u;

    x0 += x1;
    x1 = adios_rotl32(x1, ADIOS_THREEFRY_ROT_4) ^ x0;
    x0 += x1;
    x1 = adios_rotl32(x1, ADIOS_THREEFRY_ROT_5) ^ x0;
    x0 += x1;
    x1 = adios_rotl32(x1, ADIOS_THREEFRY_ROT_6) ^ x0;
    x0 += x1;
    x1 = adios_rotl32(x1, ADIOS_THREEFRY_ROT_7) ^ x0;

    x0 += ks2;
    x1 += ks0 + 2u;

    x0 += x1;
    x1 = adios_rotl32(x1, ADIOS_THREEFRY_ROT_0) ^ x0;
    x0 += x1;
    x1 = adios_rotl32(x1, ADIOS_THREEFRY_ROT_1) ^ x0;
    x0 += x1;
    x1 = adios_rotl32(x1, ADIOS_THREEFRY_ROT_2) ^ x0;
    x0 += x1;
    x1 = adios_rotl32(x1, ADIOS_THREEFRY_ROT_3) ^ x0;

    x0 += ks0;
    x1 += ks1 + 3u;

    x0 += x1;
    x1 = adios_rotl32(x1, ADIOS_THREEFRY_ROT_4) ^ x0;
    x0 += x1;
    x1 = adios_rotl32(x1, ADIOS_THREEFRY_ROT_5) ^ x0;
    x0 += x1;
    x1 = adios_rotl32(x1, ADIOS_THREEFRY_ROT_6) ^ x0;
    x0 += x1;
    x1 = adios_rotl32(x1, ADIOS_THREEFRY_ROT_7) ^ x0;

    x0 += ks1;
    x1 += ks2 + 4u;

    x0 += x1;
    x1 = adios_rotl32(x1, ADIOS_THREEFRY_ROT_0) ^ x0;
    x0 += x1;
    x1 = adios_rotl32(x1, ADIOS_THREEFRY_ROT_1) ^ x0;
    x0 += x1;
    x1 = adios_rotl32(x1, ADIOS_THREEFRY_ROT_2) ^ x0;
    x0 += x1;
    x1 = adios_rotl32(x1, ADIOS_THREEFRY_ROT_3) ^ x0;

    x0 += ks2;
    x1 += ks0 + 5u;

    *out0 = x0;
    *out1 = x1;
}

// Match jax.random.PRNGKey for 32-bit and 64-bit integer seeds.
static inline AdiosKey adios_prng_seed(uint64_t seed) {
    AdiosKey key;
    key.k0 = (uint32_t)(seed >> 32u);
    key.k1 = (uint32_t)(seed & 0xffffffffu);
    return key;
}

// Match jax.random.split(key, n)[i] for 1-D shapes.
static inline AdiosKey adios_prng_split_index(AdiosKey key, uint32_t index) {
    AdiosKey out;
    adios_threefry2x32(key, 0u, index, &out.k0, &out.k1);
    return out;
}

static inline void adios_prng_split_n(AdiosKey key, AdiosKey* out, int count) {
    for (int i = 0; i < count; i++) {
        out[i] = adios_prng_split_index(key, (uint32_t)i);
    }
}

static inline uint32_t adios_random_bits32_at(AdiosKey key, uint64_t index) {
    uint32_t hi = (uint32_t)(index >> 32u);
    uint32_t lo = (uint32_t)(index & 0xffffffffu);
    uint32_t out0 = 0u;
    uint32_t out1 = 0u;
    adios_threefry2x32(key, hi, lo, &out0, &out1);
    return out0 ^ out1;
}

// Match jax.random.uniform(key, shape, dtype=float32).
static inline float adios_uniform_f32_at(AdiosKey key, uint64_t index) {
    uint32_t bits = adios_random_bits32_at(key, index);
    uint32_t float_bits = (bits >> 9u) | 0x3f800000u;
    union {
        uint32_t u32;
        float f32;
    } value;
    value.u32 = float_bits;
    return value.f32 - 1.0f;
}

// Match jax.random.randint for 32-bit integer sampling.
static inline int32_t adios_randint_i32_at(AdiosKey key, uint64_t index, int32_t minval, int32_t maxval) {
    if (maxval <= minval) {
        return minval;
    }

    AdiosKey split_keys[2];
    adios_prng_split_n(key, split_keys, 2);

    uint32_t higher_bits = adios_random_bits32_at(split_keys[0], index);
    uint32_t lower_bits = adios_random_bits32_at(split_keys[1], index);
    uint32_t span = (uint32_t)(maxval - minval);
    if (span == 0u) {
        return minval;
    }

    uint32_t mod = 65536u % span;
    uint32_t multiplier = (uint32_t)(((uint64_t)mod * (uint64_t)mod) % span);
    uint32_t random_offset = (uint32_t)((((uint64_t)(higher_bits % span) * (uint64_t)multiplier)
        + (uint64_t)(lower_bits % span)) % span);
    return minval + (int32_t)random_offset;
}

static inline int adios_bernoulli_f32_at(AdiosKey key, uint64_t index, float p) {
    return adios_uniform_f32_at(key, index) < p;
}

static inline float adios_clipf(float value, float min_value, float max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static inline int32_t adios_choice_p(AdiosKey key, const float* probabilities, int count) {
    float total = 0.0f;
    for (int i = 0; i < count; i++) {
        total += probabilities[i];
    }

    float draw = total * (1.0f - adios_uniform_f32_at(key, 0u));
    float cumulative = 0.0f;
    for (int i = 0; i < count; i++) {
        cumulative += probabilities[i];
        if (draw <= cumulative) {
            return i;
        }
    }
    return count - 1;
}

static inline int adios_aminoacid_id(char aminoacid) {
    switch (aminoacid) {
        case 'C': return 0;
        case 'M': return 1;
        case 'F': return 2;
        case 'I': return 3;
        case 'L': return 4;
        case 'V': return 5;
        case 'W': return 6;
        case 'Y': return 7;
        case 'A': return 8;
        case 'G': return 9;
        case 'T': return 10;
        case 'S': return 11;
        case 'N': return 12;
        case 'Q': return 13;
        case 'D': return 14;
        case 'E': return 15;
        case 'R': return 16;
        case 'H': return 17;
        case 'K': return 18;
        case 'P': return 19;
        default: return -1;
    }
}

static inline char adios_aminoacid_char(int32_t aminoacid) {
    static const char lookup[ADIOS_NUM_AMINO_ACIDS] = {
        'C', 'M', 'F', 'I', 'L', 'V', 'W', 'Y', 'A', 'G',
        'T', 'S', 'N', 'Q', 'D', 'E', 'R', 'H', 'K', 'P',
    };
    if (aminoacid < 0 || aminoacid >= ADIOS_NUM_AMINO_ACIDS) {
        return '?';
    }
    return lookup[aminoacid];
}

// Convert a packed amino-acid string into integer ids.
static inline int adios_convert_aa_to_array(const char* sequence, int32_t* out, int max_len) {
    int length = (int)strlen(sequence);
    if (length > max_len) {
        return -1;
    }

    for (int i = 0; i < length; i++) {
        int aminoacid = adios_aminoacid_id(sequence[i]);
        if (aminoacid < 0) {
            return -1;
        }
        out[i] = aminoacid;
    }

    return length;
}

static inline void adios_convert_array_to_aa(const int32_t* sequence, int length, char* out) {
    for (int i = 0; i < length; i++) {
        out[i] = adios_aminoacid_char(sequence[i]);
    }
    out[length] = '\0';
}

static inline bool adios_load_file_exact(const char* path, void* dst, size_t bytes) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "adios: failed to open %s\n", path);
        return false;
    }

    size_t read_count = fread(dst, 1, bytes, file);
    fclose(file);
    if (read_count != bytes) {
        fprintf(stderr, "adios: short read for %s (%zu != %zu)\n", path, read_count, bytes);
        return false;
    }
    return true;
}

static inline bool adios_load_generated_data(Adios* adios) {
    size_t site_bytes = (size_t)ADIOS_PRIMARY_SITES_ANTIBODY_ROWS * (size_t)ADIOS_PRIMARY_SITES_ANTIBODY_COLS * sizeof(int8_t);
    size_t med_bytes = (size_t)ADIOS_MED_BIND_INDS_COUNT * sizeof(int32_t);

    adios->primary_sites_antibody = (int8_t*)malloc(site_bytes);
    adios->secondary_sites_antibody = (int8_t*)malloc(site_bytes);
    adios->secondary_sites_antigen = (int8_t*)malloc(site_bytes);
    adios->med_bind_inds = (int32_t*)malloc(med_bytes);

    if (adios->primary_sites_antibody == NULL
            || adios->secondary_sites_antibody == NULL
            || adios->secondary_sites_antigen == NULL
            || adios->med_bind_inds == NULL) {
        fprintf(stderr, "adios: failed to allocate generated data buffers\n");
        return false;
    }

    if (!adios_load_file_exact(ADIOS_PRIMARY_SITES_ANTIBODY_BIN, adios->primary_sites_antibody, site_bytes)) {
        return false;
    }
    if (!adios_load_file_exact(ADIOS_SECONDARY_SITES_ANTIBODY_BIN, adios->secondary_sites_antibody, site_bytes)) {
        return false;
    }
    if (!adios_load_file_exact(ADIOS_SECONDARY_SITES_ANTIGEN_BIN, adios->secondary_sites_antigen, site_bytes)) {
        return false;
    }
    if (!adios_load_file_exact(ADIOS_MED_BIND_INDS_BIN, adios->med_bind_inds, med_bytes)) {
        return false;
    }

    return true;
}

static inline bool adios_allocate_scratch(Adios* adios) {
    size_t max_population = (size_t)adios->max_population_size;
    size_t max_sequence_length = (size_t)adios->max_sequence_length;
    size_t max_generations = (size_t)adios->max_generations;

    adios->population_a = (int32_t*)calloc(max_population * max_sequence_length, sizeof(int32_t));
    adios->population_b = (int32_t*)calloc(max_population * max_sequence_length, sizeof(int32_t));
    adios->fitness_values = (float*)calloc(max_population, sizeof(float));
    adios->selection_probs = (float*)calloc(max_population, sizeof(float));
    adios->fitness_info = (AdiosFitnessInfo*)calloc(max_population, sizeof(AdiosFitnessInfo));
    adios->binding_full_list = (float*)calloc((size_t)ADIOS_FULL_POSE_COUNT, sizeof(float));

    adios->ag_performances = (float*)calloc(max_generations, sizeof(float));
    adios->best_fitness_history = (float*)calloc(max_generations, sizeof(float));
    adios->best_member_extra_history = (AdiosFitnessInfo*)calloc(max_generations, sizeof(AdiosFitnessInfo));
    adios->best_members_history = (int32_t*)calloc(max_generations * max_sequence_length, sizeof(int32_t));

    if (adios->population_a == NULL
            || adios->population_b == NULL
            || adios->fitness_values == NULL
            || adios->selection_probs == NULL
            || adios->fitness_info == NULL
            || adios->binding_full_list == NULL
            || adios->ag_performances == NULL
            || adios->best_fitness_history == NULL
            || adios->best_member_extra_history == NULL
            || adios->best_members_history == NULL) {
        fprintf(stderr, "adios: failed to allocate scratch buffers\n");
        return false;
    }

    return true;
}

static inline void adios_free(Adios* adios) {
    if (adios == NULL) {
        return;
    }

    free(adios->primary_sites_antibody);
    free(adios->secondary_sites_antibody);
    free(adios->secondary_sites_antigen);
    free(adios->med_bind_inds);

    free(adios->population_a);
    free(adios->population_b);
    free(adios->fitness_values);
    free(adios->selection_probs);
    free(adios->fitness_info);
    free(adios->binding_full_list);
    free(adios->ag_performances);
    free(adios->best_fitness_history);
    free(adios->best_member_extra_history);
    free(adios->best_members_history);

    free(adios);
}

static inline bool adios_init_defaults(Adios* adios) {
    if (adios_convert_aa_to_array("CARLVQLGLYY", adios->viral_target, ADIOS_ANTIBODY_LEN) != ADIOS_ANTIBODY_LEN) {
        return false;
    }
    if (adios_convert_aa_to_array(
            "SYSMCTGKFKVVKEIAETQHGTIVIRVQYEGDGSPCKIPFEIMDLEKRHVLGRLITVNPIVTEKDSPVNIEAEPPFGDSYIIIGVEPGQLKLNWFKK",
            adios->antigen_array,
            ADIOS_ANTIGEN_LEN) != ADIOS_ANTIGEN_LEN) {
        return false;
    }
    if (adios_convert_aa_to_array(
            "GRFLVNLQAKKDREAWYYWGPWNKAYWFSDPGMFDPWKQAEQSYFCNANPVCYAEHFMLGPITQKTPMVYHDPEPSKGGCVTVHNNATDYIMPDCYN",
            adios->antibody_antitarget_array,
            ADIOS_ANTIGEN_LEN) != ADIOS_ANTIGEN_LEN) {
        return false;
    }
    if (adios_convert_aa_to_array(
            "MADLEAVLADVSYLMAMEKSKATPAARASKKILLPEPSIRSVMQKYLEDRGEVTFEKIFSQKLGYLLFRDFCLNHLEEARPLVEFYEEIKKYEKLET",
            adios->different_antibody_antitarget_array,
            ADIOS_ANTIGEN_LEN) != ADIOS_ANTIGEN_LEN) {
        return false;
    }

    adios->full_pose_set.indices = NULL;
    adios->full_pose_set.count = ADIOS_FULL_POSE_COUNT;
    adios->top_pose_set.indices = ADIOS_TOP_BIND_INDS;
    adios->top_pose_set.count = ADIOS_TOP_POSE_COUNT;
    adios->med_pose_set.indices = adios->med_bind_inds;
    adios->med_pose_set.count = ADIOS_MED_POSE_COUNT;
    return true;
}

static inline Adios* adios_create(int max_population_size, int max_sequence_length, int max_generations) {
    Adios* adios = (Adios*)calloc(1, sizeof(Adios));
    if (adios == NULL) {
        return NULL;
    }

    adios->max_population_size = max_population_size;
    adios->max_sequence_length = max_sequence_length;
    adios->max_generations = max_generations;

    if (!adios_load_generated_data(adios)) {
        adios_free(adios);
        return NULL;
    }
    if (!adios_allocate_scratch(adios)) {
        adios_free(adios);
        return NULL;
    }
    if (!adios_init_defaults(adios)) {
        adios_free(adios);
        return NULL;
    }

    return adios;
}

static inline void adios_softmax(const float* values, int count, float temperature, float* out) {
    float max_value = values[0] / temperature;
    for (int i = 1; i < count; i++) {
        float value = values[i] / temperature;
        if (value > max_value) {
            max_value = value;
        }
    }

    float sum = 0.0f;
    for (int i = 0; i < count; i++) {
        out[i] = expf(values[i] / temperature - max_value);
        sum += out[i];
    }
    for (int i = 0; i < count; i++) {
        out[i] /= sum;
    }
}

static inline int adios_pose_row_index(const AdiosPoseSet* pose_set, int reduced_pose_index) {
    if (pose_set->indices == NULL) {
        return reduced_pose_index;
    }
    return pose_set->indices[reduced_pose_index];
}

static inline int32_t adios_sequence_value_at(const int32_t* sequence, int seq_len, int8_t index) {
    if (index < 0) {
        return ADIOS_PAD_INDEX;
    }
    if ((int)index >= seq_len) {
        return ADIOS_PAD_INDEX;
    }
    return sequence[(int)index];
}

// Compute the binding energy and reduced argmin for a pose set.
static inline void adios_bind(
        Adios* adios,
        const AdiosPoseSet* pose_set,
        const int32_t* antibody,
        int antibody_len,
        const int32_t* antigen,
        int antigen_len,
        float* binding_full_list,
        AdiosBindingResult* out) {
    if (antibody_len > antigen_len) {
        fprintf(stderr, "adios: antibody is longer than antigen\n");
        exit(1);
    }

    float min_value = 0.0f;
    int32_t argmin = 0;
    bool first = true;
    int32_t padded_antibody[ADIOS_ANTIBODY_LEN + 1];
    int32_t padded_antigen[ADIOS_ANTIGEN_LEN + 1];

    for (int i = 0; i <= ADIOS_ANTIBODY_LEN; i++) {
        padded_antibody[i] = ADIOS_PAD_INDEX;
    }
    for (int i = 0; i <= ADIOS_ANTIGEN_LEN; i++) {
        padded_antigen[i] = ADIOS_PAD_INDEX;
    }
    for (int i = 0; i < antibody_len; i++) {
        padded_antibody[i + 1] = antibody[i];
    }
    for (int i = 0; i < antigen_len; i++) {
        padded_antigen[i + 1] = antigen[i];
    }

    for (int pose = 0; pose < pose_set->count; pose++) {
        int row_index = adios_pose_row_index(pose_set, pose);
        int base = row_index * ADIOS_MAX_INTERACTIONS;
        float sum = 0.0f;

        for (int j = 0; j < ADIOS_MAX_INTERACTIONS; j++) {
            int8_t primary = adios->primary_sites_antibody[base + j];
            int8_t secondary_ab = adios->secondary_sites_antibody[base + j];
            int8_t secondary_ag = adios->secondary_sites_antigen[base + j];

            int32_t amino_primary = padded_antibody[(int)primary + 1];
            int32_t amino_secondary_ag = padded_antigen[(int)secondary_ag + 1];
            int32_t amino_secondary_ab = padded_antibody[(int)secondary_ab + 1];

            sum += ADIOS_JPADDED_MATRIX[amino_primary * ADIOS_JPADDED_MATRIX_COLS + amino_secondary_ag];
            sum += ADIOS_JPADDED_MATRIX[amino_primary * ADIOS_JPADDED_MATRIX_COLS + amino_secondary_ab];
        }

        if (binding_full_list != NULL) {
            binding_full_list[pose] = sum;
        }
        if (first || sum < min_value) {
            first = false;
            min_value = sum;
            argmin = pose;
        }
    }

    out->min_value = min_value;
    out->argmin = argmin;
    out->binding_full_list = binding_full_list;
    out->count = pose_set->count;
}

static inline float adios_antigen_fitness(
        Adios* adios,
        const AdiosPoseSet* pose_set,
        const int32_t* antibody_target,
        int antibody_target_len,
        const int32_t* antibody_antitarget,
        int antibody_antitarget_len,
        const int32_t* antigen,
        int antigen_len,
        float target_clip_min,
        float target_clip_max,
        float antitarget_clip_min,
        float antitarget_clip_max,
        bool give_poses,
        AdiosFitnessInfo* out) {
    AdiosBindingResult target_info = {0};
    adios_bind(adios, pose_set, antibody_target, antibody_target_len, antigen, antigen_len, NULL, &target_info);

    float binding_values_antitarget = 0.0f;
    int32_t poses_antitarget = -1;
    if (antibody_antitarget != NULL) {
        AdiosBindingResult antitarget_info = {0};
        adios_bind(adios, pose_set, antibody_antitarget, antibody_antitarget_len, antigen, antigen_len, NULL, &antitarget_info);
        binding_values_antitarget = antitarget_info.min_value;
        poses_antitarget = antitarget_info.argmin;
    }

    if (out != NULL) {
        out->binding_values_target = target_info.min_value;
        out->binding_values_antitarget = binding_values_antitarget;
        out->poses_target = give_poses ? target_info.argmin : -1;
        out->poses_antitarget = give_poses ? poses_antitarget : -1;
    }

    float clipped_target = adios_clipf(target_info.min_value, target_clip_min, target_clip_max);
    float clipped_antitarget = adios_clipf(binding_values_antitarget, antitarget_clip_min, antitarget_clip_max);
    return -1.0f * (clipped_target - clipped_antitarget);
}

static inline float adios_antibody_fitness(
        Adios* adios,
        const AdiosPoseSet* pose_set,
        const int32_t* antibody,
        int antibody_len,
        const int32_t* antigen_target,
        int antigen_target_len,
        const int32_t* antigen_antitarget,
        int antigen_antitarget_len,
        float target_clip_min,
        float target_clip_max,
        float antitarget_clip_min,
        float antitarget_clip_max,
        bool give_poses,
        AdiosFitnessInfo* out) {
    AdiosBindingResult target_info = {0};
    adios_bind(adios, pose_set, antibody, antibody_len, antigen_target, antigen_target_len, NULL, &target_info);

    float binding_values_antitarget = 0.0f;
    int32_t poses_antitarget = -1;
    if (antigen_antitarget != NULL) {
        AdiosBindingResult antitarget_info = {0};
        adios_bind(adios, pose_set, antibody, antibody_len, antigen_antitarget, antigen_antitarget_len, NULL, &antitarget_info);
        binding_values_antitarget = antitarget_info.min_value;
        poses_antitarget = antitarget_info.argmin;
    }

    if (out != NULL) {
        out->binding_values_target = target_info.min_value;
        out->binding_values_antitarget = binding_values_antitarget;
        out->poses_target = give_poses ? target_info.argmin : -1;
        out->poses_antitarget = give_poses ? poses_antitarget : -1;
    }

    float clipped_target = adios_clipf(target_info.min_value, target_clip_min, target_clip_max);
    float clipped_antitarget = adios_clipf(binding_values_antitarget, antitarget_clip_min, antitarget_clip_max);
    return -1.0f * (clipped_target - clipped_antitarget);
}

static inline void adios_antigen_fitness_population(
        Adios* adios,
        const AdiosPoseSet* pose_set,
        const int32_t* antibody_target,
        int antibody_target_len,
        const int32_t* antibody_antitarget,
        int antibody_antitarget_len,
        const int32_t* population,
        int population_size,
        int seq_len,
        float target_clip_min,
        float target_clip_max,
        float antitarget_clip_min,
        float antitarget_clip_max,
        bool give_poses,
        float* fitness_values,
        AdiosFitnessInfo* info_values) {
    for (int i = 0; i < population_size; i++) {
        const int32_t* antigen = &population[i * seq_len];
        fitness_values[i] = adios_antigen_fitness(
            adios,
            pose_set,
            antibody_target,
            antibody_target_len,
            antibody_antitarget,
            antibody_antitarget_len,
            antigen,
            seq_len,
            target_clip_min,
            target_clip_max,
            antitarget_clip_min,
            antitarget_clip_max,
            give_poses,
            &info_values[i]);
    }
}

// Mutate a population the same way as adios.gen_alg_basic.mutate.
static inline void adios_mutate(
        AdiosKey key,
        const int32_t* x,
        int rows,
        int cols,
        float p,
        int32_t* out) {
    AdiosKey split_keys[2];
    adios_prng_split_n(key, split_keys, 2);

    for (int i = 0; i < rows * cols; i++) {
        int mutation_mask = adios_bernoulli_f32_at(split_keys[0], (uint64_t)i, p);
        int32_t mutation_delta = adios_randint_i32_at(split_keys[1], (uint64_t)i, 1, 20);
        out[i] = (x[i] + mutation_mask * mutation_delta) % ADIOS_NUM_AMINO_ACIDS;
    }
}

static inline bool adios_prepare_population(Adios* adios, const int32_t* sequence, int seq_len, int population_size) {
    if (population_size > adios->max_population_size) {
        return false;
    }
    if (seq_len > adios->max_sequence_length) {
        return false;
    }

    for (int i = 0; i < population_size; i++) {
        memcpy(&adios->population_a[i * seq_len], sequence, (size_t)seq_len * sizeof(int32_t));
    }
    return true;
}

static inline void adios_single_iteration_antigen(
        Adios* adios,
        const AdiosPoseSet* pose_set,
        const int32_t* antibody_target,
        int antibody_target_len,
        const int32_t* antibody_antitarget,
        int antibody_antitarget_len,
        const int32_t* population,
        int population_size,
        int seq_len,
        float target_clip_min,
        float selection_temperature,
        float mutation_probability,
        AdiosKey key,
        int32_t* out_population,
        float* out_best_fitness,
        AdiosFitnessInfo* out_best_extra,
        int32_t* out_best_member) {
    AdiosKey split_keys[3];
    adios_prng_split_n(key, split_keys, 3);

    adios_antigen_fitness_population(
        adios,
        pose_set,
        antibody_target,
        antibody_target_len,
        antibody_antitarget,
        antibody_antitarget_len,
        population,
        population_size,
        seq_len,
        target_clip_min,
        INFINITY,
        -INFINITY,
        INFINITY,
        true,
        adios->fitness_values,
        adios->fitness_info);

    adios_softmax(adios->fitness_values, population_size, selection_temperature, adios->selection_probs);
    int32_t selected_member = adios_choice_p(split_keys[1], adios->selection_probs, population_size);
    const int32_t* selected = &population[selected_member * seq_len];

    if (out_best_fitness != NULL) {
        *out_best_fitness = adios->fitness_values[selected_member];
    }
    if (out_best_extra != NULL) {
        *out_best_extra = adios->fitness_info[selected_member];
    }
    if (out_best_member != NULL) {
        memcpy(out_best_member, selected, (size_t)seq_len * sizeof(int32_t));
    }

    for (int i = 0; i < population_size; i++) {
        memcpy(&out_population[i * seq_len], selected, (size_t)seq_len * sizeof(int32_t));
    }
    adios_mutate(split_keys[2], out_population, population_size, seq_len, mutation_probability, out_population);
}

static inline AdiosShapeRunResult adios_single_shape_run(
        Adios* adios,
        AdiosKey rng,
        const int32_t* antibody,
        const AdiosPoseSet* pose_set,
        const int32_t* start_antigen,
        int antigen_len,
        const int32_t* antigen_target,
        int antigen_target_len,
        const int32_t* antibody_antitarget,
        int antibody_antitarget_len,
        int horizon,
        AdiosShapeParams shape_params) {
    if (horizon > adios->max_generations) {
        fprintf(stderr, "adios: horizon exceeds scratch capacity\n");
        exit(1);
    }
    if (shape_params.antigen_pop_size > adios->max_population_size) {
        fprintf(stderr, "adios: population exceeds scratch capacity\n");
        exit(1);
    }
    if (antigen_len > adios->max_sequence_length) {
        fprintf(stderr, "adios: antigen length exceeds scratch capacity\n");
        exit(1);
    }

    float target_clip_min = -INFINITY;
    if (antigen_target != NULL) {
        AdiosBindingResult target_binding = {0};
        adios_bind(adios, pose_set, antigen_target, antigen_target_len, start_antigen, antigen_len, NULL, &target_binding);
        target_clip_min = target_binding.min_value - 1.0f;
    }

    if (!adios_prepare_population(adios, start_antigen, antigen_len, shape_params.antigen_pop_size)) {
        fprintf(stderr, "adios: failed to prepare initial population\n");
        exit(1);
    }

    float mutation_probability = shape_params.antigen_mut_rate / (float)antigen_len;
    AdiosKey* keys = (AdiosKey*)malloc((size_t)horizon * sizeof(AdiosKey));
    if (keys == NULL) {
        fprintf(stderr, "adios: failed to allocate key list\n");
        exit(1);
    }
    adios_prng_split_n(rng, keys, horizon);

    int32_t* current_population = adios->population_a;
    int32_t* next_population = adios->population_b;
    for (int step = 0; step < horizon; step++) {
        adios_single_iteration_antigen(
            adios,
            pose_set,
            antigen_target,
            antigen_target_len,
            antibody,
            ADIOS_ANTIBODY_LEN,
            current_population,
            shape_params.antigen_pop_size,
            antigen_len,
            target_clip_min,
            shape_params.antigen_selection_temperature,
            mutation_probability,
            keys[step],
            next_population,
            &adios->best_fitness_history[step],
            &adios->best_member_extra_history[step],
            &adios->best_members_history[step * antigen_len]);

        int32_t* swap = current_population;
        current_population = next_population;
        next_population = swap;
    }
    free(keys);

    float binding_penalty = 0.0f;
    int32_t bind_index = -1;
    if (antibody_antitarget != NULL) {
        AdiosBindingResult bind_details = {0};
        adios_bind(adios, pose_set, antibody, ADIOS_ANTIBODY_LEN, antibody_antitarget, antibody_antitarget_len, NULL, &bind_details);
        binding_penalty = bind_details.min_value;
        bind_index = bind_details.argmin;
    }

    for (int i = 0; i < horizon; i++) {
        adios->ag_performances[i] = adios->best_fitness_history[i] - binding_penalty;
    }

    AdiosShapeRunResult result;
    result.horizon = horizon;
    result.seq_len = antigen_len;
    result.ag_performances = adios->ag_performances;
    result.binding_penalty = binding_penalty;
    result.ab_t_m_pose_index = bind_index;
    result.best_fitness = adios->best_fitness_history;
    result.best_member_extra_info = adios->best_member_extra_history;
    result.best_members = adios->best_members_history;
    return result;
}

static inline void adios_init_reference_values(Adios* adios) {
    AdiosBindingResult binding = {0};
    adios_bind(
        adios,
        &adios->full_pose_set,
        adios->viral_target,
        ADIOS_ANTIBODY_LEN,
        adios->antigen_array,
        ADIOS_ANTIGEN_LEN,
        NULL,
        &binding);
    adios->ag_target_binding = binding.min_value;
    adios->ag_target_clip_min = binding.min_value - 1.0f;
}

#endif
