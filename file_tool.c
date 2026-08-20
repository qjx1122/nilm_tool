/**
 * NILM 文件压缩工具 - 支持 data/ -> out/compressed/ -> out/reconstructed/
 * 新增：批量验证，统计每个文件的压缩比和相似度指标 -> out/verification_report.csv
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
#define OUT_REPORT_DIR "out"
#define DATA_DIR "data"

typedef struct {
    char** timestamps;
    double *Va, *Vb, *Vc, *Ia, *Ib, *Ic;
    int n;
    int sampleRate;
} WaveData;

typedef struct {
    char filename[256];
    int rows;
    int sampleRate;
    int pointsPerCycle;
    int numFrames;
    size_t originalBytes;
    size_t compressedBytes;
    double compressionRatio;
    double avgSimilarity;
    double similarities[6];
    double avgRMSE;
    double avgSNR;
    double perChannelRMSE[6];
    double perChannelSNR[6];
} FileMetrics;

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
    return atof(colon2+1);
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

/* 核心验证函数：压缩并解压，计算详细指标 */
static FileMetrics verify_and_compress(const char* inputPath, const char* compOutPath, const char* reconOutPath, int mode, bool useZero){
    FileMetrics metrics;
    memset(&metrics, 0, sizeof(metrics));
    strncpy(metrics.filename, inputPath, sizeof(metrics.filename)-1);

    WaveData* wd = read_csv_wave(inputPath);
    if(!wd) return metrics;
    if(wd->n<10){ printf("文件 %s 行数过少，跳过\n", inputPath); free_wave_data(wd); return metrics; }

    int ppc = wd->sampleRate / 50;
    if(ppc>MAX_POINTS_PER_CYCLE) ppc=MAX_POINTS_PER_CYCLE;
    if(ppc<16) ppc=16;
    metrics.rows = wd->n;
    metrics.sampleRate = wd->sampleRate;
    metrics.pointsPerCycle = ppc;
    metrics.numFrames = wd->n / ppc;
    metrics.originalBytes = (size_t)wd->n * 6 * sizeof(double);

    printf("使用 pointsPerCycle=%d (sampleRate=%d) numFrames=%d\n", ppc, wd->sampleRate, metrics.numFrames);
    CompressionConfig config = create_config(wd->sampleRate, mode);
    config.enableZeroOptimization = useZero;
    config.enableFrameDiff = true;
    config.enableDictEncoding = true;

    // 打开压缩文件
    FILE* fcomp = fopen(compOutPath, "wb");
    FILE* frec = fopen(reconOutPath, "w");
    if(!fcomp || !frec){
        if(fcomp) fclose(fcomp);
        if(frec) fclose(frec);
        free_wave_data(wd);
        return metrics;
    }
    // 写头
    fwrite("NILM",1,4,fcomp);
    uint32_t version=1;
    fwrite(&version,4,1,fcomp);
    int32_t sr=wd->sampleRate, ppc_i=ppc, numFrames=metrics.numFrames, flags=0;
    if(config.enableZeroOptimization) flags|=1;
    if(config.enableDictEncoding) flags|=2;
    if(config.enableFrameDiff) flags|=4;
    fwrite(&sr,4,1,fcomp);
    fwrite(&ppc_i,4,1,fcomp);
    fwrite(&numFrames,4,1,fcomp);
    fwrite(&flags,4,1,fcomp);
    fprintf(frec, "timestamp,UA,IA,UB,IB,UC,IC\n");

    HybridCompressor compEnc, compDec;
    compressor_init(&compEnc, &config);
    compressor_init(&compDec, &config);

    size_t totalCompressed = 0;
    double totalSimAll=0, totalRMSEAll=0, totalSNRAll=0;
    double simPerCh[6]={0}, rmsePerCh[6]={0}, snrPerCh[6]={0};
    int frameCount=0;

    for(int fr=0; fr<metrics.numFrames; fr++){
        int off=fr*ppc;
        CompressedData cd = compress_three_phase(&compEnc,
            wd->Va+off, wd->Vb+off, wd->Vc+off,
            wd->Ia+off, wd->Ib+off, wd->Ic+off,
            ppc);
        uint32_t sizes[6]={ (uint32_t)cd.sizeVa, (uint32_t)cd.sizeVb, (uint32_t)cd.sizeVc,
                            (uint32_t)cd.sizeIa, (uint32_t)cd.sizeIb, (uint32_t)cd.sizeIc };
        fwrite(sizes,4,6,fcomp);
        if(cd.sizeVa) fwrite(cd.Va,1,cd.sizeVa,fcomp);
        if(cd.sizeVb) fwrite(cd.Vb,1,cd.sizeVb,fcomp);
        if(cd.sizeVc) fwrite(cd.Vc,1,cd.sizeVc,fcomp);
        if(cd.sizeIa) fwrite(cd.Ia,1,cd.sizeIa,fcomp);
        if(cd.sizeIb) fwrite(cd.Ib,1,cd.sizeIb,fcomp);
        if(cd.sizeIc) fwrite(cd.Ic,1,cd.sizeIc,fcomp);
        totalCompressed+= cd.totalSize + 6*4;

        double *Va_r=malloc(ppc*sizeof(double));
        double *Vb_r=malloc(ppc*sizeof(double));
        double *Vc_r=malloc(ppc*sizeof(double));
        double *Ia_r=malloc(ppc*sizeof(double));
        double *Ib_r=malloc(ppc*sizeof(double));
        double *Ic_r=malloc(ppc*sizeof(double));
        int outN;
        decompress_three_phase(&compDec, &cd, Va_r, Vb_r, Vc_r, Ia_r, Ib_r, Ic_r, &outN);

        // 计算该帧的相似度等
        double sims[6];
        sims[0]=calculate_similarity(wd->Va+off, Va_r, ppc);
        sims[1]=calculate_similarity(wd->Vb+off, Vb_r, ppc);
        sims[2]=calculate_similarity(wd->Vc+off, Vc_r, ppc);
        sims[3]=calculate_similarity(wd->Ia+off, Ia_r, ppc);
        sims[4]=calculate_similarity(wd->Ib+off, Ib_r, ppc);
        sims[5]=calculate_similarity(wd->Ic+off, Ic_r, ppc);
        double rmses[6];
        rmses[0]=calculate_rmse(wd->Va+off, Va_r, ppc);
        rmses[1]=calculate_rmse(wd->Vb+off, Vb_r, ppc);
        rmses[2]=calculate_rmse(wd->Vc+off, Vc_r, ppc);
        rmses[3]=calculate_rmse(wd->Ia+off, Ia_r, ppc);
        rmses[4]=calculate_rmse(wd->Ib+off, Ib_r, ppc);
        rmses[5]=calculate_rmse(wd->Ic+off, Ic_r, ppc);
        double snrs[6];
        snrs[0]=calculate_snr(wd->Va+off, Va_r, ppc);
        snrs[1]=calculate_snr(wd->Vb+off, Vb_r, ppc);
        snrs[2]=calculate_snr(wd->Vc+off, Vc_r, ppc);
        snrs[3]=calculate_snr(wd->Ia+off, Ia_r, ppc);
        snrs[4]=calculate_snr(wd->Ib+off, Ib_r, ppc);
        snrs[5]=calculate_snr(wd->Ic+off, Ic_r, ppc);

        for(int ch=0; ch<6; ch++){
            simPerCh[ch]+=sims[ch];
            rmsePerCh[ch]+=rmses[ch];
            snrPerCh[ch]+=snrs[ch];
        }
        double avgSimThis = (sims[0]+sims[1]+sims[2]+sims[3]+sims[4]+sims[5])/6.0;
        double avgRMSEThis = (rmses[0]+rmses[1]+rmses[2]+rmses[3]+rmses[4]+rmses[5])/6.0;
        double avgSNRThis = (snrs[0]+snrs[1]+snrs[2]+snrs[3]+snrs[4]+snrs[5])/6.0;
        totalSimAll+=avgSimThis;
        totalRMSEAll+=avgRMSEThis;
        totalSNRAll+=avgSNRThis;
        frameCount++;

        for(int i=0;i<ppc;i++){
            int idx=off+i;
            if(idx>=wd->n) break;
            fprintf(frec, "%s,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f\n",
                wd->timestamps[idx],
                Va_r[i], Ia_r[i], Vb_r[i], Ib_r[i], Vc_r[i], Ic_r[i]);
        }

        free(Va_r); free(Vb_r); free(Vc_r); free(Ia_r); free(Ib_r); free(Ic_r);
        free_compressed_data(&cd);
    }

    fclose(fcomp);
    fclose(frec);

    metrics.compressedBytes = totalCompressed + 20; // 含20字节文件头
    metrics.compressionRatio = (double)metrics.originalBytes / (metrics.compressedBytes?metrics.compressedBytes:1);
    if(frameCount>0){
        metrics.avgSimilarity = totalSimAll / frameCount;
        metrics.avgRMSE = totalRMSEAll / frameCount;
        metrics.avgSNR = totalSNRAll / frameCount;
        for(int ch=0; ch<6; ch++){
            metrics.similarities[ch]=simPerCh[ch]/frameCount;
            metrics.perChannelRMSE[ch]=rmsePerCh[ch]/frameCount;
            metrics.perChannelSNR[ch]=snrPerCh[ch]/frameCount;
        }
    }

    printf("压缩文件已写入 %s: 原始 %zu 字节, 压缩 %zu 字节, 压缩比 %.2f:1\n", compOutPath, metrics.originalBytes, metrics.compressedBytes, metrics.compressionRatio);
    printf("复原文件已写入 %s, 平均相似度 %.4f%% RMSE %.6f SNR %.2f dB\n", reconOutPath, metrics.avgSimilarity*100, metrics.avgRMSE, metrics.avgSNR);

    free_wave_data(wd);
    return metrics;
}

