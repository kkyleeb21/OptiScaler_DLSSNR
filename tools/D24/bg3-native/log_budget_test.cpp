#include <windows.h>
#include "dx11_log.h"
int main(){
 FILE* file=nullptr;if(fopen_s(&file,"log-budget-fixture.log","wb"))return 1;
 for(int i=0;i<10000;++i){if(allowNativeEvent("persistent_failure",100))logPrint(file,"failure\n");}
 if(nativeLogBytes!=32)return 2;
 if(allowNativeEvent("persistent_failure",5099)||!allowNativeEvent("persistent_failure",5100))return 3;
 char block[4097];memset(block,'x',4096);block[4096]=0;
 for(int i=0;i<10000;++i)logPrint(file,"%s",block);
 fflush(file);const auto bytes=_ftelli64(file);fclose(file);
 if(bytes>static_cast<long long>(nativeLogBudget)||bytes!=static_cast<long long>(nativeLogBytes)||!nativeLogDropped)return 4;
 printf("bounded log: %lld bytes, dropped=%llu; repeated event limit PASS\n",bytes,nativeLogDropped);return 0;
}
