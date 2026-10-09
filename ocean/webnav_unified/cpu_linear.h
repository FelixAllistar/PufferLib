#ifndef WEBNAV_UNIFIED_CPU_LINEAR_H
#define WEBNAV_UNIFIED_CPU_LINEAR_H
/* Evaluator-only SIMD implementation of the existing FP32 linear layer.
 * Four accumulators avoid the scalar reduction's long dependency chain.
 * Buffers/weights and network architecture are unchanged. Reduction order
 * differs from scalar FP32; validate numerical tolerance in test_cpu_linear. */
static void wu_linear(Linear *layer,const float *input) {
    for(int b=0;b<layer->batch_size;b++)for(int o=0;o<layer->output_dim;o++) {
        const float *x=input+(size_t)b*layer->input_dim;
        const float *w=layer->weights+(size_t)o*layer->input_dim;
        float sum=0;int i=0;
#if defined(__AVX2__) && defined(__FMA__)
        __m256 a=_mm256_setzero_ps(),c=a,d=a,e=a;
        for(;i+32<=layer->input_dim;i+=32){
            a=_mm256_fmadd_ps(_mm256_loadu_ps(x+i),_mm256_loadu_ps(w+i),a);
            c=_mm256_fmadd_ps(_mm256_loadu_ps(x+i+8),_mm256_loadu_ps(w+i+8),c);
            d=_mm256_fmadd_ps(_mm256_loadu_ps(x+i+16),_mm256_loadu_ps(w+i+16),d);
            e=_mm256_fmadd_ps(_mm256_loadu_ps(x+i+24),_mm256_loadu_ps(w+i+24),e);
        }
        a=_mm256_add_ps(_mm256_add_ps(a,c),_mm256_add_ps(d,e));
        for(;i+8<=layer->input_dim;i+=8)
            a=_mm256_fmadd_ps(_mm256_loadu_ps(x+i),_mm256_loadu_ps(w+i),a);
        float lanes[8];_mm256_storeu_ps(lanes,a);
        for(int k=0;k<8;k++)sum+=lanes[k];
#endif
        for(;i<layer->input_dim;i++)sum+=x[i]*w[i];
        layer->output[(size_t)b*layer->output_dim+o]=sum;
    }
}
#endif
