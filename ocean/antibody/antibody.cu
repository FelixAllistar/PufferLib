#define _POSIX_C_SOURCE 200809L

#include <cuda_runtime.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <time.h>

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

    int32_t viral_target[ADIOS_ANTIBODY_LEN];
    int32_t antigen_array[ADIOS_ANTIGEN_LEN];
    int32_t antibody_antitarget_array[ADIOS_ANTIGEN_LEN];

    AdiosPoseSet full_pose_set;
    AdiosPoseSet top_pose_set;
    AdiosPoseSet med_pose_set;
} HostData;

static const AdiosShapeParams ADIOS_DEFAULT_SHAPE_PARAMS = {
    .antigen_mut_rate = 1.0f,
    .antigen_pop_size = 15,
    .antigen_selection_temperature = 0.05f,
};

static inline uint32_t adios_rotl32(uint32_t value, uint32_t shift) {
    return (value << shift) | (value >> (32u - shift));
}

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

static inline AdiosKey adios_prng_seed(uint64_t seed) {
    AdiosKey key;
    key.k0 = (uint32_t)(seed >> 32u);
    key.k1 = (uint32_t)(seed & 0xffffffffu);
    return key;
}

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

static inline bool load_file_exact(const char* path, void* dst, size_t bytes) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "antibody: failed to open %s\n", path);
        return false;
    }

    size_t read_count = fread(dst, 1, bytes, file);
    fclose(file);
    if (read_count != bytes) {
        fprintf(stderr, "antibody: short read for %s (%zu != %zu)\n", path, read_count, bytes);
        return false;
    }
    return true;
}

static void free_data(HostData* data) {
    if (data == NULL) {
        return;
    }

    free(data->primary_sites_antibody);
    free(data->secondary_sites_antibody);
    free(data->secondary_sites_antigen);
    free(data->med_bind_inds);
    free(data);
}

static bool load_generated_data(HostData* data) {
    size_t site_bytes = (size_t)ADIOS_PRIMARY_SITES_ANTIBODY_ROWS
        * (size_t)ADIOS_PRIMARY_SITES_ANTIBODY_COLS * sizeof(int8_t);
    size_t med_bytes = (size_t)ADIOS_MED_BIND_INDS_COUNT * sizeof(int32_t);

    data->primary_sites_antibody = (int8_t*)malloc(site_bytes);
    data->secondary_sites_antibody = (int8_t*)malloc(site_bytes);
    data->secondary_sites_antigen = (int8_t*)malloc(site_bytes);
    data->med_bind_inds = (int32_t*)malloc(med_bytes);
    if (data->primary_sites_antibody == NULL
            || data->secondary_sites_antibody == NULL
            || data->secondary_sites_antigen == NULL
            || data->med_bind_inds == NULL) {
        fprintf(stderr, "antibody: failed to allocate generated data buffers\n");
        return false;
    }

    if (!load_file_exact(ADIOS_PRIMARY_SITES_ANTIBODY_BIN, data->primary_sites_antibody, site_bytes)
            || !load_file_exact(ADIOS_SECONDARY_SITES_ANTIBODY_BIN, data->secondary_sites_antibody, site_bytes)
            || !load_file_exact(ADIOS_SECONDARY_SITES_ANTIGEN_BIN, data->secondary_sites_antigen, site_bytes)
            || !load_file_exact(ADIOS_MED_BIND_INDS_BIN, data->med_bind_inds, med_bytes)) {
        return false;
    }

    return true;
}

static inline int pose_row_index(const AdiosPoseSet* pose_set, int reduced_pose_index) {
    if (pose_set->indices == NULL) {
        return reduced_pose_index;
    }
    return pose_set->indices[reduced_pose_index];
}

static bool init_defaults(HostData* data) {
    if (adios_convert_aa_to_array("CARLVQLGLYY", data->viral_target, ADIOS_ANTIBODY_LEN) != ADIOS_ANTIBODY_LEN) {
        return false;
    }
    if (adios_convert_aa_to_array(
            "SYSMCTGKFKVVKEIAETQHGTIVIRVQYEGDGSPCKIPFEIMDLEKRHVLGRLITVNPIVTEKDSPVNIEAEPPFGDSYIIIGVEPGQLKLNWFKK",
            data->antigen_array,
            ADIOS_ANTIGEN_LEN) != ADIOS_ANTIGEN_LEN) {
        return false;
    }
    if (adios_convert_aa_to_array(
            "GRFLVNLQAKKDREAWYYWGPWNKAYWFSDPGMFDPWKQAEQSYFCNANPVCYAEHFMLGPITQKTPMVYHDPEPSKGGCVTVHNNATDYIMPDCYN",
            data->antibody_antitarget_array,
            ADIOS_ANTIGEN_LEN) != ADIOS_ANTIGEN_LEN) {
        return false;
    }

    data->full_pose_set.indices = NULL;
    data->full_pose_set.count = ADIOS_FULL_POSE_COUNT;
    data->top_pose_set.indices = ADIOS_TOP_BIND_INDS;
    data->top_pose_set.count = ADIOS_TOP_POSE_COUNT;
    data->med_pose_set.indices = data->med_bind_inds;
    data->med_pose_set.count = ADIOS_MED_POSE_COUNT;
    return true;
}

static HostData* make_data(void) {
    HostData* data = (HostData*)calloc(1, sizeof(HostData));
    if (data == NULL) {
        return NULL;
    }
    if (!load_generated_data(data) || !init_defaults(data)) {
        free_data(data);
        return NULL;
    }
    return data;
}

