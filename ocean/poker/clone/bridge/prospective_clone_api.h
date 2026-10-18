#ifndef PROSPECTIVE_CLONE_API_H
#define PROSPECTIVE_CLONE_API_H
#include "clone_input_abi.h"

/* Proposed NEW v2 exports, coordinated with the model implementation owner.
 * They are declarations only here. The frozen v1 library lacks these runtime
 * identity exports and must never be wrapped to pretend it implements v2.
 */
unsigned bc_runtime_abi_version(void);
size_t bc_input_bytes(void);
unsigned bc_model_version(void);
unsigned bc_model_domain_version(void);
const char* bc_model_domain(void);
const char* bc_size_encoding(void);
const char* bc_feature_contract_sha256(void);
int bc_model_validate(void);
const char* bc_model_npz_sha256(void);
const char* bc_model_numeric_sha256(void);
size_t bc_model_numeric_bytes(void);
int bc_model_nodes(void);
int bc_supported(const BcInput*);
int bc_try_action(const BcInput*, uint64_t*, int32_t*);
#endif
