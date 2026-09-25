#include "../shenaniguns3d.h"

void s3d_reference_reset(Env* env) { puf_reset(env); }
void s3d_reference_step(Env* env) { puf_step(env); }
void s3d_reference_close(Env* env) { puf_close(env); }