#define BIND_THREADS 256
#define SMALL_WARPS 4
#define SMALL_THREADS (SMALL_WARPS * 32)
#define MUTATE_THREADS 256
#define PADDED_ANTIBODY_LEN (ADIOS_ANTIBODY_LEN + 1)
#define PADDED_ANTIGEN_LEN (ADIOS_ANTIGEN_LEN + 1)
#define SANITY_QUERIES 8

#ifndef CUDART_INF_F
#define CUDART_INF_F __int_as_float(0x7f800000)
#endif

#define CHECK(expr) do { \
    cudaError_t err__ = (expr); \
    if (err__ != cudaSuccess) { \
        fprintf(stderr, "CUDA %s:%d: %s\n", __FILE__, __LINE__, cudaGetErrorString(err__)); \
        exit(1); \
    } \
} while (0)

typedef struct {
    int count;
    int8_t* primary_sites_antibody;
    int8_t* secondary_sites_antibody;
    int8_t* secondary_sites_antigen;
} PoseSet;

typedef struct {
    uint32_t split[6];
    float uniform[4];
    int32_t randint[8];
    int32_t bernoulli[8];
    int32_t choice;
} RngTestOutput;

typedef struct {
    HostData* data;
    int max_population_size;
    int max_generations;

    PoseSet full_pose_set;
    PoseSet top_pose_set;
    PoseSet med_pose_set;

    int8_t* population_a;
    float* target_binding_values;
    int32_t* target_pose_indices;
    float* antitarget_binding_values;
    int32_t* antitarget_pose_indices;
    float* fitness_values;
    float* selection_probs;
    AdiosFitnessInfo* fitness_info;
    float* best_fitness_history;
    AdiosFitnessInfo* best_member_extra_history;
    int32_t* best_members_history;

    float* host_ag_performances;
    float* host_best_fitness_history;
    AdiosFitnessInfo* host_best_member_extra_history;
    int32_t* host_best_members_history;

    int8_t* padded_antibody;
    int8_t* padded_target;
    int8_t* single_antigen;
    float* single_binding_value;
    int32_t* single_binding_index;
} State;

__constant__ float JPADDED_MATRIX_DEVICE[ADIOS_JPADDED_MATRIX_ROWS * ADIOS_JPADDED_MATRIX_COLS];

static void free_pose_set(PoseSet* pose_set) {
    if (pose_set == NULL) {
        return;
    }

    CHECK(cudaFree(pose_set->primary_sites_antibody));
    CHECK(cudaFree(pose_set->secondary_sites_antibody));
    CHECK(cudaFree(pose_set->secondary_sites_antigen));
    pose_set->primary_sites_antibody = NULL;
    pose_set->secondary_sites_antibody = NULL;
    pose_set->secondary_sites_antigen = NULL;
}

static void upload_pose_set(const HostData* data, const AdiosPoseSet* host_pose_set, PoseSet* device_pose_set) {
    int pose;
    int total = host_pose_set->count * ADIOS_MAX_INTERACTIONS;
    size_t bytes = (size_t)total * sizeof(int8_t);
    int8_t* primary = (int8_t*)malloc(bytes);
    int8_t* secondary_ab = (int8_t*)malloc(bytes);
    int8_t* secondary_ag = (int8_t*)malloc(bytes);

    if (primary == NULL || secondary_ab == NULL || secondary_ag == NULL) {
        fprintf(stderr, "failed to allocate packed pose set\n");
        exit(1);
    }

    for (pose = 0; pose < host_pose_set->count; pose++) {
        int row_index = pose_row_index(host_pose_set, pose);
        int src_base = row_index * ADIOS_MAX_INTERACTIONS;
        int dst_base = pose * ADIOS_MAX_INTERACTIONS;
        memcpy(&primary[dst_base], &data->primary_sites_antibody[src_base], ADIOS_MAX_INTERACTIONS * sizeof(int8_t));
        memcpy(&secondary_ab[dst_base], &data->secondary_sites_antibody[src_base], ADIOS_MAX_INTERACTIONS * sizeof(int8_t));
        memcpy(&secondary_ag[dst_base], &data->secondary_sites_antigen[src_base], ADIOS_MAX_INTERACTIONS * sizeof(int8_t));
    }

    device_pose_set->count = host_pose_set->count;
    CHECK(cudaMalloc((void**)&device_pose_set->primary_sites_antibody, bytes));
    CHECK(cudaMalloc((void**)&device_pose_set->secondary_sites_antibody, bytes));
    CHECK(cudaMalloc((void**)&device_pose_set->secondary_sites_antigen, bytes));
    CHECK(cudaMemcpy(device_pose_set->primary_sites_antibody, primary, bytes, cudaMemcpyHostToDevice));
    CHECK(cudaMemcpy(device_pose_set->secondary_sites_antibody, secondary_ab, bytes, cudaMemcpyHostToDevice));
    CHECK(cudaMemcpy(device_pose_set->secondary_sites_antigen, secondary_ag, bytes, cudaMemcpyHostToDevice));

    free(primary);
    free(secondary_ab);
    free(secondary_ag);
}

void make_padded_sequence(const int32_t* sequence, int seq_len, int padded_len, int8_t* out) {
    int i;
    for (i = 0; i < padded_len; i++) {
        out[i] = (int8_t)ADIOS_PAD_INDEX;
    }
    for (i = 0; i < seq_len; i++) {
        out[i + 1] = (int8_t)sequence[i];
    }
}

