/**
 * NILM 文件压缩工具 - 支持 data/ -> out/compressed/ -> out/reconstructed/
 */

#define COMPRESSOR_LIB
#include "compress/compress_data.c"
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>

#define OUT_COMPRESSED_DIR "out/compressed"
#define OUT_RECONSTRUCTED_DIR "out/reconstructed"
#define DATA_DIR "data"

typedef struct {
    char** timestamps;
    double *Va, *Vb, *Vc, *Ia, *Ib, *Ic;
    int n;
    int sampleRate;
} WaveData;

static int ensure_dir(const char* path){
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for(char* p=tmp+1; *p; p++){
        if(*p=='/'){ *p='\0'; mkdir(tmp, 0755); *p='/'; }
    }
    mkdir(tmp, 0755);
    return 0;
}

static double parse_sec_from_ts(const char* ts){
    const char* last_space = strrchr(ts, ' ');
    if(!last_space) return 0;
    const char* time_part = last_space+1;
    const char* colon1 = strchr(time_part, ':');
    if(!colon1) return 0;
    const char* colon2 = strchr(colon1+1, ':');
    if(!colon2) return 0;
    double sec = atof(colon2+1);
    return sec;
}

static int estimate_sample_rate_from_csv(const char* path){
    FILE* f = fopen(path, "r");
    if(!f) return 10000;
    char line[2048];
    if(!fgets(line, sizeof(line), f)){ fclose(f); return 10000; }
    if(!fgets(line, sizeof(line), f)){ fclose(f); return 10000; }
    char ts1[128]={0};
    sscanf(line, "%127[^,]", ts1);
    if(!fgets(line, sizeof(line), f)){ fclose(f); return 10000; }
    char ts2[128]={0};
    sscanf(line, "%127[^,]", ts2);
    fclose(f);
    double s1 = parse_sec_from_ts(ts1);
    double s2 = parse_sec_from_ts(ts2);
    double diff = s2 - s1;
    if(diff<0) diff+=60;
    if(diff<1e-9 || diff>1) return 10000;
    int sr = (int)round(1.0/diff);
    if(sr<1000 || sr>50000) return 10000;
    return sr;
}

