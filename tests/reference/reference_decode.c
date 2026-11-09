/* Driver only; links to unmodified ISO/IEC 21122-5 edition-3 software. */
#include "libjxs.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc,char **argv) {
    if(argc!=3) return 2;
    FILE *in=fopen(argv[1],"rb"); if(!in) return 3;
    if(fseek(in,0,SEEK_END)) return 4;
    long n=ftell(in); if(n<=0 || fseek(in,0,SEEK_SET)) return 4;
    uint8_t *data=malloc((size_t)n); if(!data) return 5;
    if(fread(data,1,n,in)!=(size_t)n) return 6;
    fclose(in);
    xs_config_t cfg={0}; xs_image_t im={0};
    if(!xs_dec_probe(data,n,&cfg,&im)) return 7;
    cfg.verbose=0;
    if(!xs_config_validate(&cfg,&im)) return 8;
    if(!xs_allocate_image(&im,false)) return 9;
    xs_dec_context_t *ctx=xs_dec_init(&cfg,&im); if(!ctx) return 10;
    if(!xs_dec_bitstream(ctx,0,data,n,&im)) return 11;
    if(!xs_dec_postprocess_image(&cfg,&im)) return 12;
    FILE *out=fopen(argv[2],"wb"); if(!out) return 13;
    for(int c=0;c<im.ncomps;++c) {
        int w=(im.width+im.sx[c]-1)/im.sx[c];
        int h=(im.height+im.sy[c]-1)/im.sy[c];
        for(int i=0;i<w*h;++i) {
            uint32_t v=(uint32_t)im.comps_array[c][i];
            if(fputc(v&255,out)==EOF) return 14;
            if(im.depth>8 && fputc((v>>8)&255,out)==EOF) return 14;
        }
    }
    if(fclose(out)) return 15;
    xs_dec_close(ctx); xs_free_image(&im); free(data); return 0;
}