void fill_population_from_sequence(int8_t* dst, int population_size, const int32_t* sequence, int seq_len) {
    int member;
    for (member = 0; member < population_size; member++) {
        make_padded_sequence(sequence, seq_len,
            PADDED_ANTIGEN_LEN, &dst[(size_t)member * PADDED_ANTIGEN_LEN]);
    }
}

void free_state(State* state) {
    if (state == NULL) {
        return;
    }

    free_pose_set(&state->full_pose_set);
    free_pose_set(&state->top_pose_set);
    free_pose_set(&state->med_pose_set);

    CHECK(cudaFree(state->population_a));
    CHECK(cudaFree(state->target_binding_values));
    CHECK(cudaFree(state->target_pose_indices));
    CHECK(cudaFree(state->antitarget_binding_values));
    CHECK(cudaFree(state->antitarget_pose_indices));
    CHECK(cudaFree(state->fitness_values));
    CHECK(cudaFree(state->selection_probs));
    CHECK(cudaFree(state->fitness_info));
    CHECK(cudaFree(state->best_fitness_history));
    CHECK(cudaFree(state->best_member_extra_history));
    CHECK(cudaFree(state->best_members_history));
    CHECK(cudaFree(state->padded_antibody));
    CHECK(cudaFree(state->padded_target));
    CHECK(cudaFree(state->single_antigen));
    CHECK(cudaFree(state->single_binding_value));
    CHECK(cudaFree(state->single_binding_index));

    free(state->host_ag_performances);
    free(state->host_best_fitness_history);
    free(state->host_best_member_extra_history);
    free(state->host_best_members_history);
    free_data(state->data);
    free(state);
}

State* make_state(int max_population_size, int max_generations) {
    State* state = (State*)calloc(1, sizeof(State));
    size_t population_bytes;
    size_t history_member_bytes;
    if (state == NULL) {
        return NULL;
    }

    state->data = make_data();
    if (state->data == NULL) {
        free(state);
        return NULL;
    }
    state->max_population_size = max_population_size;
    state->max_generations = max_generations;

    state->host_ag_performances = (float*)calloc((size_t)max_generations, sizeof(float));
    state->host_best_fitness_history = (float*)calloc((size_t)max_generations, sizeof(float));
    state->host_best_member_extra_history = (AdiosFitnessInfo*)calloc((size_t)max_generations, sizeof(AdiosFitnessInfo));
    state->host_best_members_history = (int32_t*)calloc((size_t)max_generations * ADIOS_ANTIGEN_LEN, sizeof(int32_t));
    if (state->host_ag_performances == NULL
            || state->host_best_fitness_history == NULL
            || state->host_best_member_extra_history == NULL
            || state->host_best_members_history == NULL) {
        free_state(state);
        return NULL;
    }

    CHECK(cudaMemcpyToSymbol(JPADDED_MATRIX_DEVICE, ADIOS_JPADDED_MATRIX, sizeof(ADIOS_JPADDED_MATRIX)));

    upload_pose_set(state->data, &state->data->full_pose_set, &state->full_pose_set);
    upload_pose_set(state->data, &state->data->top_pose_set, &state->top_pose_set);
    upload_pose_set(state->data, &state->data->med_pose_set, &state->med_pose_set);

    population_bytes = (size_t)max_population_size * PADDED_ANTIGEN_LEN * sizeof(int8_t);
    history_member_bytes = (size_t)max_generations * ADIOS_ANTIGEN_LEN * sizeof(int32_t);

    CHECK(cudaMalloc((void**)&state->population_a, population_bytes));
    CHECK(cudaMalloc((void**)&state->target_binding_values, (size_t)max_population_size * sizeof(float)));
    CHECK(cudaMalloc((void**)&state->target_pose_indices, (size_t)max_population_size * sizeof(int32_t)));
    CHECK(cudaMalloc((void**)&state->antitarget_binding_values, (size_t)max_population_size * sizeof(float)));
    CHECK(cudaMalloc((void**)&state->antitarget_pose_indices, (size_t)max_population_size * sizeof(int32_t)));
    CHECK(cudaMalloc((void**)&state->fitness_values, (size_t)max_population_size * sizeof(float)));
    CHECK(cudaMalloc((void**)&state->selection_probs, (size_t)max_population_size * sizeof(float)));
    CHECK(cudaMalloc((void**)&state->fitness_info, (size_t)max_population_size * sizeof(AdiosFitnessInfo)));
    CHECK(cudaMalloc((void**)&state->best_fitness_history, (size_t)max_generations * sizeof(float)));
    CHECK(cudaMalloc((void**)&state->best_member_extra_history, (size_t)max_generations * sizeof(AdiosFitnessInfo)));
    CHECK(cudaMalloc((void**)&state->best_members_history, history_member_bytes));
    CHECK(cudaMalloc((void**)&state->padded_antibody, PADDED_ANTIBODY_LEN * sizeof(int8_t)));
    CHECK(cudaMalloc((void**)&state->padded_target, PADDED_ANTIBODY_LEN * sizeof(int8_t)));
    CHECK(cudaMalloc((void**)&state->single_antigen, PADDED_ANTIGEN_LEN * sizeof(int8_t)));
    CHECK(cudaMalloc((void**)&state->single_binding_value, sizeof(float)));
    CHECK(cudaMalloc((void**)&state->single_binding_index, sizeof(int32_t)));

    return state;
}

__device__ __forceinline__ uint32_t rotl32(uint32_t value, uint32_t shift) {
    return (value << shift) | (value >> (32u - shift));
}