static WaveData* read_csv_wave(const char* path){
    FILE* f = fopen(path, "r");
    if(!f){ perror(path); return NULL; }
    char line[4096];
    if(!fgets(line, sizeof(line), f)){ fclose(f); return NULL; }
    int idx_ts=-1, idx_ua=-1, idx_ia=-1, idx_ub=-1, idx_ib=-1, idx_uc=-1, idx_ic=-1;
    {
        char header_copy[4096];
        strncpy(header_copy, line, sizeof(header_copy));
        char* token = strtok(header_copy, ",");
        int col=0;
        while(token){
            while(*token==' '||*token=='\t') token++;
            char* end=token+strlen(token)-1;
            while(end>token && (*end==' '||*end=='\r'||*end=='\n')){*end='\0'; end--;}
            if(strcmp(token,"timestamp")==0) idx_ts=col;
            else if(strcmp(token,"UA")==0) idx_ua=col;
            else if(strcmp(token,"IA")==0) idx_ia=col;
            else if(strcmp(token,"UB")==0) idx_ub=col;
            else if(strcmp(token,"IB")==0) idx_ib=col;
            else if(strcmp(token,"UC")==0) idx_uc=col;
            else if(strcmp(token,"IC")==0) idx_ic=col;
            else if(strcmp(token,"Va")==0) idx_ua=col;
            else if(strcmp(token,"Ia")==0) idx_ia=col;
            else if(strcmp(token,"Vb")==0) idx_ub=col;
            else if(strcmp(token,"Ib")==0) idx_ib=col;
            else if(strcmp(token,"Vc")==0) idx_uc=col;
            else if(strcmp(token,"Ic")==0) idx_ic=col;
            token=strtok(NULL, ",");
            col++;
        }
    }
    if(idx_ts<0 || idx_ua<0 || idx_ia<0 || idx_ub<0 || idx_ib<0 || idx_uc<0 || idx_ic<0){
        fprintf(stderr, "CSV header 解析失败: %s, 使用默认顺序\n", path);
        if(idx_ts<0) idx_ts=0;
        if(idx_ua<0) idx_ua=1;
        if(idx_ia<0) idx_ia=2;
        if(idx_ub<0) idx_ub=3;
        if(idx_ib<0) idx_ib=4;
        if(idx_uc<0) idx_uc=5;
        if(idx_ic<0) idx_ic=6;
    }

    int capacity=1024;
    WaveData* wd=(WaveData*)calloc(1,sizeof(WaveData));
    wd->timestamps=(char**)malloc(capacity*sizeof(char*));
    wd->Va=(double*)malloc(capacity*sizeof(double));
    wd->Vb=(double*)malloc(capacity*sizeof(double));
    wd->Vc=(double*)malloc(capacity*sizeof(double));
    wd->Ia=(double*)malloc(capacity*sizeof(double));
    wd->Ib=(double*)malloc(capacity*sizeof(double));
    wd->Ic=(double*)malloc(capacity*sizeof(double));
    wd->n=0;

    while(fgets(line, sizeof(line), f)){
        if(strlen(line)<5) continue;
        char* fields[32];
        int nfield=0;
        char* p=line;
        char* start=p;
        while(*p && nfield<32){
            if(*p==',' || *p=='\n' || *p=='\r'){
                *p='\0';
                fields[nfield++]=start;
                start=p+1;
            }
            p++;
        }
        if(nfield<=idx_ic) continue;
        if(wd->n>=capacity){
            capacity*=2;
            wd->timestamps=(char**)realloc(wd->timestamps, capacity*sizeof(char*));
            wd->Va=(double*)realloc(wd->Va, capacity*sizeof(double));
            wd->Vb=(double*)realloc(wd->Vb, capacity*sizeof(double));
            wd->Vc=(double*)realloc(wd->Vc, capacity*sizeof(double));
            wd->Ia=(double*)realloc(wd->Ia, capacity*sizeof(double));
            wd->Ib=(double*)realloc(wd->Ib, capacity*sizeof(double));
            wd->Ic=(double*)realloc(wd->Ic, capacity*sizeof(double));
        }
        wd->timestamps[wd->n]=strdup(fields[idx_ts]);
        wd->Va[wd->n]=atof(fields[idx_ua]);
        wd->Ia[wd->n]=atof(fields[idx_ia]);
        wd->Vb[wd->n]=atof(fields[idx_ub]);
        wd->Ib[wd->n]=atof(fields[idx_ib]);
        wd->Vc[wd->n]=atof(fields[idx_uc]);
        wd->Ic[wd->n]=atof(fields[idx_ic]);
        wd->n++;
    }
    fclose(f);
    wd->sampleRate = estimate_sample_rate_from_csv(path);
    printf("读取 %s: %d 行, 估算采样率 %d Hz\n", path, wd->n, wd->sampleRate);
    return wd;
}

static void free_wave_data(WaveData* wd){
    if(!wd) return;
    for(int i=0;i<wd->n;i++) free(wd->timestamps[i]);
    free(wd->timestamps);
    free(wd->Va); free(wd->Vb); free(wd->Vc);
    free(wd->Ia); free(wd->Ib); free(wd->Ic);
    free(wd);
}

static int write_compressed_bin(const char* outPath, WaveData* wd, int pointsPerCycle, CompressionConfig* config){
    FILE* f=fopen(outPath, "wb");
    if(!f){ perror(outPath); return -1; }
    fwrite("NILM",1,4,f);
    uint32_t version=1;
    fwrite(&version,4,1,f);
    int32_t sr=wd->sampleRate;
    int32_t ppc=pointsPerCycle;
    int32_t numFrames= wd->n / pointsPerCycle;
    int32_t flags=0;
    if(config->enableZeroOptimization) flags|=1;
    if(config->enableDictEncoding) flags|=2;
    if(config->enableFrameDiff) flags|=4;
    fwrite(&sr,4,1,f);
    fwrite(&ppc,4,1,f);
    fwrite(&numFrames,4,1,f);
    fwrite(&flags,4,1,f);

    size_t totalCompressed=0;
    HybridCompressor comp;
    compressor_init(&comp, config);

    for(int fr=0; fr<numFrames; fr++){
        int off=fr*pointsPerCycle;
        CompressedData cd = compress_three_phase(&comp,
            wd->Va+off, wd->Vb+off, wd->Vc+off,
            wd->Ia+off, wd->Ib+off, wd->Ic+off,
            pointsPerCycle);
        uint32_t sizes[6]={ (uint32_t)cd.sizeVa, (uint32_t)cd.sizeVb, (uint32_t)cd.sizeVc,
                            (uint32_t)cd.sizeIa, (uint32_t)cd.sizeIb, (uint32_t)cd.sizeIc };
        fwrite(sizes,4,6,f);
        if(cd.sizeVa) fwrite(cd.Va,1,cd.sizeVa,f);
        if(cd.sizeVb) fwrite(cd.Vb,1,cd.sizeVb,f);
        if(cd.sizeVc) fwrite(cd.Vc,1,cd.sizeVc,f);
        if(cd.sizeIa) fwrite(cd.Ia,1,cd.sizeIa,f);
        if(cd.sizeIb) fwrite(cd.Ib,1,cd.sizeIb,f);
        if(cd.sizeIc) fwrite(cd.Ic,1,cd.sizeIc,f);
        totalCompressed+= cd.totalSize + 6*4;
        free_compressed_data(&cd);
    }
    fclose(f);
    size_t originalSize = (size_t)wd->n * 6 * sizeof(double);
    double ratio = (double)originalSize / (totalCompressed?totalCompressed:1);
    printf("压缩文件已写入 %s: 原始 %zu 字节, 压缩 %zu 字节 (含头), 压缩比 %.2f:1\n", outPath, originalSize, totalCompressed, ratio);
    return 0;
}

