#include "../transport/json.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    const char *const valid[]={"null","true","false","0","-0","1.25e+3","\"text\"",
        "[0,true,false,null,{\"x\":[1,2]}]"," \n {\"emoji\":\"\\ud83d\\ude00\"} \t",
        "\"\xf0\x9f\x98\x80\"","\"\\u0000\""};
    const char *const invalid[]={""," ","nul","nan","Infinity","01","1.",".5","1e","--1",
        "[1,]","[,1]","{\"x\":}","{x:1}","{\"x\":1,}","null null","\"\\x20\"",
        "\"\\ud800\"","\"\\udc00\"","\"\\ud800\\u0000\"","\"\xc0\xaf\"",
        "\"\xed\xa0\x80\"","\"\xf4\x90\x80\x80\"","\"\x80\"","\"line\nbreak\""};
    for (unsigned i=0;i<sizeof valid/sizeof *valid;i++) assert(wj_validate(valid[i],strlen(valid[i]),16,0,NULL));
    for (unsigned i=0;i<sizeof invalid/sizeof *invalid;i++) assert(!wj_validate(invalid[i],strlen(invalid[i]),16,0,NULL));
    assert(!wj_validate("\"\\u0000\"",8,16,WJ_NO_NUL_STRING,NULL));
    WJInfo info={0};
    const char *nested="[{},null,1,true,\"x\",[]]";
    assert(wj_validate(nested,strlen(nested),16,0,&info));
    assert(info.type==WJ_ARRAY && info.array_item_types==63u);
    char deep[133];
    for (unsigned i=0;i<65;i++) deep[i]='[';
    deep[65]='0';for (unsigned i=66;i<131;i++) deep[i]=']';deep[131]=0;
    assert(!wj_validate(deep,131,64,0,NULL));
    assert(wj_validate(deep+1,129,64,0,NULL));
    char embedded[]={'"','a',0,'b','"'};
    assert(!wj_validate(embedded,sizeof embedded,16,0,NULL));
    puts("PASS: shared JSON grammar, UTF-8/surrogates, optional NUL exclusion, nested values and depth limit");
    return 0;
}