__device__ __forceinline__ AdiosKey prng_seed(uint64_t seed) {
    AdiosKey key;
    key.k0 = (uint32_t)(seed >> 32u);
    key.k1 = (uint32_t)(seed & 0xffffffffu);
    return key;
}

__device__ __forceinline__ void threefry2x32(
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
    x1 = rotl32(x1, ADIOS_THREEFRY_ROT_0) ^ x0;
    x0 += x1;
    x1 = rotl32(x1, ADIOS_THREEFRY_ROT_1) ^ x0;
    x0 += x1;
    x1 = rotl32(x1, ADIOS_THREEFRY_ROT_2) ^ x0;
    x0 += x1;
    x1 = rotl32(x1, ADIOS_THREEFRY_ROT_3) ^ x0;

    x0 += ks1;
    x1 += ks2 + 1u;

    x0 += x1;
    x1 = rotl32(x1, ADIOS_THREEFRY_ROT_4) ^ x0;
    x0 += x1;
    x1 = rotl32(x1, ADIOS_THREEFRY_ROT_5) ^ x0;
    x0 += x1;
    x1 = rotl32(x1, ADIOS_THREEFRY_ROT_6) ^ x0;
    x0 += x1;
    x1 = rotl32(x1, ADIOS_THREEFRY_ROT_7) ^ x0;

    x0 += ks2;
    x1 += ks0 + 2u;

    x0 += x1;
    x1 = rotl32(x1, ADIOS_THREEFRY_ROT_0) ^ x0;
    x0 += x1;
    x1 = rotl32(x1, ADIOS_THREEFRY_ROT_1) ^ x0;
    x0 += x1;
    x1 = rotl32(x1, ADIOS_THREEFRY_ROT_2) ^ x0;
    x0 += x1;
    x1 = rotl32(x1, ADIOS_THREEFRY_ROT_3) ^ x0;

    x0 += ks0;
    x1 += ks1 + 3u;

    x0 += x1;
    x1 = rotl32(x1, ADIOS_THREEFRY_ROT_4) ^ x0;
    x0 += x1;
    x1 = rotl32(x1, ADIOS_THREEFRY_ROT_5) ^ x0;
    x0 += x1;
    x1 = rotl32(x1, ADIOS_THREEFRY_ROT_6) ^ x0;
    x0 += x1;
    x1 = rotl32(x1, ADIOS_THREEFRY_ROT_7) ^ x0;

    x0 += ks1;
    x1 += ks2 + 4u;

    x0 += x1;
    x1 = rotl32(x1, ADIOS_THREEFRY_ROT_0) ^ x0;
    x0 += x1;
    x1 = rotl32(x1, ADIOS_THREEFRY_ROT_1) ^ x0;
    x0 += x1;
    x1 = rotl32(x1, ADIOS_THREEFRY_ROT_2) ^ x0;
    x0 += x1;
    x1 = rotl32(x1, ADIOS_THREEFRY_ROT_3) ^ x0;

    x0 += ks2;
    x1 += ks0 + 5u;

    *out0 = x0;
    *out1 = x1;
}

__device__ __forceinline__ AdiosKey prng_split_index(AdiosKey key, uint32_t index) {
    AdiosKey out;
    threefry2x32(key, 0u, index, &out.k0, &out.k1);
    return out;
}

__device__ __forceinline__ void prng_split_n(AdiosKey key, AdiosKey* out, int count) {
    int i;
    for (i = 0; i < count; i++) {
        out[i] = prng_split_index(key, (uint32_t)i);
    }
}

__device__ __forceinline__ uint32_t random_bits32_at(AdiosKey key, uint64_t index) {
    uint32_t hi = (uint32_t)(index >> 32u);
    uint32_t lo = (uint32_t)(index & 0xffffffffu);
    uint32_t out0 = 0u;
    uint32_t out1 = 0u;
    threefry2x32(key, hi, lo, &out0, &out1);
    return out0 ^ out1;
}

__device__ __forceinline__ float uniform_f32_at(AdiosKey key, uint64_t index) {
    uint32_t bits = random_bits32_at(key, index);
    uint32_t float_bits_u32 = (bits >> 9u) | 0x3f800000u;
    union {
        uint32_t u32;
        float f32;
    } value;
    value.u32 = float_bits_u32;
    return value.f32 - 1.0f;
}

__device__ __forceinline__ int32_t randint_i32_at(AdiosKey key, uint64_t index, int32_t minval, int32_t maxval) {
    AdiosKey split_keys[2];
    uint32_t higher_bits;
    uint32_t lower_bits;
    uint32_t span;
    uint32_t mod;
    uint32_t multiplier;
    uint32_t random_offset;

    if (maxval <= minval) {
        return minval;
    }

    prng_split_n(key, split_keys, 2);
    higher_bits = random_bits32_at(split_keys[0], index);
    lower_bits = random_bits32_at(split_keys[1], index);
    span = (uint32_t)(maxval - minval);
    if (span == 0u) {
        return minval;
    }

    mod = 65536u % span;
    multiplier = (uint32_t)(((uint64_t)mod * (uint64_t)mod) % span);
    random_offset = (uint32_t)((((uint64_t)(higher_bits % span) * (uint64_t)multiplier)
        + (uint64_t)(lower_bits % span)) % span);
    return minval + (int32_t)random_offset;
}

__device__ __forceinline__ int bernoulli_f32_at(AdiosKey key, uint64_t index, float p) {
    return uniform_f32_at(key, index) < p;
}

