inline void escape(int* value) {
    #if defined(__clang__)
    asm volatile("" : "+r,m"(value) : : "memory");
    #else
    asm volatile("" : "+m,r"(value) : : "memory");
    #endif
}

#define ARRAY_LENGTH 2048

void calculate(){
    const int length = ARRAY_LENGTH;
    
    int weight[length];
    int value[length];
    int total[length];
    
    // populate weight, value and zero total
    
    for(int i = 0 ; i < length-1 ; i++){
        total[i] += value[i]+weight[i];
        value[i+1] = weight[i+1]-1;
    }

    total[length-1] += value[length-1]+weight[length-1];
    escape(total);    
}  

void calculateVec(){
    const int length = ARRAY_LENGTH;
    
    int weight[length];
    int value[length];
    int total[length];

    // populate weight, value and zero total
    total[0] += value[0]+weight[0];
    
    for(int i = 1 ; i < length ; i++){        
        value[i] = weight[i]-1;
        total[i] += value[i]+weight[i];
    }

    escape(total);    
}  