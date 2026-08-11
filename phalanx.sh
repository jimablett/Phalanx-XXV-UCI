rm *.o
cls
gcc -o phalanx bcreate.c book.c data.c endgame.c evaluate.c genmoves.c hash.c io.c killers.c learn.c levels.c moving.c phalanx.c search.c static.c -w -s -pipe -O3 -DNDEBUG -flto -fwhole-program 
 










