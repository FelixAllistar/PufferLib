#pragma once
#include "retro_sweep_config.h"
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>

#define PUF_CONFIGURE(ini, mode) retro_configure(ini, mode)

static void retro_checkpoint_config(const char* checkpoint, Ini* ini) {
    std::string final=std::string(checkpoint)+".ini", temporary=final+".tmp";
    FILE* file=fopen(temporary.c_str(),"w");
    if(!file) throw std::runtime_error("retro: cannot write checkpoint configuration");
    puf_ini_write(file,ini);
    bool failed=ferror(file)!=0;
    if(fclose(file)!=0) failed=true;
    if(failed||rename(temporary.c_str(),final.c_str()))
        throw std::runtime_error("retro: cannot publish checkpoint configuration");
}
#define PUF_CHECKPOINT_HOOK(checkpoint, ini) retro_checkpoint_config(checkpoint, ini)

static float retro_sweep_score(const char* checkpoint, Ini* ini) {
    // argv-based spawning: checkpoint paths are never interpreted by a shell.
    std::vector<std::string> args={"build/retro/sweep_eval",checkpoint,"--config",
        std::string(checkpoint)+".ini","--output",std::string(checkpoint)+".eval.tsv"};
    for(const char* key:{"frames","repeats","seed","workers"}) {
        std::string option=std::string("eval_")+key;
        args.push_back(std::string("--")+key);
        args.push_back(std::to_string((long long)puf_ini_get(ini,"sweep",option.c_str())));
    }
    std::vector<char*> argv; for(auto& arg:args) argv.push_back(&arg[0]); argv.push_back(nullptr);
    int fds[2]; if(pipe(fds)) throw std::runtime_error("retro: panel pipe failed");
    posix_spawn_file_actions_t actions; posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions,fds[1],STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions,fds[0]);
    posix_spawn_file_actions_addclose(&actions,fds[1]);
    extern char** environ; pid_t pid=0;
    int error=posix_spawn(&pid,argv[0],&actions,nullptr,argv.data(),environ);
    posix_spawn_file_actions_destroy(&actions); close(fds[1]);
    if(error) { close(fds[0]); throw std::runtime_error("retro: build/retro/sweep_eval missing or failed to launch; run ./build.sh retro"); }
    FILE* stream=fdopen(fds[0],"r");
    float score=NAN; int results=0; char line[1024];
    if(stream) {
        while(fgets(line,sizeof(line),stream)) {
            float value;
            if(sscanf(line,"retro_panel version=1 score=%f",&value)==1) { score=value; results++; }
        }
        fclose(stream);
    } else close(fds[0]);
    int status=0; pid_t waited;
    do { waited=waitpid(pid,&status,0); } while(waited<0&&errno==EINTR);
    if(waited!=pid||!WIFEXITED(status)||WEXITSTATUS(status)||results!=1||!std::isfinite(score))
        throw std::runtime_error("retro: fixed-panel evaluation failed; refusing a fabricated sweep score");
    return score;
}
#define PUF_SWEEP_SCORE(checkpoint, ini) retro_sweep_score(checkpoint, ini)