static int write_reconstructed_csv(const char* outPath, WaveData* wd, int pointsPerCycle, CompressionConfig* config){
    FILE* f=fopen(outPath, "w");
    if(!f){ perror(outPath); return -1; }
    fprintf(f, "timestamp,UA,IA,UB,IB,UC,IC\n");
    HybridCompressor compEnc, compDec;
    compressor_init(&compEnc, config);
    compressor_init(&compDec, config);

    int numFrames = wd->n / pointsPerCycle;
    double totalSim=0;
    int simCount=0;

    for(int fr=0; fr<numFrames; fr++){
        int off=fr*pointsPerCycle;
        CompressedData cd = compress_three_phase(&compEnc,
            wd->Va+off, wd->Vb+off, wd->Vc+off,
            wd->Ia+off, wd->Ib+off, wd->Ic+off,
            pointsPerCycle);
        double *Va_r=malloc(pointsPerCycle*sizeof(double));
        double *Vb_r=malloc(pointsPerCycle*sizeof(double));
        double *Vc_r=malloc(pointsPerCycle*sizeof(double));
        double *Ia_r=malloc(pointsPerCycle*sizeof(double));
        double *Ib_r=malloc(pointsPerCycle*sizeof(double));
        double *Ic_r=malloc(pointsPerCycle*sizeof(double));
        int outN;
        decompress_three_phase(&compDec, &cd, Va_r, Vb_r, Vc_r, Ia_r, Ib_r, Ic_r, &outN);
        totalSim+=calculate_similarity(wd->Va+off, Va_r, pointsPerCycle);
        simCount++;
        for(int i=0;i<pointsPerCycle;i++){
            int idx=off+i;
            if(idx>=wd->n) break;
            fprintf(f, "%s,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f\n",
                wd->timestamps[idx],
                Va_r[i], Ia_r[i], Vb_r[i], Ib_r[i], Vc_r[i], Ic_r[i]);
        }
        free(Va_r); free(Vb_r); free(Vc_r); free(Ia_r); free(Ib_r); free(Ic_r);
        free_compressed_data(&cd);
    }
    fclose(f);
    if(simCount>0) printf("复原文件已写入 %s, 平均相似度 %.4f%%\n", outPath, totalSim/simCount*100);
    else printf("复原文件已写入 %s\n", outPath);
    return 0;
}

static void process_single_csv(const char* inputPath, const char* compOutPath, const char* reconOutPath, int mode, bool useZero){
    WaveData* wd = read_csv_wave(inputPath);
    if(!wd) return;
    if(wd->n<10){ printf("文件 %s 行数过少，跳过\n", inputPath); free_wave_data(wd); return; }
    int ppc = wd->sampleRate / 50;
    if(ppc>MAX_POINTS_PER_CYCLE) ppc=MAX_POINTS_PER_CYCLE;
    if(ppc<16) ppc=16;
    printf("使用 pointsPerCycle=%d (sampleRate=%d)\n", ppc, wd->sampleRate);
    CompressionConfig config = create_config(wd->sampleRate, mode);
    config.enableZeroOptimization = useZero;
    config.enableFrameDiff = true;
    config.enableDictEncoding = true;

    ensure_dir(OUT_COMPRESSED_DIR);
    ensure_dir(OUT_RECONSTRUCTED_DIR);

    write_compressed_bin(compOutPath, wd, ppc, &config);
    write_reconstructed_csv(reconOutPath, wd, ppc, &config);

    free_wave_data(wd);
}

