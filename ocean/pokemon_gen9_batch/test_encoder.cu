#define PRECISION_FLOAT
#define ENV_HEADER "ocean/pokemon_gen9_batch/pokemon_gen9_batch.h"
#define PUFFER_ENV_NAME "pokemon_gen9_batch"
#include "../../src/pufferl.cu"

static void gpu(cudaError_t error){if(error!=cudaSuccess){fprintf(stderr,"%s\n",cudaGetErrorString(error));exit(1);}}
static PG9Block host_blocks[PG9_COMPACT_BLOCK_COUNT]=PG9_COMPACT_BLOCKS_DATA;
static void compare(Prec actual,const float* expected,int count,float tolerance){
    float* values=(float*)malloc((size_t)count*sizeof(float));gpu(cudaMemcpy(values,actual.data,(size_t)count*sizeof(float),cudaMemcpyDeviceToHost));
    float largest=0;
    for(int i=0;i<count;i++){
        float error=fabsf(values[i]-expected[i]);
        if(!isfinite(values[i])||error>tolerance*(1+fabsf(expected[i]))){
            fprintf(stderr,"encoder mismatch index=%d actual=%g expected=%g error=%g\n",i,values[i],expected[i],error);exit(1);
        }largest=fmaxf(largest,error);
    }
    printf("PASS compact encoder values=%d max_error=%g\n",count,largest);free(values);
}
int main(){
    const int rows=5,hidden=16;cublas_init_handle();cudaStream_t stream;gpu(cudaStreamCreate(&stream));
    Encoder encoder=build_arch(PG9_OBS,hidden,1,PG9_ACTIONS,false,16).encoder;
    EncoderWeights* weights=(EncoderWeights*)encoder.create_weights(&encoder);
    Allocator parameters={},acts={},gradients={};PG9EncoderActs rollout={},train={};
    encoder.reg_params(weights,&parameters);encoder.reg_rollout(weights,&rollout,&acts,rows);
    encoder.reg_train(weights,&train,&acts,&gradients,rows);
    Prec input={.shape={rows,PG9_OBS}},cotangent={.shape={rows,hidden}};
    alloc_register(&acts,&input);alloc_register(&acts,&cotangent);
    alloc_create(&parameters);alloc_create(&acts);alloc_create(&gradients);
    float* packed=(float*)calloc(rows*PG9_OBS,sizeof(float));
    float* dense=(float*)calloc(rows*PG9_DENSE_OBS,sizeof(float));
    float* w=(float*)malloc(hidden*PG9_DENSE_OBS*sizeof(float));float grad[rows*hidden],expected[rows*hidden]={0};
    for(int i=0;i<hidden*PG9_DENSE_OBS;i++)w[i]=0.01f*sinf(i*0.731f);
    for(int r=0;r<rows;r++)for(int j=0;j<PG9_COMPACT_BLOCK_COUNT;j++){
        PG9Block b=host_blocks[j];
        if(b.kind==0)for(int i=0;i<b.width;i++){
            float value=((r*11+j*7+i)%31-15)/16.0f;
            packed[r*PG9_OBS+b.compact+i]=dense[r*PG9_DENSE_OBS+b.dense+i]=value;
        }else if(b.kind==1){
            int id=r==0?0:r==1?1:r==2?b.width:1+((r*17+j*13)%b.width);
            packed[r*PG9_OBS+b.compact]=(float)id;if(id)dense[r*PG9_DENSE_OBS+b.dense+id-1]=1;
        }else for(int i=0;i<b.width;i++){
            // Full category sets, word boundaries and the final partial word.
            int bit=r==0?0:r==1?1:((i+r+j)%7==0||i==b.width-1);
            dense[r*PG9_DENSE_OBS+b.dense+i]=(float)bit;
            if(bit)packed[r*PG9_OBS+b.compact+(i>>4)]=(float)((unsigned)packed[r*PG9_OBS+b.compact+(i>>4)]|(1u<<(i&15)));
        }
    }
    for(int r=0;r<rows;r++)for(int h=0;h<hidden;h++){
        double sum=0;for(int i=0;i<PG9_DENSE_OBS;i++)sum+=(double)dense[r*PG9_DENSE_OBS+i]*w[h*PG9_DENSE_OBS+i];
        expected[r*hidden+h]=(float)sum;grad[r*hidden+h]=0.2f*cosf((r*hidden+h)*0.41f);
    }
    gpu(cudaMemcpyAsync(input.data,packed,rows*PG9_OBS*sizeof(float),cudaMemcpyHostToDevice,stream));
    gpu(cudaMemcpyAsync(weights->weight.data,w,hidden*PG9_DENSE_OBS*sizeof(float),cudaMemcpyHostToDevice,stream));
    gpu(cudaMemcpyAsync(cotangent.data,grad,sizeof grad,cudaMemcpyHostToDevice,stream));
    Prec a=encoder.forward(weights,&rollout,input,stream),b=encoder.forward(weights,&train,input,stream);
    encoder.backward(weights,&train,cotangent,stream);gpu(cudaStreamSynchronize(stream));
    compare(train.expanded,dense,rows*PG9_DENSE_OBS,0);
    compare(a,expected,rows*hidden,2e-4f);compare(b,expected,rows*hidden,2e-4f);
    float* dw=(float*)malloc(hidden*PG9_DENSE_OBS*sizeof(float));
    for(int h=0;h<hidden;h++)for(int i=0;i<PG9_DENSE_OBS;i++){
        double sum=0;for(int r=0;r<rows;r++)sum+=(double)grad[r*hidden+h]*dense[r*PG9_DENSE_OBS+i];dw[h*PG9_DENSE_OBS+i]=(float)sum;
    }
    compare(train.linear.wgrad_scratch,dw,hidden*PG9_DENSE_OBS,2e-6f);
    // The sparse rollout and expansion/backward path must also be capturable.
    cudaGraph_t graph;cudaGraphExec_t executable;
    gpu(cudaStreamBeginCapture(stream,cudaStreamCaptureModeGlobal));
    encoder.forward(weights,&rollout,input,stream);encoder.forward(weights,&train,input,stream);
    encoder.backward(weights,&train,cotangent,stream);
    gpu(cudaStreamEndCapture(stream,&graph));gpu(cudaGraphInstantiate(&executable,graph,NULL,NULL,0));
    gpu(cudaGraphLaunch(executable,stream));gpu(cudaStreamSynchronize(stream));
    compare(a,expected,rows*hidden,2e-4f);compare(train.linear.wgrad_scratch,dw,hidden*PG9_DENSE_OBS,2e-6f);
    printf("PASS lossless categorical selectors, full sets, sparse rollout, dense minibatch, gradients and CUDA graph replay\n");
    gpu(cudaGraphExecDestroy(executable));gpu(cudaGraphDestroy(graph));gpu(cudaStreamDestroy(stream));
    free(packed);free(dense);free(w);free(dw);gpu(cudaFree(parameters.mem));gpu(cudaFree(acts.mem));gpu(cudaFree(gradients.mem));return 0;
}
