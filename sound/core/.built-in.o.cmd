cmd_sound/core/built-in.o :=  arm-eabi-ld -EL    -r -o sound/core/built-in.o sound/core/snd.o sound/core/snd-hwdep.o sound/core/snd-timer.o sound/core/snd-hrtimer.o sound/core/snd-pcm.o sound/core/snd-page-alloc.o sound/core/snd-rawmidi.o 