static void process_data_directory(int mode, bool useZero){
    DIR* d = opendir(DATA_DIR);
    if(!d){ printf("data 目录不存在，创建空目录\n"); ensure_dir(DATA_DIR); return; }

    // 收集所有 csv 文件
    char fileList[64][512];
    int fileCount=0;
    struct dirent* entry;
    while((entry=readdir(d))!=NULL && fileCount<64){
        if(entry->d_name[0]=='.') continue;
        char* dot=strrchr(entry->d_name, '.');
        if(!dot) continue;
        if(strcmp(dot, ".csv")!=0 && strcmp(dot, ".CSV")!=0) continue;
        strncpy(fileList[fileCount], entry->d_name, 511);
        fileCount++;
    }
    closedir(d);

    if(fileCount==0){ printf("data/ 下未找到 CSV 文件\n"); return; }

    // 打开汇总报告
    ensure_dir(OUT_REPORT_DIR);
    char reportPath[1024];
    snprintf(reportPath, sizeof(reportPath), "%s/verification_report.csv", OUT_REPORT_DIR);
    FILE* freport = fopen(reportPath, "w");
    if(freport){
        fprintf(freport, "filename,rows,sampleRate,pointsPerCycle,numFrames,originalBytes,compressedBytes,compressionRatio,avgSimilarity,sim_Va,sim_Vb,sim_Vc,sim_Ia,sim_Ib,sim_Ic,avgRMSE,avgSNR\n");
    }
    char reportTxtPath[1024];
    snprintf(reportTxtPath, sizeof(reportTxtPath), "%s/verification_report.txt", OUT_REPORT_DIR);
    FILE* freportTxt = fopen(reportTxtPath, "w");
    if(freportTxt){
        fprintf(freportTxt, "NILM 压缩验证报告\n");
        fprintf(freportTxt, "========================================\n");
        fprintf(freportTxt, "模式: %s (%d)  零序优化: %s\n", mode==0?"ULTRA":mode==1?"BALANCED":"HIGH", mode, useZero?"启用":"禁用");
        fprintf(freportTxt, "时间: %s %s\n", __DATE__, __TIME__);
        fprintf(freportTxt, "========================================\n\n");
    }

    double totalOrig=0, totalComp=0, totalSim=0;
    for(int i=0;i<fileCount;i++){
        char inputPath[1024], compOut[1024], reconOut[1024];
        snprintf(inputPath, sizeof(inputPath), "%s/%s", DATA_DIR, fileList[i]);
        char baseName[512];
        strncpy(baseName, fileList[i], sizeof(baseName));
        char* ext=strrchr(baseName, '.'); if(ext) *ext='\0';
        snprintf(compOut, sizeof(compOut), "%s/%s.bin", OUT_COMPRESSED_DIR, baseName);
        snprintf(reconOut, sizeof(reconOut), "%s/%s_reconstructed.csv", OUT_RECONSTRUCTED_DIR, baseName);
        printf("\n=== 处理文件 [%d/%d]: %s ===\n", i+1, fileCount, inputPath);
        FileMetrics m = verify_and_compress(inputPath, compOut, reconOut, mode, useZero);

        totalOrig+=m.originalBytes;
        totalComp+=m.compressedBytes;
        totalSim+=m.avgSimilarity;

        if(freport){
            fprintf(freport, "%s,%d,%d,%d,%d,%zu,%zu,%.2f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.2f\n",
                fileList[i], m.rows, m.sampleRate, m.pointsPerCycle, m.numFrames,
                m.originalBytes, m.compressedBytes, m.compressionRatio,
                m.avgSimilarity, m.similarities[0], m.similarities[1], m.similarities[2],
                m.similarities[3], m.similarities[4], m.similarities[5],
                m.avgRMSE, m.avgSNR);
        }
        if(freportTxt){
            fprintf(freportTxt, "文件: %s\n", fileList[i]);
            fprintf(freportTxt, "  行数: %d  采样率: %d Hz  每周期点数: %d  帧数: %d\n", m.rows, m.sampleRate, m.pointsPerCycle, m.numFrames);
            fprintf(freportTxt, "  原始: %zu 字节  压缩: %zu 字节  压缩比: %.2f:1\n", m.originalBytes, m.compressedBytes, m.compressionRatio);
            fprintf(freportTxt, "  平均相似度: %.4f%%  RMSE: %.6f  SNR: %.2f dB\n", m.avgSimilarity*100, m.avgRMSE, m.avgSNR);
            fprintf(freportTxt, "  各通道相似度: Va %.4f%% Vb %.4f%% Vc %.4f%% Ia %.4f%% Ib %.4f%% Ic %.4f%%\n",
                m.similarities[0]*100, m.similarities[1]*100, m.similarities[2]*100,
                m.similarities[3]*100, m.similarities[4]*100, m.similarities[5]*100);
            fprintf(freportTxt, "  各通道 RMSE: Va %.4f Vb %.4f Vc %.4f Ia %.4f Ib %.4f Ic %.4f\n",
                m.perChannelRMSE[0], m.perChannelRMSE[1], m.perChannelRMSE[2],
                m.perChannelRMSE[3], m.perChannelRMSE[4], m.perChannelRMSE[5]);
            fprintf(freportTxt, "\n");
        }
    }

    if(fileCount>0){
        double avgRatio = totalOrig / (totalComp?totalComp:1);
        double avgSim = totalSim / fileCount;
        printf("\n========================================\n");
        printf("汇总: %d 个文件\n", fileCount);
        printf("总原始: %.2f MB  总压缩: %.2f MB  平均压缩比: %.2f:1  平均相似度: %.4f%%\n",
            totalOrig/1024/1024, totalComp/1024/1024, avgRatio, avgSim*100);
        printf("详细报告已写入: %s, %s\n", reportPath, reportTxtPath);
        if(freportTxt){
            fprintf(freportTxt, "========================================\n");
            fprintf(freportTxt, "汇总: %d 个文件\n", fileCount);
            fprintf(freportTxt, "总原始: %.2f MB  总压缩: %.2f MB  平均压缩比: %.2f:1  平均相似度: %.4f%%\n",
                totalOrig/1024/1024, totalComp/1024/1024, avgRatio, avgSim*100);
        }
    }

    if(freport) fclose(freport);
    if(freportTxt) fclose(freportTxt);
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
        FileMetrics m = verify_and_compress(singleInput, compOut, reconOut, mode, useZero);
        printf("\n验证完成: %s 压缩比 %.2f:1 相似度 %.4f%%\n", singleInput, m.compressionRatio, m.avgSimilarity*100);
    }else{
        process_data_directory(mode, useZero);
    }

    printf("\n所有文件处理完成。\n");
    printf("压缩文件位于: %s/\n", OUT_COMPRESSED_DIR);
    printf("复原文件位于: %s/\n", OUT_RECONSTRUCTED_DIR);
    printf("验证报告位于: %s/verification_report.csv / .txt\n", OUT_REPORT_DIR);
    return 0;
}