__device__ __forceinline__ float clip_value(float value, float min_value, float max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

__device__ __forceinline__ int32_t choose_index(AdiosKey key, const float* probabilities, int count) {
    float total = 0.0f;
    float draw;
    float cumulative = 0.0f;
    int i;

    for (i = 0; i < count; i++) {
        total += probabilities[i];
    }

    draw = total * (1.0f - uniform_f32_at(key, 0u));
    for (i = 0; i < count; i++) {
        cumulative += probabilities[i];
        if (draw <= cumulative) {
            return i;
        }
    }
    return count - 1;
}

__device__ __forceinline__ int better_result(float candidate_value,
        int32_t candidate_index, float best_value, int32_t best_index) {
    return (candidate_value < best_value
        || (candidate_value == best_value && candidate_index >= 0 && (best_index < 0 || candidate_index < best_index)));
}

__global__ void rng_test(RngTestOutput* out) {
    AdiosKey key = prng_seed(0u);
    AdiosKey split_keys[3];
    float probs[4] = {0.1f, 0.2f, 0.3f, 0.4f};
    int i;

    if (blockIdx.x != 0 || threadIdx.x != 0) {
        return;
    }

    prng_split_n(key, split_keys, 3);
    for (i = 0; i < 3; i++) {
        out->split[i * 2 + 0] = split_keys[i].k0;
        out->split[i * 2 + 1] = split_keys[i].k1;
    }
    for (i = 0; i < 4; i++) {
        out->uniform[i] = uniform_f32_at(key, (uint64_t)i);
    }
    for (i = 0; i < 8; i++) {
        out->randint[i] = randint_i32_at(key, (uint64_t)i, 0, 20);
        out->bernoulli[i] = bernoulli_f32_at(key, (uint64_t)i, 0.25f);
    }
    out->choice = choose_index(key, probs, 4);
}

__global__ void bind_block(const int8_t* primary_sites_antibody,
        const int8_t* secondary_sites_antibody,
        const int8_t* secondary_sites_antigen, int pose_count,
        const int8_t* padded_antibody, const int8_t* padded_antigens,
        int antigen_stride, float* out_min, int32_t* out_argmin) {
    __shared__ int8_t sh_antibody[PADDED_ANTIBODY_LEN];
    __shared__ int8_t sh_antigen[PADDED_ANTIGEN_LEN];
    __shared__ float sh_best_values[BIND_THREADS];
    __shared__ int32_t sh_best_indices[BIND_THREADS];
    int query = blockIdx.x;
    int tid = threadIdx.x;
    int pose;

    if (tid < PADDED_ANTIBODY_LEN) {
        sh_antibody[tid] = padded_antibody[tid];
    }
    if (tid < PADDED_ANTIGEN_LEN) {
        sh_antigen[tid] = padded_antigens[(size_t)query * antigen_stride + tid];
    }
    __syncthreads();

    sh_best_values[tid] = CUDART_INF_F;
    sh_best_indices[tid] = -1;

    for (pose = tid; pose < pose_count; pose += blockDim.x) {
        int base = pose * ADIOS_MAX_INTERACTIONS;
        float sum = 0.0f;
        int j;

        for (j = 0; j < ADIOS_MAX_INTERACTIONS; j++) {
            int primary_slot = (int)primary_sites_antibody[base + j] + 1;
            int secondary_ab_slot = (int)secondary_sites_antibody[base + j] + 1;
            int secondary_ag_slot = (int)secondary_sites_antigen[base + j] + 1;
            int amino_primary = (int)sh_antibody[primary_slot];
            int amino_secondary_ab = (int)sh_antibody[secondary_ab_slot];
            int amino_secondary_ag = (int)sh_antigen[secondary_ag_slot];
            int row_base = amino_primary * ADIOS_JPADDED_MATRIX_COLS;
            sum += JPADDED_MATRIX_DEVICE[row_base + amino_secondary_ag];
            sum += JPADDED_MATRIX_DEVICE[row_base + amino_secondary_ab];
        }

        if (better_result(sum, pose, sh_best_values[tid], sh_best_indices[tid])) {
            sh_best_values[tid] = sum;
            sh_best_indices[tid] = pose;
        }
    }
    __syncthreads();

    for (pose = blockDim.x / 2; pose > 0; pose >>= 1) {
        if (tid < pose) {
            float other_value = sh_best_values[tid + pose];
            int32_t other_index = sh_best_indices[tid + pose];
            if (better_result(other_value, other_index, sh_best_values[tid], sh_best_indices[tid])) {
                sh_best_values[tid] = other_value;
                sh_best_indices[tid] = other_index;
            }
        }
        __syncthreads();
    }

    if (tid == 0) {
        out_min[query] = sh_best_values[0];
        out_argmin[query] = sh_best_indices[0];
    }
}

__global__ void bind_small(const int8_t* primary_sites_antibody,
        const int8_t* secondary_sites_antibody,
        const int8_t* secondary_sites_antigen, int pose_count,
        const int8_t* padded_antibody, const int8_t* padded_antigens,
        int antigen_stride, int pool_size, float* out_min,
        int32_t* out_argmin) {
    __shared__ int8_t sh_antibody[PADDED_ANTIBODY_LEN];
    __shared__ int8_t sh_antigens[SMALL_WARPS][PADDED_ANTIGEN_LEN];
    int warp = threadIdx.x >> 5;
    int lane = threadIdx.x & 31;
    int query = blockIdx.x * SMALL_WARPS + warp;
    int idx;
    int pose;
    float best_value;
    int32_t best_index;

    if (threadIdx.x < PADDED_ANTIBODY_LEN) {
        sh_antibody[threadIdx.x] = padded_antibody[threadIdx.x];
    }
    for (idx = threadIdx.x; idx < SMALL_WARPS * PADDED_ANTIGEN_LEN; idx += blockDim.x) {
        int antigen_warp = idx / PADDED_ANTIGEN_LEN;
        int antigen_slot = idx % PADDED_ANTIGEN_LEN;
        int antigen_query = blockIdx.x * SMALL_WARPS + antigen_warp;
        if (antigen_query < pool_size) {
            sh_antigens[antigen_warp][antigen_slot] = padded_antigens[(size_t)antigen_query * antigen_stride + antigen_slot];
        }
    }
    __syncthreads();

    if (query >= pool_size) {
        return;
    }

    best_value = CUDART_INF_F;
    best_index = -1;
    for (pose = lane; pose < pose_count; pose += 32) {
        int base = pose * ADIOS_MAX_INTERACTIONS;
        float sum = 0.0f;
        int j;
        const int8_t* sh_antigen = sh_antigens[warp];

        for (j = 0; j < ADIOS_MAX_INTERACTIONS; j++) {
            int primary_slot = (int)primary_sites_antibody[base + j] + 1;
            int secondary_ab_slot = (int)secondary_sites_antibody[base + j] + 1;
            int secondary_ag_slot = (int)secondary_sites_antigen[base + j] + 1;
            int amino_primary = (int)sh_antibody[primary_slot];
            int amino_secondary_ab = (int)sh_antibody[secondary_ab_slot];
            int amino_secondary_ag = (int)sh_antigen[secondary_ag_slot];
            int row_base = amino_primary * ADIOS_JPADDED_MATRIX_COLS;
            sum += JPADDED_MATRIX_DEVICE[row_base + amino_secondary_ag];
            sum += JPADDED_MATRIX_DEVICE[row_base + amino_secondary_ab];
        }

        if (better_result(sum, pose, best_value, best_index)) {
            best_value = sum;
            best_index = pose;
        }
    }

    for (pose = 16; pose > 0; pose >>= 1) {
        float other_value = __shfl_down_sync(0xffffffffu, best_value, pose);
        int32_t other_index = __shfl_down_sync(0xffffffffu, best_index, pose);
        if (better_result(other_value, other_index, best_value, best_index)) {
            best_value = other_value;
            best_index = other_index;
        }
    }

    if (lane == 0) {
        out_min[query] = best_value;
        out_argmin[query] = best_index;
    }
}

__global__ void select_replicate(
        const float* target_binding_values, const int32_t* target_pose_indices,
        const float* antitarget_binding_values,
        const int32_t* antitarget_pose_indices, const int8_t* population_in,
        int population_size, float target_clip_min, float selection_temperature,
        AdiosKey choice_key, int8_t* selected_antigen, float* fitness_values,
        float* selection_probs, AdiosFitnessInfo* fitness_info,
        float* out_best_fitness, AdiosFitnessInfo* out_best_extra,
        int32_t* out_best_member) {
    int i;
    int j;
    float max_value;
    float sum;
    int32_t selected_member;

    if (blockIdx.x != 0 || threadIdx.x != 0) {
        return;
    }

    for (i = 0; i < population_size; i++) {
        float binding_values_target = target_binding_values[i];
        float binding_values_antitarget = antitarget_binding_values[i];
        float clipped_target = clip_value(binding_values_target, target_clip_min, CUDART_INF_F);
        float clipped_antitarget = clip_value(binding_values_antitarget, -CUDART_INF_F, CUDART_INF_F);
        fitness_values[i] = -1.0f * (clipped_target - clipped_antitarget);
        fitness_info[i].binding_values_target = binding_values_target;
        fitness_info[i].binding_values_antitarget = binding_values_antitarget;
        fitness_info[i].poses_target = target_pose_indices[i];
        fitness_info[i].poses_antitarget = antitarget_pose_indices[i];
    }

    max_value = fitness_values[0] / selection_temperature;
    for (i = 1; i < population_size; i++) {
        float value = fitness_values[i] / selection_temperature;
        if (value > max_value) {
            max_value = value;
        }
    }

    sum = 0.0f;
    for (i = 0; i < population_size; i++) {
        selection_probs[i] = expf(fitness_values[i] / selection_temperature - max_value);
        sum += selection_probs[i];
    }
    for (i = 0; i < population_size; i++) {
        selection_probs[i] /= sum;
    }

    selected_member = choose_index(choice_key, selection_probs, population_size);
    *out_best_fitness = fitness_values[selected_member];
    *out_best_extra = fitness_info[selected_member];

    for (j = 0; j < ADIOS_ANTIGEN_LEN; j++) {
        out_best_member[j] = (int32_t)population_in[(size_t)selected_member * PADDED_ANTIGEN_LEN + j + 1];
    }

    for (j = 0; j < PADDED_ANTIGEN_LEN; j++) {
        selected_antigen[j] = population_in[(size_t)selected_member * PADDED_ANTIGEN_LEN + j];
    }
}

__global__ void fill_population(const int8_t* template_antigen,
        int8_t* population, int rows) {
    int index = blockIdx.x * blockDim.x + threadIdx.x;
    int total = rows * PADDED_ANTIGEN_LEN;
    int col;

    if (index >= total) {
        return;
    }

    col = index % PADDED_ANTIGEN_LEN;
    population[index] = template_antigen[col];
}

__global__ void mutate(AdiosKey key, const int8_t* selected_antigen,
        int8_t* population, int rows, int cols, float p) {
    AdiosKey split_keys[2];
    int index = blockIdx.x * blockDim.x + threadIdx.x;
    int total = rows * PADDED_ANTIGEN_LEN;
    int row;
    int col;
    int population_index;
    int mutation_index;
    int mutation_mask;
    int32_t mutation_delta;

    if (index >= total) {
        return;
    }

    row = index / PADDED_ANTIGEN_LEN;
    col = index % PADDED_ANTIGEN_LEN;
    population_index = row * PADDED_ANTIGEN_LEN + col;
    if (col == 0) {
        population[population_index] = (int8_t)ADIOS_PAD_INDEX;
        return;
    }

    prng_split_n(key, split_keys, 2);
    mutation_index = row * cols + (col - 1);
    mutation_mask = bernoulli_f32_at(split_keys[0], (uint64_t)mutation_index, p);
    mutation_delta = randint_i32_at(split_keys[1], (uint64_t)mutation_index, 1, 20);
    population[population_index] = (int8_t)(((int)selected_antigen[col] + mutation_mask * mutation_delta) % ADIOS_NUM_AMINO_ACIDS);
}

void run_rng_test(RngTestOutput* out) {
    RngTestOutput* d_out;

    CHECK(cudaMalloc((void**)&d_out, sizeof(RngTestOutput)));
    rng_test<<<1, 1>>>(d_out);
    CHECK(cudaGetLastError());
    CHECK(cudaDeviceSynchronize());
    CHECK(cudaMemcpy(out, d_out, sizeof(*out), cudaMemcpyDeviceToHost));
    CHECK(cudaFree(d_out));
}

void bind_batch(const PoseSet* pose_set, const int8_t* padded_antibody,
        const int8_t* padded_antigens, int pool_size, float* out_min,
        int32_t* out_argmin) {
    if (pose_set->count <= 2048) {
        int blocks = (pool_size + SMALL_WARPS - 1) / SMALL_WARPS;
        bind_small<<<blocks, SMALL_THREADS>>>(
            pose_set->primary_sites_antibody, pose_set->secondary_sites_antibody,
            pose_set->secondary_sites_antigen, pose_set->count, padded_antibody,
            padded_antigens, PADDED_ANTIGEN_LEN, pool_size, out_min, out_argmin);
    } else {
        bind_block<<<pool_size, BIND_THREADS>>>(
            pose_set->primary_sites_antibody, pose_set->secondary_sites_antibody,
            pose_set->secondary_sites_antigen, pose_set->count, padded_antibody,
            padded_antigens, PADDED_ANTIGEN_LEN, out_min, out_argmin);
    }
    CHECK(cudaGetLastError());
}

static void bind_loaded_single(State* state, const PoseSet* pose_set,
        const int8_t* padded_antibody) {
    bind_batch(pose_set, padded_antibody, state->single_antigen, 1,
        state->single_binding_value, state->single_binding_index);
}

static void bind_population_against_target(State* state,
        const PoseSet* pose_set, const int8_t* population, int population_size) {
    bind_batch(pose_set, state->padded_target, population, population_size,
        state->target_binding_values, state->target_pose_indices);
}

static void bind_population_against_antibody(State* state,
        const PoseSet* pose_set, const int8_t* population, int population_size) {
    bind_batch(pose_set, state->padded_antibody, population, population_size,
        state->antitarget_binding_values, state->antitarget_pose_indices);
}

static void select_population_member(State* state,
        const int8_t* population_in, int population_size, int antigen_len,
        float target_clip_min, float selection_temperature,
        AdiosKey choice_key, int step) {
    select_replicate<<<1, 1>>>(
        state->target_binding_values, state->target_pose_indices,
        state->antitarget_binding_values, state->antitarget_pose_indices, population_in,
        population_size, target_clip_min, selection_temperature, choice_key,
        state->single_antigen, state->fitness_values, state->selection_probs,
        state->fitness_info, &state->best_fitness_history[step],
        &state->best_member_extra_history[step],
        &state->best_members_history[(size_t)step * antigen_len]);
    CHECK(cudaGetLastError());
}

void fill_population_device(const int8_t* template_antigen, int8_t* population, int rows) {
    int total_elements = rows * PADDED_ANTIGEN_LEN;
    fill_population<<<(total_elements + MUTATE_THREADS - 1) / MUTATE_THREADS, MUTATE_THREADS>>>(
        template_antigen, population, rows);
    CHECK(cudaGetLastError());
    CHECK(cudaDeviceSynchronize());
}

void mutate_population(AdiosKey key, const int8_t* selected_antigen,
        int8_t* population, int rows, int cols, float p) {
    int total_elements = rows * PADDED_ANTIGEN_LEN;
    mutate<<<(total_elements + MUTATE_THREADS - 1) / MUTATE_THREADS, MUTATE_THREADS>>>(
        key, selected_antigen, population, rows, cols, p);
    CHECK(cudaGetLastError());
    CHECK(cudaDeviceSynchronize());
}

void load_single_antigen(State* state, const int32_t* antigen, int antigen_len) {
    int8_t padded_antigen[PADDED_ANTIGEN_LEN];
    make_padded_sequence(antigen, antigen_len, PADDED_ANTIGEN_LEN, padded_antigen);
    CHECK(cudaMemcpy(state->single_antigen, padded_antigen, sizeof(padded_antigen), cudaMemcpyHostToDevice));
}

void bind_single(State* state, const PoseSet* pose_set,
        const int32_t* antibody, int antibody_len, const int32_t* antigen,
        int antigen_len, float* out_value, int32_t* out_index) {
    int8_t padded_antibody[PADDED_ANTIBODY_LEN];

    make_padded_sequence(antibody, antibody_len, PADDED_ANTIBODY_LEN, padded_antibody);
    CHECK(cudaMemcpy(state->padded_antibody, padded_antibody, sizeof(padded_antibody), cudaMemcpyHostToDevice));
    load_single_antigen(state, antigen, antigen_len);
    bind_loaded_single(state, pose_set, state->padded_antibody);
    CHECK(cudaDeviceSynchronize());
    CHECK(cudaMemcpy(out_value, state->single_binding_value, sizeof(float), cudaMemcpyDeviceToHost));
    CHECK(cudaMemcpy(out_index, state->single_binding_index, sizeof(int32_t), cudaMemcpyDeviceToHost));
}

AdiosShapeRunResult single_shape_run(
        State* state, AdiosKey rng, const int32_t* antibody,
        const PoseSet* pose_set, const int32_t* start_antigen, int antigen_len,
        const int32_t* antigen_target, int antigen_target_len,
        const int32_t* antibody_antitarget, int antibody_antitarget_len,
        int horizon, AdiosShapeParams shape_params) {
    int8_t padded_antibody[PADDED_ANTIBODY_LEN];
    int8_t padded_target[PADDED_ANTIBODY_LEN];
    int8_t* current_population;
    float target_clip_min = -INFINITY;
    float binding_penalty = 0.0f;
    int32_t bind_index = -1;
    int step;
    if (horizon > state->max_generations) {
        fprintf(stderr, "runtime: horizon exceeds scratch capacity\n");
        exit(1);
    }
    if (shape_params.antigen_pop_size > state->max_population_size) {
        fprintf(stderr, "runtime: population exceeds scratch capacity\n");
        exit(1);
    }
    if (antigen_len > ADIOS_ANTIGEN_LEN) {
        fprintf(stderr, "runtime: antigen length exceeds scratch capacity\n");
        exit(1);
    }
    if (antigen_target == NULL) {
        fprintf(stderr, "runtime: antigen target is required\n");
        exit(1);
    }

    make_padded_sequence(antibody, ADIOS_ANTIBODY_LEN, PADDED_ANTIBODY_LEN, padded_antibody);
    make_padded_sequence(antigen_target, antigen_target_len, PADDED_ANTIBODY_LEN, padded_target);
    CHECK(cudaMemcpy(state->padded_antibody, padded_antibody, sizeof(padded_antibody), cudaMemcpyHostToDevice));
    CHECK(cudaMemcpy(state->padded_target, padded_target, sizeof(padded_target), cudaMemcpyHostToDevice));

    load_single_antigen(state, start_antigen, antigen_len);
    bind_loaded_single(state, pose_set, state->padded_target);
    CHECK(cudaDeviceSynchronize());
    CHECK(cudaMemcpy(&target_clip_min, state->single_binding_value, sizeof(float), cudaMemcpyDeviceToHost));
    target_clip_min -= 1.0f;

    fill_population_device(state->single_antigen, state->population_a, shape_params.antigen_pop_size);

    current_population = state->population_a;
    for (step = 0; step < horizon; step++) {
        AdiosKey split_keys[3];
        AdiosKey step_key = adios_prng_split_index(rng, (uint32_t)step);
        adios_prng_split_n(step_key, split_keys, 3);

        bind_population_against_target(
            state, pose_set, current_population, shape_params.antigen_pop_size);
        bind_population_against_antibody(
            state, pose_set, current_population, shape_params.antigen_pop_size);

        select_population_member(
            state, current_population, shape_params.antigen_pop_size, antigen_len,
            target_clip_min, shape_params.antigen_selection_temperature, split_keys[1], step);

        mutate_population(
            split_keys[2], state->single_antigen, current_population,
            shape_params.antigen_pop_size, antigen_len,
            shape_params.antigen_mut_rate / (float)antigen_len);
    }

    if (antibody_antitarget != NULL) {
        load_single_antigen(state, antibody_antitarget, antibody_antitarget_len);
        bind_loaded_single(state, pose_set, state->padded_antibody);
        CHECK(cudaDeviceSynchronize());
        CHECK(cudaMemcpy(&binding_penalty, state->single_binding_value, sizeof(float), cudaMemcpyDeviceToHost));
        CHECK(cudaMemcpy(&bind_index, state->single_binding_index, sizeof(int32_t), cudaMemcpyDeviceToHost));
    }

    CHECK(cudaMemcpy(
        state->host_best_fitness_history,
        state->best_fitness_history,
        (size_t)horizon * sizeof(float),
        cudaMemcpyDeviceToHost));
    CHECK(cudaMemcpy(
        state->host_best_member_extra_history,
        state->best_member_extra_history,
        (size_t)horizon * sizeof(AdiosFitnessInfo),
        cudaMemcpyDeviceToHost));
    CHECK(cudaMemcpy(
        state->host_best_members_history,
        state->best_members_history,
        (size_t)horizon * antigen_len * sizeof(int32_t),
        cudaMemcpyDeviceToHost));

    for (step = 0; step < horizon; step++) {
        state->host_ag_performances[step] = state->host_best_fitness_history[step] - binding_penalty;
    }

    AdiosShapeRunResult result;
    result.horizon = horizon;
    result.seq_len = antigen_len;
    result.ag_performances = state->host_ag_performances;
    result.binding_penalty = binding_penalty;
    result.ab_t_m_pose_index = bind_index;
    result.best_fitness = state->host_best_fitness_history;
    result.best_member_extra_info = state->host_best_member_extra_history;
    result.best_members = state->host_best_members_history;
    return result;
}

