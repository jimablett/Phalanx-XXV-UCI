all:
	clang -o phalanx *.c ./fathom/src/tbprobe.c -O3 -flto=auto -ftree-vectorize -funroll-loops \
	-fno-exceptions -ffast-math -static -static-libgcc -DNDEBUG -DNDEBUG_TERMINAL -DNDEBUG_MATE -finline-functions -pipe -Wall -std=gnu23  \
	-fstrict-aliasing -fomit-frame-pointer -lm -fuse-ld=lld -MMD -MP -s -funsafe-math-optimizations -fsee \
	-march=k8 -mtune=core2
	
	
	
	#        use same profiling data for all
	
	
	#       -fprofile-instr-generate -fcoverage-mapping                                                             <   before -o
	
	#        llvm-profdata merge -output=default.profdata *.profraw                                                 <  enter on command line
 
    #       -fprofile-use=default.profdata                                                                          <   before -o
	
    
    #       -march=silvermont -mtune=k8                                                                             <  for popcount builds (with sse4.1/4.2)    


    #       -march=k8 -mtune=core2                                                                                  <  sse3 build	
   


  
