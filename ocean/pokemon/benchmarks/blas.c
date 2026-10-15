// Optional single-threaded NumPy OpenBLAS ILP64 backend for CPU benchmarks.
#include <stdint.h>
extern void scipy_cblas_sgemm64_(int,int,int,int64_t,int64_t,int64_t,float,
    const float*,int64_t,const float*,int64_t,float,float*,int64_t);
extern void scipy_openblas_set_num_threads64_(int);
void pk_benchmark_gemm(const float* input,const float* weights,float* output,
    int rows,int stride,int in,int out,int weight_stride) {
    static int configured=0;
    if(!configured){scipy_openblas_set_num_threads64_(1);configured=1;}
    scipy_cblas_sgemm64_(101,111,112,rows,out,in,1,input,stride,weights,weight_stride,0,output,out);
}
