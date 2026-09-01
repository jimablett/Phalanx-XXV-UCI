all:
	clang  -o phalanx *.c ./fathom/src/tbprobe.c -Ofast -flto=auto -ftree-vectorize -funroll-loops \
	-fno-exceptions -ffast-math -static -static-libgcc -DNDEBUG -DNDEBUG_TERMINAL -DNDEBUG_MATE -finline-functions -pipe -Wall -std=gnu23  \
	-fstrict-aliasing -fomit-frame-pointer -lm -fuse-ld=lld -MMD -MP -s -funsafe-math-optimizations -fsee -mpopcnt -w -s \
	-falign-functions=32 -ffunction-sections -fdata-sections \
	-march=x86-64-v2 -mtune=silvermont
	
	
	
	#        use same profiling data for all
	
	
	#       -fprofile-instr-generate -fcoverage-mapping                                                             <   before -o
	
	#        llvm-profdata merge -output=default.profdata *.profraw                                                 <  enter on command line
 
    #       -fprofile-use=default.profdata                                                                          <   before -o
	
    
    #        -march=x86-64-v2 -mtune=silvermont                                                                     <  for popcount builds (with sse4.1/4.2)    


    #        -msse3 -mssse3 -march=k8 -mtune=k8 -mno-popcnt                                                         <  sse3 build	
   


  
