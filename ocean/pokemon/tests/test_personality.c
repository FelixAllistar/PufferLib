#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../personality.h"
static void fresh(uint8_t* x) {
    memset(x,0,640); x[0]=1; x[4]=1;
    for(int i=0;i<6;i++) { x[16+32*i]=i+1; x[17+32*i]=255; }
    for(int i=403;i<=408;i++) x[i]=6;
}
int main(void) {
    uint8_t a[640],b[640]; fresh(a); fresh(b);
    double f[5],w[5]={.2,.2,.2,.2,.2};
    pk_personality_features(a,f); assert(fabs(f[4]-1)<1e-12);
    for(int k=0;k<4;k++) assert(f[k]==0);
    assert(pk_personality_potential(a,b,w)==0);
    a[18]=5; pk_personality_features(a,f); assert(fabs(f[0]+1./6)<1e-12);
    a[19]=1; pk_personality_features(a,f); assert(f[0]==0);
    fresh(a); a[18]=1; pk_personality_features(a,f); assert(fabs(f[1]+1./6)<1e-12);
    a[403]=12; a[405]=12; a[406]=12; a[404]=12; a[422]=a[423]=1;
    pk_personality_features(a,f); assert(f[2]==1 && f[3]==1);
    double phi=pk_personality_potential(a,b,w);
    assert(fabs(phi+pk_personality_potential(b,a,w))<1e-12);
    a[0]=0; pk_personality_features(a,f);
    for(int k=0;k<5;k++) assert(f[k]==0);
    for(int length=1;length<100;length++) {
        double gamma=.999, discount=1, total=0, before=0;
        for(int t=0;t<length;t++) {
            double after=sin(t+1);
            total+=discount*pk_personality_shaping(before,after,gamma,t==length-1);
            discount*=gamma; before=after;
        }
        assert(fabs(total)<1e-12);
    }
    assert(pk_personality_shaping(.3,999,.99,1)==-.3);
    puts("personality state and telescoping tests passed");
}