static void process_data_directory(int mode, bool useZero){
    DIR* d = opendir(DATA_DIR);
    if(!d){ printf("data 目录不存在，创建空目录\n"); ensure_dir(DATA_DIR); return; }
    struct dirent* entry;
    while((entry=readdir(d))!=NULL){
        if(entry->d_name[0]=='.') continue;
        char* dot=strrchr(entry->d_name, '.');
        if(!dot) continue;
        if(strcmp(dot, ".csv")!=0 && strcmp(dot, ".CSV")!=0) continue;
        char inputPath[1024];
        snprintf(inputPath, sizeof(inputPath), "%s/%s", DATA_DIR, entry->d_name);
        char baseName[512];
        strncpy(baseName, entry->d_name, sizeof(baseName));
        char* ext=strrchr(baseName, '.'); if(ext) *ext='\0';
        char compOut[1024], reconOut[1024];
        snprintf(compOut, sizeof(compOut), "%s/%s.bin", OUT_COMPRESSED_DIR, baseName);
        snprintf(reconOut, sizeof(reconOut), "%s/%s_reconstructed.csv", OUT_RECONSTRUCTED_DIR, baseName);
        printf("\n=== 处理文件: %s ===\n", inputPath);
        process_single_csv(inputPath, compOut, reconOut, mode, useZero);
    }
    closedir(d);
}

static void print_usage(const char* prog){
    printf("用法: %s [选项]\n", prog);
    printf("  --mode <0|1|2>        压缩模式 0=极致 1=平衡(默认) 2=高质量\n");
    printf("  --zero-opt <0|1>      是否启用零序优化 1=启用(默认) 0=禁用\n");
    printf("  --input <file>        指定单个输入CSV文件 (默认处理 data/ 下所有CSV)\n");
    printf("示例:\n");
    printf("  %s --mode 1 --zero-opt 1\n", prog);
    printf("  %s --input data/wave1.csv --mode 0\n", prog);
}

int main(int argc, char* argv[]){
    int mode=COMPRESS_MODE_BALANCED;
    bool useZero=true;
    const char* singleInput=NULL;

    for(int i=1;i<argc;i++){
        if(strcmp(argv[i],"--mode")==0 && i+1<argc){
            mode=atoi(argv[++i]);
        }else if(strcmp(argv[i],"--zero-opt")==0 && i+1<argc){
            useZero=atoi(argv[++i])!=0;
        }else if(strcmp(argv[i],"--input")==0 && i+1<argc){
            singleInput=argv[++i];
        }else if(strcmp(argv[i],"--help")==0 || strcmp(argv[i],"-h")==0){
            print_usage(argv[0]);
            return 0;
        }
    }

    ensure_dir(OUT_COMPRESSED_DIR);
    ensure_dir(OUT_RECONSTRUCTED_DIR);
    ensure_dir(DATA_DIR);

    if(singleInput){
        char baseName[512];
        const char* slash=strrchr(singleInput,'/');
        const char* name=slash?slash+1:singleInput;
        strncpy(baseName, name, sizeof(baseName));
        char* ext=strrchr(baseName,'.'); if(ext) *ext='\0';
        char compOut[1024], reconOut[1024];
        snprintf(compOut, sizeof(compOut), "%s/%s.bin", OUT_COMPRESSED_DIR, baseName);
        snprintf(reconOut, sizeof(reconOut), "%s/%s_reconstructed.csv", OUT_RECONSTRUCTED_DIR, baseName);
        printf("处理单文件: %s\n", singleInput);
        process_single_csv(singleInput, compOut, reconOut, mode, useZero);
    }else{
        process_data_directory(mode, useZero);
    }

    printf("\n所有文件处理完成。\n");
    printf("压缩文件位于: %s/\n", OUT_COMPRESSED_DIR);
    printf("复原文件位于: %s/\n", OUT_RECONSTRUCTED_DIR);
    return 0;
}
