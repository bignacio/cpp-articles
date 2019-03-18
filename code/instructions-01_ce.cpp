#include <benchmark/benchmark.h>

using namespace std;

#define ARRAY_LENGTH 2048

template <class T>
inline static void randomFill(T* data, int length, int skip){
    for(int i = 0 ; i < length ; i++){
        data[i] = random();
        if(data[i] == skip){
            data[i] = 0;
        }
    }
}

template <class T>
inline static void randomFill(T* data, int length){
    randomFill(data, length, 0);
}

static void BM_arrayIndex(benchmark::State& state) {          
    const int length = ARRAY_LENGTH;
    
    int* values = new int[length];    
    randomFill(values, length);
    unsigned int x = 0;
    for (auto _ : state){                                
        for(int i = 0 ; i < length ; i++){
            x += values[i];
        }                
    }
    benchmark::DoNotOptimize(x);

    delete[] values;
}

static void BM_pointerIncrement(benchmark::State& state) { 
    // add one to the array for the sentinel value
    const int length = ARRAY_LENGTH+1;
    const int sentinel = 1;
    
    int* values = new int[length];    
    randomFill(values, length, sentinel);
    
    // set the last position as the sentinel
    values[length-1] = sentinel;
    
    unsigned int x = 0;
    for (auto _ : state){                
        int* p = values;
        while(*p != sentinel){
            x += *p;
            p++;            
        }       
    }    
    benchmark::DoNotOptimize(x);
    delete[] values;
}


BENCHMARK(BM_arrayIndex);
BENCHMARK(BM_pointerIncrement);

BENCHMARK_MAIN();