#ifndef WT_CONTRACT_H
#define WT_CONTRACT_H
#include "training.h"
#ifdef __cplusplus
extern "C" {
#endif
int wt_contract_write(const char *path,int potion,int hidden,int layers,unsigned seed,unsigned tasks);
int wt_contract_read(const char *path,int *potion,int *hidden,int *layers);
#ifdef __cplusplus
}
#endif
#endif
