// Exercise real libc -> WASI -> JS File reads, not a mocked filesystem.
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
int range_stdio(const char* path, int offset) {
    FILE* input=fopen(path,"rb");if(!input)return -1;
    unsigned char data[5];
    if(fseek(input,offset,SEEK_SET) || fread(data,1,5,input)!=5){fclose(input);return -2;}
    fclose(input);int sum=0;for(int i=0;i<5;++i)sum+=data[i];return sum;
}
int range_pread(const char* path, int offset) {
    int fd=open(path,O_RDONLY);if(fd<0)return -1;
    unsigned char data[5];int count=pread(fd,data,5,offset);close(fd);
    if(count!=5)return -2;
    int sum=0;for(int i=0;i<5;++i)sum+=data[i];return sum;
}
#ifdef GW_TEST_HTSLIB
#include <htslib/sam.h>
int range_bam(const char* path) {
    samFile* input=sam_open(path,"r");if(!input)return -1;
    sam_hdr_t* header=sam_hdr_read(input);
    hts_idx_t* index=header?sam_index_load(input,path):NULL;
    if(!header || !index){if(header)sam_hdr_destroy(header);sam_close(input);return -2;}
    hts_itr_t* iter=sam_itr_queryi(index,sam_hdr_name2tid(header,"chr1"),9900,10300);
    bam1_t* record=bam_init1();int count=0,status=-1;
    if(iter)while((status=sam_itr_next(input,iter,record))>=0)++count;
    bam_destroy1(record);if(iter)hts_itr_destroy(iter);hts_idx_destroy(index);sam_hdr_destroy(header);sam_close(input);
    return status < -1 ? -3 : count;
}
#endif
