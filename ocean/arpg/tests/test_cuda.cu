#include "../arpg.cu"
#include <assert.h>

int main(void) {
    Ini ini={};puf_ini_load_env(&ini,"arpg",0,NULL);
    Dict* cfg=puf_ini_section(&ini,"env",0);
    const int count=17;
    dict_set(cfg,"enemy_cap",31); // odd pool dimensions exercise SoA alignment
    float *gpu_obs, *gpu_actions, *gpu_rewards, *gpu_terminals;
    AR_CUDA_CHECK(cudaMalloc(&gpu_obs, count * AR_OBS_SIZE * sizeof(float)));
    AR_CUDA_CHECK(cudaMalloc(&gpu_actions, count * NUM_ATNS * sizeof(float)));
    AR_CUDA_CHECK(cudaMalloc(&gpu_rewards, count * sizeof(float)));
    AR_CUDA_CHECK(cudaMalloc(&gpu_terminals, count * sizeof(float)));
    Env* envs=puf_vec_create(count,cfg,gpu_obs,gpu_actions,gpu_rewards,gpu_terminals);
    ARCudaSim* sim=&ar_gpu;
    ar_cuda_reset_all(sim,42);
    float actions[count*NUM_ATNS] = {0}, obs[count*AR_OBS_SIZE];
    AR_CUDA_CHECK(cudaMemcpy(sim->actions,actions,sizeof(actions),cudaMemcpyHostToDevice));
    for(int t=0;t<3600;t++)ar_cuda_step_range(sim,0,count);
    AR_CUDA_CHECK(cudaDeviceSynchronize());
    float hp[count],produced[count];int enemies[count],pets[count];
    AR_CUDA_CHECK(cudaMemcpy(hp,sim->hp,sizeof(hp),cudaMemcpyDeviceToHost));
    AR_CUDA_CHECK(cudaMemcpy(produced,sim->harvested,sizeof(produced),cudaMemcpyDeviceToHost));
    AR_CUDA_CHECK(cudaMemcpy(enemies,sim->enemy_count,sizeof(enemies),cudaMemcpyDeviceToHost));
    AR_CUDA_CHECK(cudaMemcpy(pets,sim->pets_alive,sizeof(pets),cudaMemcpyDeviceToHost));
    AR_CUDA_CHECK(cudaMemcpy(obs,sim->observations,sizeof(obs),cudaMemcpyDeviceToHost));
    for(int i=0;i<count;i++) {
        assert(hp[i]==sim->cfg.player_health && enemies[i]==0 && pets[i]==2);
        assert(produced[i]>=4);
    }
    for(float value:obs)assert(isfinite(value));
    for(int i=0;i<count;i++) {actions[i*NUM_ATNS+5]=AR_TASK_HOLD;actions[i*NUM_ATNS+6]=AR_TASK_HOME;}
    AR_CUDA_CHECK(cudaMemcpy(sim->actions,actions,sizeof(actions),cudaMemcpyHostToDevice));
    ar_cuda_step_range(sim,0,count);AR_CUDA_CHECK(cudaDeviceSynchronize());
    int tasks[count*AR_MAX_PETS];
    AR_CUDA_CHECK(cudaMemcpy(tasks,sim->pet_task,sizeof(tasks),cudaMemcpyDeviceToHost));
    for(int i=0;i<count;i++)assert(tasks[i]==AR_TASK_HOLD && tasks[count+i]==AR_TASK_HOME);
    sim->cfg.max_steps=3602;ar_cuda_step_range(sim,0,count);
    AR_CUDA_CHECK(cudaDeviceSynchronize());
    int ticks[count];float terminal[count];
    AR_CUDA_CHECK(cudaMemcpy(ticks,sim->tick,sizeof(ticks),cudaMemcpyDeviceToHost));
    AR_CUDA_CHECK(cudaMemcpy(terminal,sim->terminals,sizeof(terminal),cudaMemcpyDeviceToHost));
    for(int i=0;i<count;i++)assert(ticks[i]==0 && terminal[i]==1);
    sim->cfg.max_steps=900;
    int sizes[]=ACT_SIZES;uint32_t rng=17;
    for(int t=0;t<3600;t++) {
        if(t%30==0) {
            for(int i=0;i<count;i++)for(int h=0;h<NUM_ATNS;h++) {
                rng=rng*1664525u+1013904223u;actions[i*NUM_ATNS+h]=(float)((rng>>8)%sizes[h]);
            }
            AR_CUDA_CHECK(cudaMemcpy(sim->actions,actions,sizeof(actions),cudaMemcpyHostToDevice));
        }
        ar_cuda_step_range(sim,0,count);
        if(t%120==0) {
            AR_CUDA_CHECK(cudaMemcpy(obs,sim->observations,sizeof(obs),cudaMemcpyDeviceToHost));
            for(float value:obs)assert(isfinite(value));
        }
    }
    AR_CUDA_CHECK(cudaDeviceSynchronize());
    cudaStream_t stream;
    AR_CUDA_CHECK(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking));
    puf_bind_stream(stream);
    sim->cfg.max_steps = 1;
    puf_reset(envs);
    AR_CUDA_CHECK(cudaStreamSynchronize(stream));
    cudaGraph_t graph;
    cudaGraphExec_t executable;
    AR_CUDA_CHECK(cudaStreamBeginCapture(stream, cudaStreamCaptureModeGlobal));
    puf_step(envs);
    AR_CUDA_CHECK(cudaStreamEndCapture(stream, &graph));
    AR_CUDA_CHECK(cudaGraphInstantiate(&executable, graph, NULL, NULL, 0));
    for (int i = 0; i < 8; i++) {
        AR_CUDA_CHECK(cudaGraphLaunch(executable, stream));
    }
    AR_CUDA_CHECK(cudaStreamSynchronize(stream));
    Env shells[count];
    AR_CUDA_CHECK(cudaMemcpy(shells, envs, sizeof(shells), cudaMemcpyDeviceToHost));
    AR_CUDA_CHECK(cudaMemcpy(terminal, sim->terminals, sizeof(terminal), cudaMemcpyDeviceToHost));
    for (int i = 0; i < count; i++) {
        assert(shells[i].num_agents == 1 && shells[i].log.n >= 8);
        assert(terminal[i] == 1);
    }
    AR_CUDA_CHECK(cudaGraphExecDestroy(executable));
    AR_CUDA_CHECK(cudaGraphDestroy(graph));
    puf_close(envs);
    AR_CUDA_CHECK(cudaStreamDestroy(stream));
    envs=puf_vec_create(count,cfg,gpu_obs,gpu_actions,gpu_rewards,gpu_terminals);
    puf_reset(envs);
    AR_CUDA_CHECK(cudaDeviceSynchronize());
    puf_close(envs);puf_ini_free(&ini);
    cudaFree(gpu_obs);cudaFree(gpu_actions);cudaFree(gpu_rewards);cudaFree(gpu_terminals);
    puts("ARPG CUDA idle economy, 443-float observations, eight pet task heads, terminal reset: PASS");
}
