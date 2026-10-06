/* Small Node-API bridge to the generated numeric kernels. Node-API's opaque
 * types and the ABI functions used here are declared locally so this does not
 * require node-gyp, a native npm package, or a second JavaScript process. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
typedef struct napi_env__ *napi_env;
typedef struct napi_value__ *napi_value;
typedef struct napi_callback_info__ *napi_callback_info;
typedef int napi_status;
typedef int napi_typedarray_type;
typedef napi_value (*napi_callback)(napi_env, napi_callback_info);
extern napi_status napi_get_cb_info(napi_env,napi_callback_info,size_t*,napi_value*,napi_value*,void**);
extern napi_status napi_get_value_uint32(napi_env,napi_value,uint32_t*);
extern napi_status napi_get_typedarray_info(napi_env,napi_value,napi_typedarray_type*,size_t*,void**,napi_value*,size_t*);
extern napi_status napi_create_function(napi_env,const char*,size_t,napi_callback,void*,napi_value*);
extern napi_status napi_create_string_utf8(napi_env,const char*,size_t,napi_value*);
extern napi_status napi_set_named_property(napi_env,napi_value,const char*,napi_value);
extern napi_status napi_get_undefined(napi_env,napi_value*);
extern napi_status napi_throw_type_error(napi_env,const char*,const char*);
extern const char *pg9_kernel_source_hash(void);
extern int pg9_kernel_slots(unsigned);
extern void pg9_kernel_checked(unsigned,size_t,const double*const*,double*,uint32_t*,double*);

static napi_value failure(napi_env env,const char *message){
    napi_throw_type_error(env,"PG9_KERNEL_ARGUMENT",message);return NULL;
}
static napi_value execute(napi_env env,napi_callback_info info){
    size_t argc=6;napi_value args[6];uint32_t op,count;
    if(napi_get_cb_info(env,info,&argc,args,NULL,NULL)||argc!=6||
       napi_get_value_uint32(env,args[0],&op)||napi_get_value_uint32(env,args[1],&count))
        return failure(env,"run expects operation, count and four typed arrays");
    int slots=pg9_kernel_slots(op);
    if(slots<0||count>1048576u)return failure(env,"invalid kernel or lane count");
    void *data[4];size_t lengths[4];
    for(int i=0;i<4;i++){
        napi_typedarray_type type;
        if(napi_get_typedarray_info(env,args[i+2],&type,&lengths[i],&data[i],NULL,NULL)||
           type!=(i==2?6:8))return failure(env,"expected Float64/Float64/Uint32/Float64 arrays");
        size_t need=i==0?(size_t)slots*count:count;
        if(lengths[i]<need)return failure(env,"typed array is too short");
    }
    /* The generated ABI has restrict output parameters. Reject aliases rather
     * than allowing callers to invoke undefined C behavior. */
    for(int i=0;i<4;i++)for(int j=i+1;j<4;j++){
        uintptr_t a=(uintptr_t)data[i],b=(uintptr_t)data[j];
        size_t an=(i==2?4:8)*(i==0?(size_t)slots*count:count);
        size_t bn=(j==2?4:8)*(j==0?(size_t)slots*count:count);
        if(an&&bn&&a<b+bn&&b<a+an)return failure(env,"kernel buffers must not overlap");
    }
    if(!count){napi_value result;if(napi_get_undefined(env,&result))return NULL;return result;}
    const double **inputs=malloc((size_t)(slots?slots:1)*sizeof(*inputs));
    if(!inputs)return failure(env,"kernel pointer allocation failed");
    for(int i=0;i<slots;i++)inputs[i]=(const double*)data[0]+(size_t)i*count;
    pg9_kernel_checked(op,count,inputs,data[1],data[2],data[3]);free(inputs);
    napi_value result;if(napi_get_undefined(env,&result))return NULL;return result;
}
__attribute__((visibility("default"))) napi_value napi_register_module_v1(napi_env env,napi_value exports){
    napi_value run,hash;
    if(napi_create_function(env,"run",SIZE_MAX,execute,NULL,&run)||
       napi_set_named_property(env,exports,"run",run)||
       napi_create_string_utf8(env,pg9_kernel_source_hash(),SIZE_MAX,&hash)||
       napi_set_named_property(env,exports,"sourceHash",hash))return NULL;
    return exports;
}
