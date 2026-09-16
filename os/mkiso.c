/* mkiso.c — мінімальний ISO9660 + El Torito (floppy emulation) генератор */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static void le32(uint8_t *p, uint32_t v){ for(int i=0;i<4;i++){p[i]=(uint8_t)(v&0xff);v>>=8;} }
static void be32(uint8_t *p, uint32_t v){ for(int i=3;i>=0;i--){p[i]=(uint8_t)(v&0xff);v>>=8;} }
static void dual32(uint8_t *p, uint32_t v){ le32(p,v); be32(p+4,v); }
static void le16(uint8_t *p, uint16_t v){ p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void be16(uint8_t *p, uint16_t v){ p[0]=(uint8_t)(v>>8); p[1]=(uint8_t)v; }

int main(int argc, char **argv){
    if (argc < 3){ fprintf(stderr, "usage: mkiso out.iso floppy.img\n"); return 1; }
    FILE *img = fopen(argv[2], "rb");
    if (!img){ perror(argv[2]); return 1; }
    fseek(img, 0, SEEK_END); long imgsz = ftell(img); fseek(img, 0, SEEK_SET);
    if (imgsz != 1474560){ fprintf(stderr, "floppy img має бути 1474560 байт (є %ld)\n", imgsz); return 1; }
    uint8_t *fl = (uint8_t *)malloc((size_t)imgsz);
    fread(fl, 1, (size_t)imgsz, img); fclose(img);

    /* layout
       0-15   system area (0)
       16     PVD
       17     Boot Record (El Torito)
       18     Terminator
       19     Boot catalog
       20     Root dir
       21     L path table
       22     M path table
       24+    floppy image (aligned) */
    const int pvd_lba=16, br_lba=17, term_lba=18, bc_lba=19, root_lba=20, lpt_lba=21, mpt_lba=22, img_lba=24;
    const int img_sectors = (int)(imgsz / 2048);
    const int total_sectors = img_lba + img_sectors;

    uint8_t sec[2048]; /* один сектор-буфер */
    FILE *out = fopen(argv[1], "wb");
    if (!out){ perror(argv[1]); return 1; }

    /* 0-15: системна зона */
    memset(sec, 0, sizeof sec);
    for (int i = 0; i < 16; i++) fwrite(sec, 1, sizeof sec, out);

    /* 16: PVD */
    memset(sec, 0, sizeof sec);
    sec[0]=1; memcpy(sec+1,"CD001",5); sec[6]=1;
    strncpy((char*)sec+8, "SOKIL", 32);
    strncpy((char*)sec+40,"SOKIL_OS", 32);
    dual32(sec+80, (uint32_t)total_sectors);
    le16(sec+120, 1); be16(sec+122, 1);          /* volume set size */
    le16(sec+124, 1); be16(sec+126, 1);          /* sequence */
    le16(sec+128, 2048); be16(sec+130, 2048);    /* block size */
    le32(sec+132, 8); be32(sec+136, 8);          /* path table size */
    le32(sec+140, (uint32_t)lpt_lba);            /* L path table */
    le32(sec+144, 0);
    be32(sec+148, (uint32_t)mpt_lba);            /* M path table */
    be32(sec+152, 0);
    /* root dir record (34B) at 156 */
    uint8_t *rd = sec + 156;
    rd[0]=34; rd[1]=0;
    dual32(rd+2, (uint32_t)root_lba);
    dual32(rd+10, 2048);
    rd[18]=2;                                    /* directory */
    rd[25]=1; rd[32]=1; rd[33]=0;
    strncpy((char*)sec+190,"SOKIL OS IMAGES", 128);
    sec[813]=1;                                  /* file structure version */
    fwrite(sec, 1, sizeof sec, out);

    /* 17: Boot Record (El Torito) */
    memset(sec, 0, sizeof sec);
    sec[0]=0; memcpy(sec+1,"CD001",5); sec[6]=1;
    memcpy(sec+7, "EL TORITO SPECIFICATION", 23);
    le32(sec+71, (uint32_t)bc_lba);
    fwrite(sec, 1, sizeof sec, out);

    /* 18: термінатор */
    memset(sec, 0, sizeof sec);
    sec[0]=255; memcpy(sec+1,"CD001",5); sec[6]=1;
    fwrite(sec, 1, sizeof sec, out);

    /* 19: boot catalog */
    memset(sec, 0, sizeof sec);
    sec[0]=1; sec[1]=0;                          /* validation: x86 */
    strncpy((char*)sec+2, "SOKIL OS", 24);
    int sum=0;
    for (int i=0;i<16;i++) sum += sec[i*2] | (sec[i*2+1]<<8);   /* поточні перші 16 слів (без checksum) */
    le16(sec+28, (uint16_t)(-sum));              /* checksum: сума всіх слів = 0 */
    sec[30]=0x55; sec[31]=0xAA;
    /* default/initial entry @32 */
    sec[32]=0x88;                                /* bootable */
    sec[33]=0x02;                                /* 1.44MB floppy emulation */
    le32(sec+40, (uint32_t)img_lba);             /* load RBA */
    fwrite(sec, 1, sizeof sec, out);

    /* 20: root dir — "." та ".." */
    memset(sec, 0, sizeof sec);
    rd = sec;
    rd[0]=34;
    dual32(rd+2,(uint32_t)root_lba);
    dual32(rd+10,2048);
    rd[18]=2; rd[25]=1; rd[32]=1; rd[33]=0;
    rd = sec+34;
    rd[0]=34;
    dual32(rd+2,(uint32_t)root_lba);
    dual32(rd+10,2048);
    rd[18]=2; rd[25]=1; rd[32]=1; rd[33]=0;
    fwrite(sec, 1, sizeof sec, out);

    /* 21: L path table; 22: M path table */
    memset(sec, 0, sizeof sec);
    sec[0]=1; sec[1]=0;                          /* len 1, ext attrs 0 */
    le32(sec+2, 1);                              /* root extent (dir #1) */
    le16(sec+6, 1);                              /* parent = root */
    sec[8]=0;                                    /* di = 0x00 */
    fwrite(sec, 1, sizeof sec, out);
    memset(sec, 0, sizeof sec);
    sec[0]=1; sec[1]=0;
    be32(sec+2, 1);
    be16(sec+6, 1);
    sec[8]=0;
    fwrite(sec, 1, sizeof sec, out);

    /* 23: 1 сектор вирівнювання (нулі) */
    memset(sec, 0, sizeof sec);
    fwrite(sec, 1, sizeof sec, out);

    /* 24+: floppy image */
    fwrite(fl, 1, (size_t)imgsz, out);
    free(fl);
    fclose(out);
    printf("OK: %s (%d секторів, image @%d)\n", argv[1], total_sectors, img_lba);
    return 0;
}