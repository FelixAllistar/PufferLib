// Linear chess encoder over the base observation only (2026-11-14). Included by src/ocean.cu with PUFFER_CHESS and
// CHESS_MOVE_FEATS: the per-move feature slots that chess.h appends after CHESS_OBS_END are small integers for the
// decoder's per-move logit bias (chess_decoder.cu), not encoder inputs, exactly as in bc_train.py --move-feats.
// Same math and weight shape (hidden, CHESS_OBS_END) as the default linear encoder on the base observation, so the
// Stockfish-BC checkpoints load unchanged.

__global__ void chess_prefix_gather(const precision_t* __restrict__ obs, precision_t* __restrict__ x, long n) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    long b = i / CHESS_OBS_END;
    x[i] = obs[b * OBS_SIZE + i % CHESS_OBS_END];
}

struct ChessPrefixEncoderActivations {
    Prec out, saved_input, wgrad_scratch;  // as EncoderActivations (saved_input = the gathered base observation)
    Prec x;                                // rollout: gathered base observation
};

static Prec chess_prefix_encoder_forward(void* w, void* activations, Prec input, cudaStream_t st) {
    EncoderWeights* ew = (EncoderWeights*)w;
    ChessPrefixEncoderActivations* a = (ChessPrefixEncoderActivations*)activations;
    long B = input.shape[0];
    Prec dst = a->saved_input.data ? a->saved_input : a->x;
    long n = B * CHESS_OBS_END;
    chess_prefix_gather<<<(int)((n + BLOCK_SIZE - 1) / BLOCK_SIZE), BLOCK_SIZE, 0, st>>>(input.data, dst.data, n);
    Prec x = {.data = dst.data, .shape = {B, CHESS_OBS_END}};
    Prec out = {.data = a->out.data, .shape = {B, ew->out_dim}};
    puf_mm(&x, &ew->weight, &out, st);
    return out;
}

static void chess_prefix_encoder_backward(void* w, void* activations, Prec grad, cudaStream_t st) {
    (void)w;
    ChessPrefixEncoderActivations* a = (ChessPrefixEncoderActivations*)activations;
    puf_mm_tn(&grad, &a->saved_input, &a->wgrad_scratch, st);
}

static void chess_prefix_encoder_train(void* w, void* activations, Allocator* acts, Allocator* grads, int B_TT) {
    EncoderWeights* ew = (EncoderWeights*)w;
    ChessPrefixEncoderActivations* a = (ChessPrefixEncoderActivations*)activations;
    *a = (ChessPrefixEncoderActivations){};
    a->out = {.shape = {B_TT, ew->out_dim}};
    a->saved_input = {.shape = {B_TT, CHESS_OBS_END}};
    a->wgrad_scratch = {.shape = {ew->out_dim, CHESS_OBS_END}};
    alloc_register(acts, &a->out);
    alloc_register(acts, &a->saved_input);
    alloc_register(grads, &a->wgrad_scratch);
}

static void chess_prefix_encoder_rollout(void* w, void* activations, Allocator* alloc, int B) {
    EncoderWeights* ew = (EncoderWeights*)w;
    ChessPrefixEncoderActivations* a = (ChessPrefixEncoderActivations*)activations;
    a->out = {.shape = {B, ew->out_dim}};
    a->x = {.shape = {B, CHESS_OBS_END}};
    alloc_register(alloc, &a->out);
    alloc_register(alloc, &a->x);
}

static void* chess_prefix_encoder_create(void* self) {
    Encoder* e = (Encoder*)self;
    EncoderWeights* ew = (EncoderWeights*)calloc(1, sizeof(EncoderWeights));
    ew->in_dim = CHESS_OBS_END;  // encoder_reg_params / encoder_init_weights size the weight from this
    ew->out_dim = e->out_dim;
    return ew;
}

static void create_chess_prefix_encoder(Encoder* e) {
    e->forward = chess_prefix_encoder_forward;
    e->backward = chess_prefix_encoder_backward;
    e->reg_train = chess_prefix_encoder_train;
    e->reg_rollout = chess_prefix_encoder_rollout;
    e->create_weights = chess_prefix_encoder_create;
    e->activation_size = sizeof(ChessPrefixEncoderActivations);
    // init_weights / reg_params: the defaults (weight (out_dim, in_dim) with in_dim = CHESS_OBS_END)
}
