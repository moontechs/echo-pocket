idf_component_register(
    SRCS "wav_to_mp3.c"
         "shine/bitstream.c"
         "shine/huffman.c"
         "shine/l3bitstream.c"
         "shine/l3loop.c"
         "shine/l3mdct.c"
         "shine/l3subband.c"
         "shine/layer3.c"
         "shine/reservoir.c"
         "shine/tables.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "shine"
    REQUIRES recorder
)
