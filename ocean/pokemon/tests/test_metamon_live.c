#define _XOPEN_SOURCE 700
#include <assert.h>
#include <sys/stat.h>
#include "../metamon_eval.h"

static void pause_ms(long ms) {
    struct timespec t={ms/1000,(ms%1000)*1000000}; nanosleep(&t,NULL);
}

static int fake_evaluator(int argc, char** argv) {
    const char* out=NULL;
    for (int i=1;i+1<argc;i++) if (!strcmp(argv[i],"--output")) out=argv[i+1];
    assert(out); assert(mkdir(out,0700)==0);
    const char* mode=getenv("PK_METAMON_TEST_MODE");
    if (mode && !strcmp(mode,"fail")) return 7;
    if (mode && !strcmp(mode,"slow")) pause_ms(10000);
    char path[4200]; snprintf(path,sizeof(path),"%s/progress.txt",out);
    FILE* fp=fopen(path,"w"); assert(fp);
    fprintf(fp,"METAMON_V1 2 1 0 1 0.5 6\n"); fclose(fp);
    pause_ms(10);
    snprintf(path,sizeof(path),"%s/result.txt",out); fp=fopen(path,"w"); assert(fp);
    if (mode && !strcmp(mode,"bad")) fprintf(fp,"METAMON_V1 6 4 0 1 nan 6\n");
    else if (mode && !strcmp(mode,"lower")) fprintf(fp,"METAMON_V1 6 2 0 4 0.33333333333333331 6\n");
    else fprintf(fp,"METAMON_V1 6 4 0 2 0.66666666666666663 6\n");
    fclose(fp); return 0;
}

static void run_fake(const char* directory, const char* name, const char* mode, long steps) {
    char checkpoint[4096]; snprintf(checkpoint,sizeof(checkpoint),"%s/%s.bin",directory,name);
    setenv("PK_METAMON_TEST_MODE",mode,1);
    pk_metamon_begin(checkpoint,steps,steps);
    while (pk_metamon_poll()) pause_ms(5);
    assert(pk_metamon.pid==0);
}

int main(int argc,char** argv) {
    if (argc>1 && !strcmp(argv[1],"ocean/pokemon/benchmarks/evaluate_private.py")) return fake_evaluator(argc,argv);
    if (argc==3 && !strcmp(argv[1],"--real")) {
        Ini ini={0}; puf_ini_load_file(&ini,"config/default.ini"); puf_ini_load_file(&ini,"config/pokemon.ini");
        puf_ini_put(&ini,"metamon.games","2");
        puf_ini_put(&ini,"metamon.final_games","2");
        pk_metamon_init(&ini,1);
        assert(pk_metamon_due(1,1,1));
        pk_metamon_begin(argv[2],1,1);
        int active;
        do { active=pk_metamon_poll(); pk_metamon_dashboard(0,3); if(active)pause_ms(500); } while(active);
        assert(pk_metamon.state==2 && pk_metamon.last_games==6);
        printf("REAL_METAMON_OK score=%.9f games=%d (no training performed)\n",pk_metamon.score,pk_metamon.last_games);
        return 0;
    }
    Ini missing={0}; pk_metamon_init(&missing,1); assert(!pk_metamon.enabled);
    pk_metamon.enabled=1; pk_metamon.fraction=.1;
    int checks=0;
    for(long epoch=1;epoch<=15258;epoch++) if(pk_metamon_due(epoch,15258,65536)) {
        checks++; pk_metamon.next_epoch=epoch+pk_metamon.interval_epochs;
    }
    assert(checks==10 && pk_metamon.target_steps==999948288);
    Dict log={0}; pk_metamon_log(&log); assert(!dict_find(&log,"env/metamon_score"));
    char directory[]="/tmp/pokemon-live-test.XXXXXX"; assert(mkdtemp(directory));
    assert(realpath(argv[0],pk_metamon.python));
    snprintf(pk_metamon.device,sizeof(pk_metamon.device),"cpu");
    pk_metamon.games=2; pk_metamon.seed=3; pk_metamon.timeout=5;
    run_fake(directory,"first","ok",100);
    assert(pk_metamon.state==2 && pk_metamon.has_score && pk_metamon.completed==6);
    assert(fabs(pk_metamon.score-2./3)<1e-9 && pk_metamon.best_steps==100);
    run_fake(directory,"lower","lower",200);
    assert(fabs(pk_metamon.score-1./3)<1e-9 && fabs(pk_metamon.best-2./3)<1e-9);
    assert(pk_metamon.best_steps==100 && pk_metamon.score_steps==200);
    assert(pk_metamon.best_games==6);
    // A separate final panel changes only the final evaluation game count.
    pk_metamon.interim_games=2; pk_metamon.final_games=4; pk_metamon.target_steps=250;
    run_fake(directory,"final_count","ok",250);
    assert(pk_metamon.games==4 && pk_metamon.state==3); // fake emits six, expected twelve
    pk_metamon.interim_games=pk_metamon.final_games=0;
    pk_metamon.games=2; pk_metamon.failures=0;
    run_fake(directory,"bad","bad",300);
    assert(pk_metamon.state==3 && pk_metamon.failures==1 && pk_metamon.score_steps==200);
    run_fake(directory,"fail","fail",400);
    assert(pk_metamon.state==3 && pk_metamon.failures==2);
    pk_metamon.timeout=.01;
    run_fake(directory,"slow","slow",500);
    assert(pk_metamon.state==3 && pk_metamon.failures==3);
    pk_metamon_log(&log);
    assert(fabs(dict_get(&log,"env/metamon_score")-1./3)<1e-9);
    pk_metamon.state=2;
    assert(pk_metamon_dashboard(0,3)==3);
    assert(pk_metamon_dashboard(0,1)==0);
    char compact[256]; pk_metamon_compact(compact,sizeof(compact),"500","100K");
    assert(strstr(compact,"Metamon=33.33%") && strstr(compact,"best=66.67%"));
    dict_clear(&log);
    puts("PASS: native schedule, pending/score/best, subprocess progress, failure, timeout, dashboard");
    return 0;
}
